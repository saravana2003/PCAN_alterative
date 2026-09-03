/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * can_logger — integrated entry point.
 *
 * Wires the Phase 2A CAN-FD module (can_iface.c) to the Phase 2C CLP protocol
 * (clp_proto.c) over a UART transport (clp_uart.c):
 *
 *     canfd0 RX ISR ──▶ on_can_rx() ──▶ k_msgq ──▶ pump_thread ──▶ CLP_MSG_CAN_RX ──▶ COM12
 *     COM12 ──▶ CLP_MSG_CAN_TX ──▶ on_tx_request() ──▶ can_iface_send() ──▶ canfd0 TX
 *                                                  └─▶ CLP_MSG_CAN_TX_ACK ──▶ COM12
 *     pump_thread also emits CLP_MSG_STATUS ~1 Hz.
 *
 * The RX ISR does only a k_msgq_put so interrupt latency stays low; all CLP
 * encoding happens in pump_thread / rx_thread context.
 *
 * "Interrupt + NVIC" report deliverable: the RX path is canfd0 RX vector
 * (NVIC IRQ 45, prio 12) -> can_iface_rx_isr -> on_can_rx. See can_iface.c.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "can_iface.h"
#include "clp_uart.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Boot self-transmit: a bench convenience that proves the TX path on power-up.
 * OFF by default: a logger should not inject traffic onto a bus that may not
 * be live yet. With no peer ACKing, the RA controller auto-retransmits (no
 * CAN_MODE_ONE_SHOT on this driver) and one frame becomes a sustained error
 * storm -> BUS_OFF. Set to 1 only for bench TX testing with a peer present.
 */
#define BOOT_TX_SELFTEST 0
/* If BOOT_TX_SELFTEST: CLASSIC frame only (the bench PCAN-USB is classic-only;
 * an FD/BRS frame there storms even harder). Flip to 1 with FD-capable gear.
 */
#define BOOT_TX_USE_FD 0

/* CAN RX ISR -> pump_thread hand-off. 64 * sizeof(canlog_frame) ~= 5 KiB. */
K_MSGQ_DEFINE(can_rx_msgq, sizeof(struct canlog_frame), 64, 4);

static atomic_t stat_rx_msgq_drops;   /* CAN faster than the pump could drain */
static atomic_t stat_tx_requests;     /* CLP_MSG_CAN_TX accepted from the host */

/* -------------------------------------------------------------------------- */
/* Flag translation: Zephyr CAN_FRAME_* <-> CLP_CANF_*                         */
/*                                                                            */
/* clp_proto.h's comment claims the bit positions line up 1:1 with Zephyr's,  */
/* but they do NOT (CAN_FRAME_IDE=BIT(0) vs CLP_CANF_IDE=BIT(3), etc.). The    */
/* usb_cdc self-test never noticed because it only round-trips CLP<->CLP. An   */
/* explicit map is the integration point where that matters.                  */
/* -------------------------------------------------------------------------- */

static uint8_t clp_flags_from_can(uint8_t canf)
{
	uint8_t o = 0U;

	if (canf & CAN_FRAME_FDF) {
		o |= CLP_CANF_FDF;
	}
	if (canf & CAN_FRAME_BRS) {
		o |= CLP_CANF_BRS;
	}
	if (canf & CAN_FRAME_ESI) {
		o |= CLP_CANF_ESI;
	}
	if (canf & CAN_FRAME_IDE) {
		o |= CLP_CANF_IDE;
	}
	if (canf & CAN_FRAME_RTR) {
		o |= CLP_CANF_RTR;
	}
	return o;
}

static uint8_t can_flags_from_clp(uint8_t clpf)
{
	uint8_t o = 0U;

	if (clpf & CLP_CANF_FDF) {
		o |= CAN_FRAME_FDF;
	}
	if (clpf & CLP_CANF_BRS) {
		o |= CAN_FRAME_BRS;
	}
	if (clpf & CLP_CANF_ESI) {
		o |= CAN_FRAME_ESI;
	}
	if (clpf & CLP_CANF_IDE) {
		o |= CAN_FRAME_IDE;
	}
	if (clpf & CLP_CANF_RTR) {
		o |= CAN_FRAME_RTR;
	}
	return o;
}

/* -------------------------------------------------------------------------- */
/* CAN RX  (ISR context - keep it to the k_msgq_put)                          */
/* -------------------------------------------------------------------------- */

static void on_can_rx(const struct canlog_frame *frame, void *user_data)
{
	ARG_UNUSED(user_data);

	if (k_msgq_put(&can_rx_msgq, frame, K_NO_WAIT) != 0) {
		atomic_inc(&stat_rx_msgq_drops);
	}
}

/* -------------------------------------------------------------------------- */
/* CLP_MSG_CAN_TX from the host  (rx_thread context)                          */
/* -------------------------------------------------------------------------- */

static void on_tx_request(const struct clp_can_frame *req, void *user_data)
{
	ARG_UNUSED(user_data);

	struct canlog_frame tx = {
		.id = req->can_id,
		.dlc = req->dlc,
		.flags = can_flags_from_clp(req->flags),
	};
	const uint8_t n = can_dlc_to_bytes(req->dlc);
	int ret;

	memcpy(tx.data, req->data, MIN(n, (uint8_t)sizeof(tx.data)));

	ret = can_iface_send(&tx, K_MSEC(50));
	if (ret == 0) {
		atomic_inc(&stat_tx_requests);
	} else {
		LOG_WRN("host CAN_TX id=0x%x -> can_iface_send %d", req->can_id, ret);
	}

	struct clp_tx_ack ack = {
		.tag = req->tag,
		.status = (int16_t)ret,
		.timestamp = (uint64_t)k_uptime_ticks(),
	};
	(void)clp_uart_send_tx_ack(&ack);
}

/* -------------------------------------------------------------------------- */
/* Pump thread: msgq -> CLP_MSG_CAN_RX, plus ~1 Hz CLP_MSG_STATUS             */
/* -------------------------------------------------------------------------- */

static void send_status(void)
{
	enum can_state bus_state = CAN_STATE_ERROR_ACTIVE;
	struct can_bus_err_cnt err = { 0 };
	uint32_t tx_frames = 0, tx_drops = 0, crc_err = 0, frm_err = 0;

	(void)can_iface_state(&bus_state, &err);
	clp_uart_get_counters(&tx_frames, &tx_drops, &crc_err, &frm_err);

	struct clp_status st = {
		.bus_state = (uint8_t)bus_state,
		.rx_frames = tx_frames,
		.tx_frames = (uint32_t)atomic_get(&stat_tx_requests),
		.rx_drops = tx_drops + (uint32_t)atomic_get(&stat_rx_msgq_drops),
		.tx_err_cnt = err.tx_err_cnt,
		.rx_err_cnt = err.rx_err_cnt,
	};

	(void)clp_uart_send_status(&st);
}

static void pump_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t last_status = k_uptime_get();
	unsigned int status_ticks = 0;

	for (;;) {
		struct canlog_frame f;

		if (k_msgq_get(&can_rx_msgq, &f, K_MSEC(500)) == 0) {
			struct clp_can_frame cf = {
				.can_id = f.id,
				.flags = clp_flags_from_can(f.flags),
				.dlc = f.dlc,
				.tag = 0U,
				.timestamp = f.timestamp,
			};

			memcpy(cf.data, f.data, sizeof(cf.data));
			(void)clp_uart_send_can_rx(&cf);
		}

		if (k_uptime_get() - last_status >= 1000) {
			last_status = k_uptime_get();
			send_status();
			/* Re-announce every 5 s so a host attaching mid-run
			 * still learns the fw version (HELLO is otherwise
			 * boot-only).
			 */
			if (++status_ticks % 5U == 0U) {
				clp_uart_send_hello();
			}
		}
	}
}

static K_THREAD_STACK_DEFINE(pump_stack, 2048);
static struct k_thread pump_data;

/* -------------------------------------------------------------------------- */

int main(void)
{
	int ret;

	ret = can_iface_init();
	if (ret != 0) {
		LOG_ERR("can_iface_init failed: %d", ret);
		return 0;
	}
	can_iface_set_rx_handler(on_can_rx, NULL);

	ret = clp_uart_init();
	if (ret != 0) {
		LOG_ERR("clp_uart_init failed: %d", ret);
		return 0;
	}
	clp_uart_set_tx_handler(on_tx_request, NULL);

	k_thread_create(&pump_data, pump_stack, K_THREAD_STACK_SIZEOF(pump_stack),
			pump_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&pump_data, "clp_pump");

#if BOOT_TX_SELFTEST
	/* Bench-only: prove the TX path on power-up. Needs a peer ACKing. */
	struct canlog_frame boot_tx = {
		.id = 0x123,
		.dlc = 8,
#if BOOT_TX_USE_FD
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
#else
		.flags = 0,
#endif
	};

	for (uint8_t i = 0; i < 8; i++) {
		boot_tx.data[i] = i;
	}

	ret = can_iface_send(&boot_tx, K_MSEC(100));
	if (ret != 0) {
		LOG_WRN("boot self-TX returned %d (no bus / no ACK?)", ret);
	}
#endif

	LOG_INF("can_logger integrated: CAN-FD <-> CLP on the console UART");
	return 0;
}
