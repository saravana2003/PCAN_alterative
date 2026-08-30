/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2D demo entry point (build-only, no board).
 *
 * Exercises the littlefs CAN logger so the whole module is compiled and
 * linked: mount -> start -> feed synthetic frames -> flush -> stop. None of
 * this runs without hardware (the OSPI flash and littlefs are never actually
 * touched in this session); there is no flashing in this phase.
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "flash_log.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* bit4 = RTR, matching flash_log_frame.flags / CAN_FRAME_* */
#define FRAME_FLAG_FDF 0x01
#define FRAME_FLAG_BRS 0x02

int main(void)
{
	struct flash_log_stats st;
	int ret;

	ret = flash_log_init();
	if (ret != 0) {
		LOG_ERR("flash_log_init failed: %d", ret);
		return 0;
	}

	ret = flash_log_start();
	if (ret != 0) {
		LOG_ERR("flash_log_start failed: %d", ret);
		return 0;
	}

	/* Simulate the "start logging" button having been pressed and a short
	 * burst of bus traffic arriving. flash_log_record() would normally be
	 * called from the CAN RX ISR (Phase 2A) with k_uptime_ticks() captured
	 * there.
	 */
	for (uint32_t i = 0; i < 200; i++) {
		struct flash_log_frame f = {
			.id = 0x100 + (i & 0x7F),
			.flags = FRAME_FLAG_FDF | FRAME_FLAG_BRS,
			.dlc = 8,
			.timestamp = k_uptime_ticks(),
		};

		for (int b = 0; b < 8; b++) {
			f.data[b] = (uint8_t)(i + b);
		}

		ret = flash_log_record(&f);
		if (ret == -ENOSPC) {
			LOG_WRN("ring full at frame %u (writer would catch up)", i);
		}
	}

	/* "Periodic flush trigger" (Phase 2B timer would call this). */
	flash_log_flush();
	k_sleep(K_MSEC(50));

	ret = flash_log_stop();
	if (ret != 0) {
		LOG_ERR("flash_log_stop failed: %d", ret);
	}

	flash_log_get_stats(&st);
	LOG_INF("stats: queued=%u dropped=%u bytes=%u flushes=%u werr=%u",
		st.records_queued, st.records_dropped, st.bytes_written,
		st.flush_count, st.write_errors);

	return 0;
}
