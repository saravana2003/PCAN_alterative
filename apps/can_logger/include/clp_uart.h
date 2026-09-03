/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * CLP transport glue for a plain hardware UART (the J-Link VCOM on COM12,
 * chosen zephyr,console = &uart9 on the EK-RA8D1).
 *
 * Mirrors apps/usb_cdc/src/usb_link.c (the CDC-ACM transport) but for an
 * interrupt-driven SCI UART:
 *   - TX: clp_uart_send_*() encode a COBS-framed CLP frame on the caller's
 *     stack and push the bytes into an outbound ring buffer under a spinlock.
 *     Callable from any context including the CAN RX ISR. Lossy by design:
 *     a full ring drops the frame and bumps a counter, never blocks.
 *   - RX: the UART ISR copies received bytes into an inbound ring and wakes a
 *     worker thread, which feeds clp_parser and forwards decoded
 *     CLP_MSG_CAN_TX requests to the registered handler (thread context).
 *
 * There is no DTR / connect concept on a bare UART, so a HELLO is sent once at
 * init and the caller is expected to emit periodic CLP_MSG_STATUS frames so a
 * host that attaches late can resync (COBS guarantees resync at the next 0x00).
 */

#ifndef CAN_LOGGER_CLP_UART_H_
#define CAN_LOGGER_CLP_UART_H_

#include <stdbool.h>
#include <stdint.h>

#include "clp_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Invoked in worker-thread context for each CLP_MSG_CAN_TX received. */
typedef void (*clp_uart_tx_request_cb_t)(const struct clp_can_frame *req,
					 void *user_data);

/**
 * @brief Take over the console UART, start the RX worker, queue a HELLO.
 *
 * @retval 0        success
 * @retval -ENODEV  the UART device is not ready
 */
int clp_uart_init(void);

/** Register the CLP_MSG_CAN_TX handler. Pass NULL to detach. */
void clp_uart_set_tx_handler(clp_uart_tx_request_cb_t cb, void *user_data);

/**
 * @brief Queue a HELLO frame. Sent once by clp_uart_init(); call again
 *        periodically so a host that attaches mid-run learns the fw version.
 */
void clp_uart_send_hello(void);

/**
 * @brief Queue a received CAN frame for the host as CLP_MSG_CAN_RX.
 *
 * Non-blocking. @retval 0 queued, -ENOSPC dropped (ring full), <0 encode error.
 */
int clp_uart_send_can_rx(const struct clp_can_frame *f);

/** Queue a CLP_MSG_CAN_TX_ACK. Same semantics as clp_uart_send_can_rx(). */
int clp_uart_send_tx_ack(const struct clp_tx_ack *ack);

/** Queue a CLP_MSG_STATUS. Same semantics as clp_uart_send_can_rx(). */
int clp_uart_send_status(const struct clp_status *st);

/** Snapshot of link counters (any pointer may be NULL). */
void clp_uart_get_counters(uint32_t *tx_frames, uint32_t *tx_drops,
			   uint32_t *crc_errors, uint32_t *framing_errors);

#ifdef __cplusplus
}
#endif

#endif /* CAN_LOGGER_CLP_UART_H_ */
