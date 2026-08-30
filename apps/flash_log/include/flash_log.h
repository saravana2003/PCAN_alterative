/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2D - persistent CAN-frame logger on the Octo-SPI flash (littlefs).
 *
 * Design points (see PROMPTS.md Phase 2D and STATE.md decisions):
 *   - Storage: littlefs on partition "canlog" of the on-board 64 MB OSPI NOR
 *     (&s28hl512t), mounted at /lfs. Partition + fstab node are in the app
 *     overlay.
 *   - Batched writes, NOT one flash write per frame: flash_log_record() only
 *     copies a serialised record into an SRAM ring buffer. A dedicated writer
 *     thread drains the ring to the open file in >= chunk-sized fs_write()s.
 *     This bounds flash wear and keeps the producer (a CAN RX ISR, later)
 *     non-blocking.
 *   - SRAM ring, not SDRAM: the board's 64 MB SDRAM is populated (SW1-7 ON by
 *     default) but the decisions log keeps the logger on on-chip SRAM unless
 *     SRAM proves insufficient for burst absorption. SDRAM stays a documented
 *     future option (bump FLASH_LOG_RING_SIZE and place the ring in the SDRAM
 *     region).
 *   - Lossy by design: a full ring drops the frame and counts it rather than
 *     stalling the caller.
 *
 * Independent of the Phase 2A/2B/2C modules - it only takes a plain frame
 * struct. Wiring flash_log_record() to the CAN RX path and flash_log_flush()
 * to the Phase 2B periodic timer is an integration step for when hardware is
 * available.
 *
 * On-disk format: see the "CLB1" description in flash_log.c.
 */

#ifndef FLASH_LOG_FLASH_LOG_H_
#define FLASH_LOG_FLASH_LOG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SRAM staging ring size. Raise (and relocate to SDRAM) for heavier bursts. */
#define FLASH_LOG_RING_SIZE 8192

/**
 * @brief One CAN(-FD) frame to be logged.
 *
 * Field semantics match the Phase 2A struct canlog_frame and the Phase 2C
 * struct clp_can_frame (flags bits = Zephyr CAN_FRAME_*).
 */
struct flash_log_frame {
	uint32_t id;
	uint8_t  flags;       /* bit0 FDF, bit1 BRS, bit2 ESI, bit3 IDE, bit4 RTR */
	uint8_t  dlc;         /* raw DLC; byte count via the CAN-FD DLC table    */
	uint8_t  data[64];
	uint64_t timestamp;   /* k_uptime_ticks() sampled by the caller          */
};

struct flash_log_stats {
	uint32_t records_queued;   /* accepted into the ring        */
	uint32_t records_dropped;  /* ring full                     */
	uint32_t bytes_written;    /* bytes fs_write()n to the file */
	uint32_t flush_count;      /* fs_sync() calls               */
	uint32_t write_errors;     /* fs_write()/fs_sync() failures */
};

/**
 * @brief Mount the littlefs log store at /lfs and start the writer thread.
 *
 * @retval 0        mounted
 * @retval -ENODEV  flash device / partition not available
 * @retval <0       errno from fs_mount()
 */
int flash_log_init(void);

/**
 * @brief Open a new log file "/lfs/log_<uptime_ticks>.clb" and write its header.
 *
 * @retval 0         logging started
 * @retval -EALREADY a log file is already open
 * @retval <0        errno from fs_open()/fs_write()
 */
int flash_log_start(void);

/** @return true while a log file is open. */
bool flash_log_active(void);

/**
 * @brief Queue a frame for logging (non-blocking).
 *
 * @retval 0        queued
 * @retval -ENOSPC  ring full, frame dropped (counted)
 * @retval -EPERM   logging not started
 */
int flash_log_record(const struct flash_log_frame *f);

/** Ask the writer thread to drain the ring and fs_sync() now. Non-blocking. */
int flash_log_flush(void);

/**
 * @brief Drain the ring, fs_sync(), and close the current log file.
 *
 * @retval 0        stopped
 * @retval -EALREADY no log file open
 * @retval <0       errno from fs_close()
 */
int flash_log_stop(void);

void flash_log_get_stats(struct flash_log_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_LOG_FLASH_LOG_H_ */
