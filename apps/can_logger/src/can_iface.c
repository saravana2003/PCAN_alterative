/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2A — CAN-FD interface module for canfd0 (build-only).
 *
 * All Zephyr CAN API used here was verified against
 * zephyr/include/zephyr/drivers/can.h and the controller driver
 * drivers/can/can_renesas_ra.c in this workspace (not from memory):
 *
 *   struct can_frame  { uint32_t id; uint8_t dlc; uint8_t flags;
 *                       uint16_t timestamp/reserved; uint8_t data[CAN_MAX_DLEN]; ... }
 *   struct can_filter { uint32_t id; uint32_t mask; uint8_t flags; }   // flags: only CAN_FILTER_IDE
 *   int  can_set_mode(const struct device *, can_mode_t);
 *   int  can_start(const struct device *);
 *   int  can_send(const struct device *, const struct can_frame *,
 *                 k_timeout_t, can_tx_callback_t, void *user_data);
 *   int  can_add_rx_filter(const struct device *, can_rx_callback_t,
 *                          void *user_data, const struct can_filter *);   // returns filter_id
 *   int  can_get_capabilities(const struct device *, can_mode_t *);
 *   int  can_get_core_clock(const struct device *, uint32_t *);
 *   int  can_get_state(const struct device *, enum can_state *, struct can_bus_err_cnt *);
 *   typedef void (*can_rx_callback_t)(const struct device *, struct can_frame *, void *);
 *   typedef void (*can_tx_callback_t)(const struct device *, int error, void *);
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

#include "can_iface.h"

LOG_MODULE_REGISTER(can_iface, LOG_LEVEL_INF);

/*
 * canfd0 is the board's chosen CAN bus:
 *   zephyr,canbus = &canfd0;   (zephyr/boards/renesas/ek_ra8d1/ek_ra8d1.dts)
 * Channel 0, compatible "renesas,ra-canfd". Its interrupts (from
 * zephyr/dts/arm/renesas/ra/ra8/ra8x1.dtsi) are:
 *   err = 43, tx = 44, rx = 45   (all at priority 12)
 */
#define CANFD0_NODE DT_CHOSEN(zephyr_canbus)
BUILD_ASSERT(DT_NODE_HAS_STATUS(CANFD0_NODE, okay),
	     "chosen zephyr,canbus (canfd0) is not enabled");

static const struct device *const canfd0 = DEVICE_DT_GET(CANFD0_NODE);

/* ---------------------------------------------------------------------------
 * "Interrupt + NVIC" deliverable (university report)
 * ---------------------------------------------------------------------------
 * The Renesas RA CAN-FD driver connects the channel-0 RX FIFO vector with:
 *
 *   IRQ_CONNECT(DT_INST_IRQ_BY_NAME(index, rx, irq),          // == 45 for canfd0
 *               DT_INST_IRQ_BY_NAME(index, rx, priority),      // == 12
 *               canfd_common_fifo_rx_isr, NULL, 0);
 *   irq_enable(DT_INST_IRQ_BY_NAME(index, rx, irq));           // enable NVIC line 45
 *
 * (drivers/can/can_renesas_ra.c, CAN_RENESAS_RA_INIT()).
 *
 * On a received frame the Cortex-M85 NVIC takes exception for IRQ 45,
 * canfd_common_fifo_rx_isr() runs, the FSP posts CAN_EVENT_RX_COMPLETE, and
 * the driver calls the callback we registered with can_add_rx_filter() -
 * can_iface_rx_isr() below - still in interrupt context. That callback is
 * this project's interrupt-service path for CAN reception.
 * ------------------------------------------------------------------------- */

static canlog_rx_handler_t rx_handler;
static void *rx_handler_user_data;
static int rx_filter_ids[3] = { -1, -1, -1 };

/* RX interrupt callback (ISR context - keep it short, no blocking calls). */
static void can_iface_rx_isr(const struct device *dev, struct can_frame *frame, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	canlog_rx_handler_t handler = rx_handler;

	if (handler == NULL) {
		return;
	}

	struct canlog_frame out = {
		.id = frame->id,
		.dlc = frame->dlc,
		.flags = frame->flags,
		/* Software timestamp: the renesas,ra-canfd driver does not
		 * implement CONFIG_CAN_RX_TIMESTAMP, so frame->timestamp is
		 * meaningless. k_uptime_ticks() is ISR-safe.
		 */
		.timestamp = (uint64_t)k_uptime_ticks(),
	};

	const uint8_t len = can_dlc_to_bytes(frame->dlc);

	memcpy(out.data, frame->data, MIN(len, (uint8_t)sizeof(out.data)));

	handler(&out, rx_handler_user_data);
}

/* TX completion callback (ISR context). Non-blocking send path. */
static void can_iface_tx_done(const struct device *dev, int error, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (error != 0) {
		LOG_WRN("CAN TX completion error: %d", error);
	}
}

int can_iface_set_rx_handler(canlog_rx_handler_t handler, void *user_data)
{
	/* Pointer-sized stores; the RX ISR reads a local snapshot of
	 * rx_handler first. Set this before traffic starts.
	 */
	rx_handler_user_data = user_data;
	rx_handler = handler;
	return 0;
}

int can_iface_send(const struct canlog_frame *frame, k_timeout_t timeout)
{
	if (frame == NULL) {
		return -EINVAL;
	}

	struct can_frame tx = {
		.id = frame->id,
		.dlc = frame->dlc,
		.flags = frame->flags,
	};

	const uint8_t len = can_dlc_to_bytes(frame->dlc);

	memcpy(tx.data, frame->data, MIN(len, (uint8_t)sizeof(tx.data)));

	/* Non-blocking: a non-NULL callback makes can_send() return as soon as
	 * the frame is queued (zephyr/include/zephyr/drivers/can.h).
	 */
	return can_send(canfd0, &tx, timeout, can_iface_tx_done, NULL);
}

int can_iface_state(enum can_state *state, struct can_bus_err_cnt *err_cnt)
{
	return can_get_state(canfd0, state, err_cnt);
}

int can_iface_init(void)
{
	int ret;
	uint32_t core_clock = 0;
	can_mode_t caps = 0;

	if (!device_is_ready(canfd0)) {
		LOG_ERR("canfd0 device not ready");
		return -ENODEV;
	}

	ret = can_get_capabilities(canfd0, &caps);
	if (ret != 0) {
		LOG_ERR("can_get_capabilities: %d", ret);
		return ret;
	}
	if ((caps & CAN_MODE_FD) == 0) {
		LOG_ERR("canfd0 does not advertise CAN_MODE_FD (caps=0x%08x)", caps);
		return -ENOTSUP;
	}

	/* CAN-FD, normal (not loopback/listen-only). Bitrates come from the
	 * devicetree (bitrate / bitrate-data in boards/ek_ra8d1.overlay), so
	 * no can_set_bitrate() call is needed here.
	 */
	ret = can_set_mode(canfd0, CAN_MODE_FD);
	if (ret != 0) {
		LOG_ERR("can_set_mode(CAN_MODE_FD): %d", ret);
		return ret;
	}

	/*
	 * RX filters. In the Zephyr CAN API a filter is {id, mask, flags} and
	 * the only filter flag is CAN_FILTER_IDE (standard vs extended ID) -
	 * "CAN-FD" is a per-frame property (CAN_FRAME_FDF), not a filter
	 * criterion. So FD frames are delivered through whichever of these
	 * filters matches, once the controller is in CAN_MODE_FD.
	 *
	 * The renesas,ra-canfd driver evaluates filters in registration order
	 * and stops at the first match (drivers/can/can_renesas_ra.c:
	 * can_renesas_ra_call_rx_cb), so the specific filter is added before
	 * the catch-alls.
	 */
	static const struct can_filter filters[3] = {
		/* [0] targeted example: standard IDs 0x100..0x1FF (diagnostic
		 *     range) - also catches FD frames in that range.
		 */
		{ .id = 0x100, .mask = 0x700, .flags = 0 },
		/* [1] catch-all standard (11-bit) IDs, classic + FD. */
		{ .id = 0, .mask = 0, .flags = 0 },
		/* [2] catch-all extended (29-bit) IDs, classic + FD. */
		{ .id = 0, .mask = 0, .flags = CAN_FILTER_IDE },
	};

	for (int i = 0; i < (int)ARRAY_SIZE(filters); i++) {
		ret = can_add_rx_filter(canfd0, can_iface_rx_isr, NULL, &filters[i]);
		if (ret < 0) {
			LOG_ERR("can_add_rx_filter[%d]: %d", i, ret);
			return ret;
		}
		rx_filter_ids[i] = ret;
	}

	ret = can_start(canfd0);
	if (ret != 0) {
		LOG_ERR("can_start: %d", ret);
		return ret;
	}

	if (can_get_core_clock(canfd0, &core_clock) == 0) {
		LOG_INF("canfd0 started: CAN-FD, core clock %u Hz, RX IRQ 45 (NVIC)",
			core_clock);
	} else {
		LOG_INF("canfd0 started: CAN-FD, RX IRQ 45 (NVIC)");
	}

	return 0;
}
