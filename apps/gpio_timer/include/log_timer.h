/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2B - periodic timer module. THIS IS THE "TIMER" DELIVERABLE for the
 * BITS Pilani WILP Embedded Systems Design report.
 *
 * Purpose in the wider system (not wired up until hardware integration):
 *   1. Provide a monotonic timestamp source for CAN frames captured by the
 *      Phase 2A module (apps/can_logger uses k_uptime_ticks() in its RX ISR;
 *      log_timer_timestamp() returns the same clock so the two agree).
 *   2. Fire a periodic "flush trigger" so the Phase 2D flash logger writes
 *      buffered frames in batches instead of one flash write per frame.
 *
 * Implementation: Zephyr kernel k_timer (see log_timer.c for why k_timer and
 * not the on-chip AGT counter). Self-contained: no dependency on io_control,
 * CAN or flash - main.c connects it to the rest for the build/demo only.
 */

#ifndef GPIO_TIMER_LOG_TIMER_H_
#define GPIO_TIMER_LOG_TIMER_H_

#include <stdint.h>

/**
 * @brief Periodic tick callback.
 *
 * @param seq           tick counter since log_timer_start() (starts at 1)
 * @param uptime_ticks  k_uptime_ticks() sampled at expiry
 * @param user_data     pointer passed to log_timer_start()
 *
 * Runs in the system timer / ISR context - keep it short and non-blocking
 * (submit a k_work item for anything heavy such as an actual flash flush).
 */
typedef void (*log_timer_tick_cb_t)(uint32_t seq, uint64_t uptime_ticks, void *user_data);

/**
 * @brief Start the periodic tick.
 *
 * @param period_ms  tick period in milliseconds (must be > 0)
 * @param cb         callback, or NULL for a bare timekeeping tick
 * @param user_data  opaque pointer handed back to @p cb
 * @retval 0        on success
 * @retval -EINVAL  period_ms == 0
 * @retval -EALREADY already running
 */
int log_timer_start(uint32_t period_ms, log_timer_tick_cb_t cb, void *user_data);

/** Stop the periodic tick. Safe to call when already stopped. */
void log_timer_stop(void);

/** @return number of ticks elapsed since log_timer_start(). */
uint32_t log_timer_seq(void);

/**
 * @brief Monotonic timestamp for tagging CAN frames.
 * @return k_uptime_ticks() - same clock the Phase 2A RX ISR samples.
 */
uint64_t log_timer_timestamp(void);

#endif /* GPIO_TIMER_LOG_TIMER_H_ */
