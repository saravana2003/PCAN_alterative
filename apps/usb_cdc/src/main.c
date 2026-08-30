/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2C demo entry point (build-only, no board).
 *
 * 1. Runs a pure CLP encode -> parse round-trip self-test that needs no USB
 *    and no CAN hardware, exercising clp_proto.c end to end (framing, CRC,
 *    COBS, resync after a corrupted byte).
 * 2. Brings up the USB CDC-ACM link and shows the wiring points: a
 *    CLP_MSG_CAN_TX handler (host -> device) and a CLP_MSG_CAN_RX send
 *    (device -> host). Nothing enumerates without a host; there is no
 *    flashing in this phase.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "clp_proto.h"
#include "usb_link.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ---- self-test scaffolding ---------------------------------------------- */

static struct clp_can_frame st_last;
static uint8_t st_last_type;
static int st_count;

static void st_frame_cb(uint8_t type, uint8_t seq, const uint8_t *payload,
			uint16_t len, void *user_data)
{
	ARG_UNUSED(seq);
	ARG_UNUSED(user_data);

	st_last_type = type;
	st_count++;
	if (type == CLP_MSG_CAN_RX || type == CLP_MSG_CAN_TX) {
		(void)clp_decode_can_frame(payload, len, &st_last);
	}
}

static bool clp_selftest(void)
{
	struct clp_parser p;
	struct clp_can_frame in = {
		.can_id = 0x1ABCDEF0,
		.flags = CLP_CANF_FDF | CLP_CANF_BRS | CLP_CANF_IDE,
		.dlc = 15,            /* 64 data bytes */
		.tag = 0,
		.timestamp = 0x0011223344556677ULL,
	};
	uint8_t wire[CLP_MAX_WIRE];
	int n;

	for (int i = 0; i < 64; i++) {
		in.data[i] = (uint8_t)(i ^ 0xA5); /* deliberately include 0xA5 */
	}

	clp_parser_init(&p, st_frame_cb, NULL);

	/* Happy path. */
	n = clp_encode_can_frame(CLP_MSG_CAN_RX, 7, &in, wire, sizeof(wire));
	if (n <= 0) {
		LOG_ERR("selftest: encode failed %d", n);
		return false;
	}
	LOG_INF("selftest: FD-64 frame -> %d wire bytes (fixed struct would be 80)", n);

	st_count = 0;
	clp_parser_feed(&p, wire, (size_t)n);
	if (st_count != 1 || st_last_type != CLP_MSG_CAN_RX ||
	    st_last.can_id != in.can_id || st_last.dlc != in.dlc ||
	    st_last.timestamp != in.timestamp ||
	    memcmp(st_last.data, in.data, 64) != 0) {
		LOG_ERR("selftest: round-trip mismatch (count %d)", st_count);
		return false;
	}

	/* Corruption + resync: flip a byte mid-frame, then send a good frame. */
	uint8_t bad[CLP_MAX_WIRE];

	memcpy(bad, wire, (size_t)n);
	bad[10] ^= 0xFF;

	st_count = 0;
	clp_parser_feed(&p, bad, (size_t)n);          /* should be rejected */
	n = clp_encode_can_frame(CLP_MSG_CAN_RX, 8, &in, wire, sizeof(wire));
	clp_parser_feed(&p, wire, (size_t)n);         /* must still parse   */

	if (st_count != 1) {
		LOG_ERR("selftest: resync failed (delivered %d)", st_count);
		return false;
	}
	if (p.crc_errors + p.framing_errors == 0) {
		LOG_ERR("selftest: corrupted frame was not counted");
		return false;
	}

	LOG_INF("selftest PASS (crc_err=%u framing_err=%u frames_ok=%u)",
		p.crc_errors, p.framing_errors, p.frames_ok);
	return true;
}

/* ---- USB link demo ----------------------------------------------------- */

static void on_tx_request(const struct clp_can_frame *req, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("host asked to TX: id=0x%08x dlc=%u flags=0x%02x tag=%u",
		req->can_id, req->dlc, req->flags, req->tag);

	/* When wired to Phase 2A: can_iface_send(req) then usb_link_send_tx_ack(). */
}

int main(void)
{
	uint32_t txf, drops, crce, frme;
	int ret;

	if (!clp_selftest()) {
		LOG_ERR("CLP self-test FAILED");
		return 0;
	}

	ret = usb_link_init();
	if (ret != 0) {
		LOG_ERR("usb_link_init failed: %d", ret);
		return 0;
	}

	usb_link_set_tx_handler(on_tx_request, NULL);

	/* Try one live-frame send. Off-bench this returns -ENOTCONN (no host). */
	struct clp_can_frame live = {
		.can_id = 0x123,
		.flags = CLP_CANF_FDF | CLP_CANF_BRS,
		.dlc = 8,
		.timestamp = k_uptime_ticks(),
	};

	for (int i = 0; i < 8; i++) {
		live.data[i] = (uint8_t)i;
	}

	ret = usb_link_send_can_rx(&live);
	LOG_INF("usb_link_send_can_rx -> %d (%s)", ret,
		ret == -ENOTCONN ? "no host, expected off-bench" : "queued");

	usb_link_get_counters(&txf, &drops, &crce, &frme);
	LOG_INF("counters: tx_frames=%u rx_drops=%u crc_err=%u framing_err=%u",
		txf, drops, crce, frme);

	return 0;
}
