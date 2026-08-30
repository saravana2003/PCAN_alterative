/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2C - CDC-ACM transport glue for the CLP protocol.
 *
 * Owns the USB CDC-ACM UART: interrupt-driven byte I/O, an outbound byte ring
 * buffer, and a CLP streaming parser on the inbound side. Encodes outgoing
 * messages with clp_proto and hands decoded CLP_MSG_CAN_TX requests to a
 * registered handler.
 *
 * Independent of the Phase 2A CAN driver and the Phase 2B GPIO/timer module:
 * it deals only in struct clp_can_frame and friends. Wiring it to a real CAN
 * controller is an integration step for when hardware is available.
 */

#ifndef USB_CDC_USB_LINK_H_
#define USB_CDC_USB_LINK_H_

#include <stdbool.h>
#include <stdint.h>

#include "clp_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Invoked (in a thread context, not ISR) for each CLP_MSG_CAN_TX from the host. */
typedef void (*usb_link_tx_request_cb_t)(const struct clp_can_frame *req,
					 void *user_data);

/**
 * @brief Bring up the USB device and the CDC-ACM endpoint.
 *
 * Does not block waiting for the host. A HELLO frame is queued automatically
 * when the host asserts DTR.
 *
 * @retval 0        success
 * @retval -ENODEV  CDC-ACM UART or USB device not ready
 * @retval <0       errno from the USB stack
 */
int usb_link_init(void);

/** @return true once the host has opened the port (DTR asserted). */
bool usb_link_connected(void);

/** Register the CLP_MSG_CAN_TX handler. Pass NULL to detach. */
void usb_link_set_tx_handler(usb_link_tx_request_cb_t cb, void *user_data);

/**
 * @brief Queue a received CAN frame for the host as a CLP_MSG_CAN_RX.
 *
 * Non-blocking. If the outbound buffer is full (host not draining USB) the
 * frame is dropped and the rx-drop counter is incremented - CAN RX must never
 * stall on USB back-pressure.
 *
 * @retval 0        queued
 * @retval -ENOSPC  dropped, buffer full
 * @retval -ENOTCONN host not connected
 * @retval <0       encode error
 */
int usb_link_send_can_rx(const struct clp_can_frame *f);

/** Queue a CLP_MSG_CAN_TX_ACK for the host. Same semantics as above. */
int usb_link_send_tx_ack(const struct clp_tx_ack *ack);

/** Queue a CLP_MSG_STATUS for the host. Same semantics as above. */
int usb_link_send_status(const struct clp_status *st);

/** Snapshot of link counters (any pointer may be NULL). */
void usb_link_get_counters(uint32_t *tx_frames, uint32_t *rx_drops,
			   uint32_t *crc_errors, uint32_t *framing_errors);

#ifdef __cplusplus
}
#endif

#endif /* USB_CDC_USB_LINK_H_ */
