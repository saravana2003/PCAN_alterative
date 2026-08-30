/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2A — self-contained CAN-FD interface for canfd0 on the EK-RA8D1.
 *
 * Wraps the Zephyr native CAN controller API (zephyr/drivers/can.h) for the
 * on-chip Renesas RA CAN-FD controller (compatible "renesas,ra-canfd",
 * channel 0). The transceiver is external and passive (STB hard-wired to
 * GND); pin routing CTX0=P203 / CRX0=P202 comes from the board overlay.
 *
 * Build-only in this phase: no board attached, nothing is flashed or run.
 */

#ifndef CAN_LOGGER_CAN_IFACE_H_
#define CAN_LOGGER_CAN_IFACE_H_

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>

/**
 * @brief One captured/queued CAN(-FD) frame.
 *
 * Deliberately a superset of the fields the university report asks for:
 * ID, DLC, flags, data, timestamp.
 *
 * @note @p data is sized to CAN_MAX_DLEN, which is 64 when CONFIG_CAN_FD_MODE
 *       is enabled and 8 otherwise (see zephyr/include/zephyr/drivers/can.h).
 * @note @p timestamp is a *software* timestamp (k_uptime_ticks()) sampled
 *       inside the RX interrupt callback. The renesas,ra-canfd driver does
 *       not implement CONFIG_CAN_RX_TIMESTAMP, so no hardware bit-time
 *       timestamp is available on this SoC.
 */
struct canlog_frame {
	/** Standard (<= CAN_STD_ID_MASK) or extended (<= CAN_EXT_ID_MASK) ID. */
	uint32_t id;
	/** Raw Data Length Code. Byte count = can_dlc_to_bytes(dlc). */
	uint8_t dlc;
	/** Frame flags: CAN_FRAME_IDE | _RTR | _FDF | _BRS | _ESI. */
	uint8_t flags;
	/** Payload. Valid bytes = can_dlc_to_bytes(dlc). */
	uint8_t data[CAN_MAX_DLEN];
	/** k_uptime_ticks() captured in the RX ISR callback. */
	uint64_t timestamp;
};

/**
 * @brief RX handler, invoked from the CAN RX interrupt callback (ISR context).
 *
 * Keep it short and non-blocking. See can_iface.c for the "Interrupt + NVIC"
 * notes (canfd0 RX vector = NVIC IRQ 45, priority 12).
 */
typedef void (*canlog_rx_handler_t)(const struct canlog_frame *frame, void *user_data);

/**
 * @brief Configure canfd0 for CAN-FD, install the standard/extended/FD RX
 *        filters and start the controller.
 *
 * @retval 0        on success
 * @retval -ENODEV  canfd0 device not ready
 * @retval -ENOTSUP controller does not advertise CAN_MODE_FD
 * @retval <0       errno from can_set_mode()/can_add_rx_filter()/can_start()
 */
int can_iface_init(void);

/**
 * @brief Register the application RX handler (called in ISR context).
 *
 * @retval 0 always. Pass NULL to detach.
 */
int can_iface_set_rx_handler(canlog_rx_handler_t handler, void *user_data);

/**
 * @brief Queue a frame for transmission on canfd0 (non-blocking).
 *
 * Fills a struct can_frame from @p frame and calls can_send() with an
 * internal completion callback, so this never blocks on bus arbitration.
 *
 * @param frame   frame to send (id/dlc/flags/data are used)
 * @param timeout wait for a free TX mailbox, or K_NO_WAIT / K_FOREVER
 * @retval 0 on success, negative errno from can_send() otherwise
 */
int can_iface_send(const struct canlog_frame *frame, k_timeout_t timeout);

/**
 * @brief Thin pass-through to can_get_state() for canfd0.
 */
int can_iface_state(enum can_state *state, struct can_bus_err_cnt *err_cnt);

#endif /* CAN_LOGGER_CAN_IFACE_H_ */
