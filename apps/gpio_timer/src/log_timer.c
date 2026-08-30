/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2B - "Timer" deliverable. Build-only; nothing is flashed.
 *
 * WHY k_timer AND NOT THE ON-CHIP AGT:
 *   PROMPTS.md Phase 2B says "k_timer or the on-chip AGT counter - check which
 *   is simpler in this Zephyr version". k_timer wins here:
 *     - No devicetree overlay. agt0/agt1 (renesas,ra-agt @0x40221000/0x40221100,
 *       NVIC IRQ 83 "agti") are status=disabled in
 *       dts/arm/renesas/ra/ra8/ra8x1.dtsi and the ek_ra8d1 board never enables
 *       them, so AGT would need an overlay turning on &agt0 *and* its child
 *       counter node.
 *     - No extra Kconfig. AGT would need CONFIG_COUNTER + CONFIG_COUNTER_RA_AGT
 *       (drivers/counter/counter_renesas_ra_agt.c) and the counter-alarm API.
 *     - k_timer is always built into the kernel and its period/callback model
 *       is exactly what a "timestamp + periodic flush" tick needs.
 *   The AGT path stays documented above as the hardware-timer alternative if a
 *   later phase needs a timer that keeps running through deep sleep or needs
 *   sub-tick resolution independent of CONFIG_SYS_CLOCK_TICKS_PER_SEC.
 *
 * The k_timer expiry function runs in the system clock announce context
 * (ISR context on this SoC) - see the Zephyr k_timer docs. Treat it like an
 * ISR: short, non-blocking.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "log_timer.h"

LOG_MODULE_REGISTER(log_timer, LOG_LEVEL_INF);

static void tick_expiry(struct k_timer *t);
static K_TIMER_DEFINE(periodic, tick_expiry, NULL);

static log_timer_tick_cb_t tick_cb;
static void *tick_cb_data;
static uint32_t tick_seq;
static bool running;

static void tick_expiry(struct k_timer *t)
{
	ARG_UNUSED(t);

	uint32_t seq = ++tick_seq;
	uint64_t ts = k_uptime_ticks();

	if (tick_cb != NULL) {
		tick_cb(seq, ts, tick_cb_data);
	}
}

int log_timer_start(uint32_t period_ms, log_timer_tick_cb_t cb, void *user_data)
{
	if (period_ms == 0U) {
		return -EINVAL;
	}
	if (running) {
		return -EALREADY;
	}

	tick_cb = cb;
	tick_cb_data = user_data;
	tick_seq = 0U;
	running = true;

	/* First expiry one period out, then repeat every period. */
	k_timer_start(&periodic, K_MSEC(period_ms), K_MSEC(period_ms));
	LOG_INF("periodic tick started: %u ms", period_ms);
	return 0;
}

void log_timer_stop(void)
{
	if (!running) {
		return;
	}
	k_timer_stop(&periodic);
	running = false;
	tick_cb = NULL;
	tick_cb_data = NULL;
	LOG_INF("periodic tick stopped after %u ticks", tick_seq);
}

uint32_t log_timer_seq(void)
{
	return tick_seq;
}

uint64_t log_timer_timestamp(void)
{
	return k_uptime_ticks();
}
