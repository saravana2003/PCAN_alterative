/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2B demo entry point (build-only, no board).
 *
 * Compiles and links the two independent modules and shows how they connect:
 *   io_control  - S1/S2 buttons (edge IRQ) drive capture start/stop, LEDs show
 *                 state (green idle / red blinking while logging).
 *   log_timer   - "Timer" deliverable: a periodic tick that will timestamp CAN
 *                 frames and trigger batched flash flushes.
 *
 * The wiring here (start capture -> start the tick, stop -> stop the tick) is
 * done in main so the modules stay decoupled and each stays build-testable on
 * its own. Nothing runs without hardware; there is no flashing in this phase.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "io_control.h"
#include "log_timer.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Periodic flush cadence - matches STATE.md's "batch writes, don't write one
 * flash page per CAN frame" decision. Tunable once real bus load is measured.
 */
#define FLUSH_PERIOD_MS 100U

/* Tick handler - ISR context (k_timer expiry). */
static void on_tick(uint32_t seq, uint64_t uptime_ticks, void *user_data)
{
	ARG_UNUSED(user_data);

	/* In the integrated system: swap buffers and submit a k_work item to
	 * write the filled batch to littlefs (Phase 2D). Here we just log.
	 */
	LOG_INF("flush tick #%u ts=%llu", seq, (unsigned long long)uptime_ticks);
}

/* Capture state-change handler - may be ISR context (button interrupt). */
static void on_capture_state(enum capture_state new_state, void *user_data)
{
	ARG_UNUSED(user_data);

	if (new_state == CAPTURE_LOGGING) {
		LOG_INF("capture START -> periodic flush timer on");
		(void)log_timer_start(FLUSH_PERIOD_MS, on_tick, NULL);
	} else {
		LOG_INF("capture STOP -> periodic flush timer off");
		log_timer_stop();
	}
}

int main(void)
{
	int ret;

	ret = io_control_init();
	if (ret != 0) {
		LOG_ERR("io_control_init failed: %d", ret);
		return 0;
	}

	io_control_set_callback(on_capture_state, NULL);

	/* Exercise the path without a physical button press so a build/run on
	 * future hardware shows the timer ticking even before anyone touches S1.
	 */
	io_control_request(CAPTURE_LOGGING);
	k_sleep(K_MSEC(500));
	io_control_request(CAPTURE_IDLE);

	LOG_INF("gpio_timer demo idle; press S1 to start, S2 to stop");
	return 0;
}
