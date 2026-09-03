/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * CLP <-> hardware-UART transport glue. See clp_uart.h for the model.
 *
 * Adapted from apps/usb_cdc/src/usb_link.c. The differences that matter:
 *   - The device is the chosen console UART (uart9 / J-Link VCOM / COM12),
 *     driven through the interrupt API of uart_renesas_ra8_sci_b.c.
 *   - That callback is a *true ISR* (unlike the CDC-ACM one, which the USB
 *     stack services from a workqueue), so the ISR does the minimum — copy
 *     RX bytes into a ring, drain the TX ring — and a worker thread runs the
 *     CLP parser and the inbound CAN_TX dispatch.
 *   - No DTR/VBUS: the link is always "up". HELLO is sent once at init.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include "clp_uart.h"

LOG_MODULE_REGISTER(clp_uart, LOG_LEVEL_INF);

#define CLP_UART_FW_VERSION "can_logger clp-uart v0.1"

/* The CLP wire is the console UART. UART_CONSOLE is disabled in prj.conf so
 * nothing else owns this device's IRQ.
 */
#define CLP_UART_NODE DT_CHOSEN(zephyr_console)
BUILD_ASSERT(DT_NODE_HAS_STATUS(CLP_UART_NODE, okay),
	     "chosen zephyr,console UART is not enabled");

static const struct device *const uart_dev = DEVICE_DT_GET(CLP_UART_NODE);

/* Outbound: encoded CLP bytes waiting for the TX ISR. 2 KiB matches usb_link.
 * At 115200 baud a full ring drains in ~180 ms; under sustained CAN load the
 * host-not-draining case is handled by dropping + counting (tx_drops).
 */
#define TX_RING_SIZE 2048
RING_BUF_DECLARE(tx_rb, TX_RING_SIZE);
static struct k_spinlock tx_lock;

/* Inbound: raw bytes from the RX ISR, consumed by rx_thread. Host -> device
 * traffic is only occasional CAN_TX commands, so this stays small.
 */
#define RX_RING_SIZE 512
RING_BUF_DECLARE(rx_rb, RX_RING_SIZE);
static struct k_spinlock rx_lock;
static K_SEM_DEFINE(rx_sem, 0, 1);

#define ISR_CHUNK 64

static struct clp_parser parser;

static clp_uart_tx_request_cb_t tx_req_cb;
static void *tx_req_cb_data;

static uint8_t tx_seq;

/* Counters, host-visible via CLP_MSG_STATUS / clp_uart_get_counters(). */
static atomic_t stat_tx_frames;
static atomic_t stat_tx_drops;

/* -------------------------------------------------------------------------- */
/* Outbound                                                                   */
/* -------------------------------------------------------------------------- */

static int tx_push(const uint8_t *wire, size_t len)
{
	k_spinlock_key_t key;
	uint32_t wrote;

	key = k_spin_lock(&tx_lock);
	if (ring_buf_space_get(&tx_rb) < len) {
		k_spin_unlock(&tx_lock, key);
		atomic_inc(&stat_tx_drops);
		return -ENOSPC;
	}
	wrote = ring_buf_put(&tx_rb, wire, len);
	k_spin_unlock(&tx_lock, key);

	if (wrote != len) {
		atomic_inc(&stat_tx_drops);
		return -ENOSPC;
	}

	uart_irq_tx_enable(uart_dev);
	return 0;
}

int clp_uart_send_can_rx(const struct clp_can_frame *f)
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

int clp_uart_send_tx_ack(const struct clp_tx_ack *ack)
{
	uint8_t wire[CLP_MAX_WIRE];
	int n = clp_encode_tx_ack(tx_seq++, ack, wire, sizeof(wire));

	if (n < 0) {
		return n;
	}
	return tx_push(wire, (size_t)n);
}

int clp_uart_send_status(const struct clp_status *st)
{
	uint8_t wire[CLP_MAX_WIRE];
	int n = clp_encode_status(tx_seq++, st, wire, sizeof(wire));

	if (n < 0) {
		return n;
	}
	return tx_push(wire, (size_t)n);
}

void clp_uart_send_hello(void)
{
	uint8_t wire[CLP_MAX_WIRE];
	int n = clp_encode_hello(tx_seq++, CLP_UART_FW_VERSION,
				 wire, sizeof(wire));

	if (n > 0) {
		(void)tx_push(wire, (size_t)n);
	}
}

/* -------------------------------------------------------------------------- */
/* Inbound - CLP frame dispatch (rx_thread context)                           */
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
/* UART ISR - minimal: shuttle bytes between the FIFO and the rings           */
/* -------------------------------------------------------------------------- */

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (true) {
		uart_irq_update(dev);
		if (uart_irq_is_pending(dev) <= 0) {
			break;
		}

		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[ISR_CHUNK];
			int n = uart_fifo_read(dev, buf, sizeof(buf));

			if (n > 0) {
				k_spinlock_key_t key = k_spin_lock(&rx_lock);

				/* Drop on overflow - inbound is low volume; a
				 * lost CAN_TX command is not fatal.
				 */
				(void)ring_buf_put(&rx_rb, buf, (uint32_t)n);
				k_spin_unlock(&rx_lock, key);
				k_sem_give(&rx_sem);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[ISR_CHUNK];
			k_spinlock_key_t key = k_spin_lock(&tx_lock);
			uint32_t n = ring_buf_get(&tx_rb, buf, sizeof(buf));

			k_spin_unlock(&tx_lock, key);

			if (n == 0) {
				uart_irq_tx_disable(dev);
			} else {
				int sent = uart_fifo_fill(dev, buf, (int)n);

				if (sent < (int)n) {
					/* Return the tail for the next IRQ. */
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
/* RX worker thread - run the CLP parser off the ISR                          */
/* -------------------------------------------------------------------------- */

static void rx_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		uint8_t buf[ISR_CHUNK];
		uint32_t n;

		k_sem_take(&rx_sem, K_FOREVER);

		do {
			k_spinlock_key_t key = k_spin_lock(&rx_lock);

			n = ring_buf_get(&rx_rb, buf, sizeof(buf));
			k_spin_unlock(&rx_lock, key);

			if (n > 0) {
				(void)clp_parser_feed(&parser, buf, n);
			}
		} while (n > 0);
	}
}

static K_THREAD_STACK_DEFINE(rx_thread_stack, 1536);
static struct k_thread rx_thread_data;

/* -------------------------------------------------------------------------- */
/* Public                                                                     */
/* -------------------------------------------------------------------------- */

void clp_uart_set_tx_handler(clp_uart_tx_request_cb_t cb, void *user_data)
{
	tx_req_cb = cb;
	tx_req_cb_data = user_data;
}

void clp_uart_get_counters(uint32_t *tx_frames, uint32_t *tx_drops,
			   uint32_t *crc_errors, uint32_t *framing_errors)
{
	if (tx_frames != NULL) {
		*tx_frames = (uint32_t)atomic_get(&stat_tx_frames);
	}
	if (tx_drops != NULL) {
		*tx_drops = (uint32_t)atomic_get(&stat_tx_drops);
	}
	if (crc_errors != NULL) {
		*crc_errors = parser.crc_errors;
	}
	if (framing_errors != NULL) {
		*framing_errors = parser.framing_errors;
	}
}

int clp_uart_init(void)
{
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("console UART not ready");
		return -ENODEV;
	}

	clp_parser_init(&parser, on_clp_frame, NULL);

	uart_irq_rx_disable(uart_dev);
	uart_irq_tx_disable(uart_dev);
	uart_irq_callback_set(uart_dev, uart_isr);

	k_thread_create(&rx_thread_data, rx_thread_stack,
			K_THREAD_STACK_SIZEOF(rx_thread_stack),
			rx_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&rx_thread_data, "clp_uart_rx");

	uart_irq_rx_enable(uart_dev);

	clp_uart_send_hello();

	LOG_INF("CLP-over-UART up on %s", uart_dev->name);
	return 0;
}
