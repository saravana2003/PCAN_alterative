/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2D - littlefs CAN-frame logger. Build-only; nothing is flashed and the
 * filesystem is never actually mounted in this session (no board).
 *
 * On-disk format ("CLB1"), little-endian, one file per logging session:
 *
 *   File header (16 bytes):
 *     off 0  : magic  "CLB1"                (4 bytes)
 *     off 4  : format_version  u16          (= 1)
 *     off 6  : header_length   u16          (= 16)
 *     off 8  : start_timestamp u64          (k_uptime_ticks() at flash_log_start)
 *
 *   Then a stream of variable-length records:
 *     off 0  : marker  u8   (= 0xC5, lets an offline tool resync)
 *     off 1  : flags   u8   (CAN_FRAME_* bits: FDF|BRS|ESI|IDE|RTR)
 *     off 2  : dlc     u8   (raw DLC)
 *     off 3  : n       u8   (data byte count = dlc_to_bytes(dlc), 0 if RTR)
 *     off 4  : can_id  u32
 *     off 8  : timestamp u64
 *     off 16 : data    n bytes
 *   record length = 16 + n.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>

#include "flash_log.h"

LOG_MODULE_REGISTER(flash_log, LOG_LEVEL_INF);

/* fstab node from boards/ek_ra8d1.overlay. */
FS_FSTAB_DECLARE_ENTRY(DT_NODELABEL(lfs_canlog));
#define LOG_MP (&FS_FSTAB_ENTRY(DT_NODELABEL(lfs_canlog)))

BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(lfs_canlog)),
	     "missing lfs_canlog fstab node - check the app overlay");

#define WRITE_CHUNK       1024U   /* max bytes per fs_write() batch  */
#define WRITER_IDLE_MS    500     /* drain accumulated data this often */
#define WRITER_STACK_SIZE 6144
#define STOP_WAIT_MS      2000

#define REC_MARKER 0xC5U
#define REC_FIXED  16U           /* marker+flags+dlc+n + id(4) + ts(8) */

RING_BUF_DECLARE(log_rb, FLASH_LOG_RING_SIZE);
static struct k_spinlock rb_lock;

static struct fs_file_t log_file;
static K_MUTEX_DEFINE(file_lock);
static atomic_t active;
static atomic_t flush_req;
static atomic_t mounted;

static K_SEM_DEFINE(wake, 0, 1);

/* producer-updated */
static atomic_t stat_queued;
static atomic_t stat_dropped;
/* writer-thread-updated (single writer + the stop path, serialised by file_lock) */
static uint32_t stat_bytes;
static uint32_t stat_flush;
static uint32_t stat_werr;

/* CAN-FD DLC -> byte count (local copy; no <zephyr/drivers/can.h> dependency). */
static uint8_t dlc_to_bytes(uint8_t dlc)
{
	static const uint8_t tbl[16] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64,
	};
	return tbl[dlc & 0x0FU];
}

static uint32_t rb_get_locked(uint8_t *buf, uint32_t size)
{
	k_spinlock_key_t key = k_spin_lock(&rb_lock);
	uint32_t n = ring_buf_get(&log_rb, buf, size);

	k_spin_unlock(&rb_lock, key);
	return n;
}

static uint32_t rb_size_locked(void)
{
	k_spinlock_key_t key = k_spin_lock(&rb_lock);
	uint32_t n = ring_buf_size_get(&log_rb);

	k_spin_unlock(&rb_lock, key);
	return n;
}

/* Drain the ring to the open file. Caller holds file_lock and has confirmed a
 * file is open. @p min_bytes: only write once at least this many bytes are
 * queued (WRITE_CHUNK for the steady state, 1 to fully drain).
 */
static void drain_to_file(uint8_t *buf, uint32_t min_bytes)
{
	while (rb_size_locked() >= min_bytes) {
		uint32_t n = rb_get_locked(buf, WRITE_CHUNK);
		ssize_t w;

		if (n == 0U) {
			break;
		}

		w = fs_write(&log_file, buf, n);
		if (w < 0) {
			LOG_ERR("fs_write failed: %d", (int)w);
			stat_werr++;
			break;
		}
		stat_bytes += (uint32_t)w;

		if ((uint32_t)w < n) {
			/* Short write - return the tail to the ring and stop. */
			k_spinlock_key_t key = k_spin_lock(&rb_lock);

			ring_buf_put(&log_rb, &buf[w], n - (uint32_t)w);
			k_spin_unlock(&rb_lock, key);
			break;
		}
	}
}

static void writer_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	uint8_t buf[WRITE_CHUNK];

	for (;;) {
		bool flush;

		(void)k_sem_take(&wake, K_MSEC(WRITER_IDLE_MS));
		flush = atomic_set(&flush_req, 0) != 0;

		k_mutex_lock(&file_lock, K_FOREVER);
		if (atomic_get(&active)) {
			drain_to_file(buf, flush ? 1U : WRITE_CHUNK);

			if (flush && fs_sync(&log_file) == 0) {
				stat_flush++;
			} else if (flush) {
				stat_werr++;
			}
		}
		k_mutex_unlock(&file_lock);
	}
}

K_THREAD_DEFINE(flash_log_writer, WRITER_STACK_SIZE, writer_fn, NULL, NULL, NULL,
		K_PRIO_PREEMPT(7), 0, 0);

/* -------------------------------------------------------------------------- */

int flash_log_init(void)
{
	int rc;

	if (atomic_get(&mounted)) {
		return 0;
	}

	fs_file_t_init(&log_file);

	rc = fs_mount(LOG_MP);
	if (rc < 0) {
		LOG_ERR("fs_mount(%s) failed: %d", LOG_MP->mnt_point, rc);
		return rc;
	}

	atomic_set(&mounted, 1);
	LOG_INF("littlefs mounted at %s", LOG_MP->mnt_point);

	struct fs_statvfs sb;

	if (fs_statvfs(LOG_MP->mnt_point, &sb) == 0) {
		LOG_INF("  bsize=%lu frsize=%lu blocks=%lu bfree=%lu",
			sb.f_bsize, sb.f_frsize, sb.f_blocks, sb.f_bfree);
	}
	return 0;
}

int flash_log_start(void)
{
	char name[40];
	uint8_t hdr[16];
	int rc;

	if (!atomic_get(&mounted)) {
		return -ENODEV;
	}

	k_mutex_lock(&file_lock, K_FOREVER);
	if (atomic_get(&active)) {
		k_mutex_unlock(&file_lock);
		return -EALREADY;
	}

	snprintf(name, sizeof(name), "%s/log_%llu.clb", LOG_MP->mnt_point,
		 (unsigned long long)k_uptime_ticks());

	rc = fs_open(&log_file, name, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		LOG_ERR("fs_open(%s) failed: %d", name, rc);
		k_mutex_unlock(&file_lock);
		return rc;
	}

	memcpy(&hdr[0], "CLB1", 4);
	sys_put_le16(1, &hdr[4]);
	sys_put_le16(sizeof(hdr), &hdr[6]);
	sys_put_le64(k_uptime_ticks(), &hdr[8]);

	rc = fs_write(&log_file, hdr, sizeof(hdr));
	if (rc < 0) {
		LOG_ERR("header write failed: %d", rc);
		(void)fs_close(&log_file);
		k_mutex_unlock(&file_lock);
		return rc;
	}
	stat_bytes += (uint32_t)rc;

	atomic_set(&active, 1);
	k_mutex_unlock(&file_lock);

	LOG_INF("logging to %s", name);
	return 0;
}

bool flash_log_active(void)
{
	return atomic_get(&active) != 0;
}

int flash_log_record(const struct flash_log_frame *f)
{
	uint8_t rec[REC_FIXED + 64];
	uint8_t n;
	uint32_t wrote;
	k_spinlock_key_t key;

	if (!atomic_get(&active)) {
		return -EPERM;
	}

	n = dlc_to_bytes(f->dlc);
	if ((f->flags & BIT(4)) != 0U) { /* RTR: no data */
		n = 0U;
	}

	rec[0] = REC_MARKER;
	rec[1] = f->flags;
	rec[2] = f->dlc;
	rec[3] = n;
	sys_put_le32(f->id, &rec[4]);
	sys_put_le64(f->timestamp, &rec[8]);
	memcpy(&rec[REC_FIXED], f->data, n);

	key = k_spin_lock(&rb_lock);
	if (ring_buf_space_get(&log_rb) < (uint32_t)(REC_FIXED + n)) {
		k_spin_unlock(&rb_lock, key);
		atomic_inc(&stat_dropped);
		return -ENOSPC;
	}
	wrote = ring_buf_put(&log_rb, rec, REC_FIXED + n);
	k_spin_unlock(&rb_lock, key);

	if (wrote != (uint32_t)(REC_FIXED + n)) {
		atomic_inc(&stat_dropped);
		return -ENOSPC;
	}

	atomic_inc(&stat_queued);

	/* Nudge the writer once the ring is half full. */
	if (rb_size_locked() >= (FLASH_LOG_RING_SIZE / 2U)) {
		k_sem_give(&wake);
	}
	return 0;
}

int flash_log_flush(void)
{
	if (!atomic_get(&active)) {
		return -EPERM;
	}
	atomic_set(&flush_req, 1);
	k_sem_give(&wake);
	return 0;
}

int flash_log_stop(void)
{
	uint8_t buf[WRITE_CHUNK];
	int rc;

	k_mutex_lock(&file_lock, K_FOREVER);
	if (!atomic_get(&active)) {
		k_mutex_unlock(&file_lock);
		return -EALREADY;
	}
	k_mutex_unlock(&file_lock);

	/* Let the writer drain what it can. */
	atomic_set(&flush_req, 1);
	k_sem_give(&wake);
	for (int i = 0; i < (STOP_WAIT_MS / 50) && rb_size_locked() > 0U; i++) {
		k_sleep(K_MSEC(50));
	}

	/* Take ownership, drain the remainder synchronously, close. */
	k_mutex_lock(&file_lock, K_FOREVER);
	atomic_set(&active, 0);
	drain_to_file(buf, 1U);
	(void)fs_sync(&log_file);
	stat_flush++;
	rc = fs_close(&log_file);
	k_mutex_unlock(&file_lock);

	if (rc < 0) {
		LOG_ERR("fs_close failed: %d", rc);
		return rc;
	}
	LOG_INF("logging stopped (%u bytes written)", stat_bytes);
	return 0;
}

void flash_log_get_stats(struct flash_log_stats *out)
{
	out->records_queued = (uint32_t)atomic_get(&stat_queued);
	out->records_dropped = (uint32_t)atomic_get(&stat_dropped);
	out->bytes_written = stat_bytes;
	out->flush_count = stat_flush;
	out->write_errors = stat_werr;
}
