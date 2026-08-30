/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2C - CDC-ACM <-> CLP glue. Build-only; nothing is flashed.
 *
 * TX path: usb_link_send_*() encode a COBS-framed CLP frame onto the stack,
 * then push the bytes into tx_rb under a spinlock. The CDC-ACM TX interrupt
 * drains tx_rb into the USB FIFO. Producers may be any context (incl. a CAN
 * RX ISR later), so the buffer is lock-guarded and lossy-by-design: a full
 * buffer drops the frame and bumps a counter rather than blocking.
 *
 * RX path: the CDC-ACM RX interrupt reads the FIFO and feeds clp_parser, which
 * calls on_clp_frame() for each CRC-valid frame. CLP_MSG_CAN_TX is decoded and
 * forwarded to the registered handler.
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

#include "usb_link.h"
#include "usbd_ctx.h"

LOG_MODULE_REGISTER(usb_link, LOG_LEVEL_INF);

#define USB_LINK_FW_VERSION "usb_cdc phase-2c v0.1"

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(zephyr_cdc_acm_uart),
	     "no enabled zephyr,cdc-acm-uart node - add one in the app overlay");

static const struct device *const cdc_dev =
	DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

#define TX_RING_SIZE 2048
RING_BUF_DECLARE(tx_rb, TX_RING_SIZE);
static struct k_spinlock tx_lock;

#define RX_CHUNK 64

static struct usbd_context *usbd;
static struct clp_parser parser;

static usb_link_tx_request_cb_t tx_req_cb;
static void *tx_req_cb_data;

static atomic_t connected = ATOMIC_INIT(0);
static uint8_t tx_seq;

/* Counters (host-visible via STATUS / usb_link_get_counters). */
static atomic_t stat_tx_frames;
static atomic_t stat_rx_drops;

/* -------------------------------------------------------------------------- */
/* Outbound                                                                   */
/* -------------------------------------------------------------------------- */

static int tx_push(const uint8_t *wire, size_t len)
{
	k_spinlock_key_t key;
	uint32_t wrote;

	if (!atomic_get(&connected)) {
		return -ENOTCONN;
	}

	key = k_spin_lock(&tx_lock);
	if (ring_buf_space_get(&tx_rb) < len) {
		k_spin_unlock(&tx_lock, key);
		atomic_inc(&stat_rx_drops);
		return -ENOSPC;
	}
	wrote = ring_buf_put(&tx_rb, wire, len);
	k_spin_unlock(&tx_lock, key);

	if (wrote != len) {
		atomic_inc(&stat_rx_drops);
		return -ENOSPC;
	}

	uart_irq_tx_enable(cdc_dev);
	return 0;
}

int usb_link_send_can_rx(const struct clp_can_frame *f)
{
	uint8_t wire[CLP_MAX_WIRE];
	int n;

	n = clp_encode_can_frame(CLP_MSG_CAN_RX, tx_seq++, f, wire, sizeof(wire));
	if (n < 0) {
		return n;
	}

	n = tx_push(wire, (size_t)n);
	if (n == 0) {
		atomic_inc(&stat_tx_frames);
	}
	return n;
}

int usb_link_send_tx_ack(const struct clp_tx_ack *ack)
{
	uint8_t wire[CLP_MAX_WIRE];
	int n = clp_encode_tx_ack(tx_seq++, ack, wire, sizeof(wire));

	if (n < 0) {
		return n;
	}
	return tx_push(wire, (size_t)n);
}

int usb_link_send_status(const struct clp_status *st)
{
	uint8_t wire[CLP_MAX_WIRE];
	int n = clp_encode_status(tx_seq++, st, wire, sizeof(wire));

	if (n < 0) {
		return n;
	}
	return tx_push(wire, (size_t)n);
}

static void send_hello(void)
{
	uint8_t wire[CLP_MAX_WIRE];
	int n = clp_encode_hello(tx_seq++, USB_LINK_FW_VERSION,
				 wire, sizeof(wire));

	if (n > 0) {
		(void)tx_push(wire, (size_t)n);
	}
}

/* -------------------------------------------------------------------------- */
/* Inbound - CLP frame dispatch                                               */
/* -------------------------------------------------------------------------- */

static void on_clp_frame(uint8_t type, uint8_t seq, const uint8_t *payload,
			 uint16_t len, void *user_data)
{
	ARG_UNUSED(seq);
	ARG_UNUSED(user_data);

	switch (type) {
	case CLP_MSG_CAN_TX: {
		struct clp_can_frame req;

		if (clp_decode_can_frame(payload, len, &req) != 0) {
			LOG_WRN("CAN_TX decode failed (len %u)", len);
			return;
		}
		if (tx_req_cb != NULL) {
			tx_req_cb(&req, tx_req_cb_data);
		}
		break;
	}
	default:
		LOG_DBG("ignoring inbound CLP type 0x%02x", type);
		break;
	}
}

/* -------------------------------------------------------------------------- */
/* CDC-ACM interrupt handler (RX + TX servicing)                              */
/* -------------------------------------------------------------------------- */

static void cdc_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (true) {
		uart_irq_update(dev);
		if (uart_irq_is_pending(dev) <= 0) {
			break;
		}

		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[RX_CHUNK];
			int n = uart_fifo_read(dev, buf, sizeof(buf));

			if (n > 0) {
				(void)clp_parser_feed(&parser, buf, (size_t)n);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[RX_CHUNK];
			k_spinlock_key_t key = k_spin_lock(&tx_lock);
			uint32_t n = ring_buf_get(&tx_rb, buf, sizeof(buf));

			k_spin_unlock(&tx_lock, key);

			if (n == 0) {
				uart_irq_tx_disable(dev);
			} else {
				int sent = uart_fifo_fill(dev, buf, n);

				if (sent < (int)n) {
					/* Put the tail back for the next IRQ. */
					key = k_spin_lock(&tx_lock);
					ring_buf_put(&tx_rb, &buf[sent],
						     n - (uint32_t)sent);
					k_spin_unlock(&tx_lock, key);
				}
			}
		}
	}
}

/* -------------------------------------------------------------------------- */
/* USB device message callback                                                */
/* -------------------------------------------------------------------------- */

static void usbd_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("usbd_enable failed");
			}
		} else if (msg->type == USBD_MSG_VBUS_REMOVED) {
			(void)usbd_disable(ctx);
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		(void)uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr && atomic_set(&connected, 1) == 0) {
			LOG_INF("host connected (DTR) - sending HELLO");
			send_hello();
			uart_irq_rx_enable(cdc_dev);
		} else if (!dtr) {
			atomic_set(&connected, 0);
		}
	}
}

/* -------------------------------------------------------------------------- */
/* Public                                                                     */
/* -------------------------------------------------------------------------- */

void usb_link_set_tx_handler(usb_link_tx_request_cb_t cb, void *user_data)
{
	tx_req_cb = cb;
	tx_req_cb_data = user_data;
}

bool usb_link_connected(void)
{
	return atomic_get(&connected) != 0;
}

void usb_link_get_counters(uint32_t *tx_frames, uint32_t *rx_drops,
			   uint32_t *crc_errors, uint32_t *framing_errors)
{
	if (tx_frames != NULL) {
		*tx_frames = (uint32_t)atomic_get(&stat_tx_frames);
	}
	if (rx_drops != NULL) {
		*rx_drops = (uint32_t)atomic_get(&stat_rx_drops);
	}
	if (crc_errors != NULL) {
		*crc_errors = parser.crc_errors;
	}
	if (framing_errors != NULL) {
		*framing_errors = parser.framing_errors;
	}
}

int usb_link_init(void)
{
	int err;

	if (!device_is_ready(cdc_dev)) {
		LOG_ERR("CDC-ACM device not ready");
		return -ENODEV;
	}

	clp_parser_init(&parser, on_clp_frame, NULL);

	usbd = usb_cdc_init_usbd(usbd_msg_cb);
	if (usbd == NULL) {
		return -ENODEV;
	}

	uart_irq_rx_disable(cdc_dev);
	uart_irq_tx_disable(cdc_dev);
	uart_irq_callback_set(cdc_dev, cdc_isr);

	if (!usbd_can_detect_vbus(usbd)) {
		err = usbd_enable(usbd);
		if (err) {
			LOG_ERR("usbd_enable failed: %d", err);
			return err;
		}
	}

	LOG_INF("USB CDC-ACM up; waiting for host");
	return 0;
}
