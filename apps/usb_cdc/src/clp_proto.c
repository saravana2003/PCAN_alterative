/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2C - CLP v1 encode / decode / streaming parser. Build-only.
 *
 * No dependency on the USB stack or the Phase 2A CAN driver: this file only
 * manipulates byte buffers and plain structs, so main.c can exercise it with a
 * pure encode -> parse round trip that needs no hardware.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include "clp_proto.h"

/* Payload sizes (fixed part). */
#define CLP_CANF_FIXED   16U  /* can_id(4) flags(1) dlc(1) tag(2) timestamp(8) */
#define CLP_TX_ACK_SIZE  12U  /* tag(2) status(2) timestamp(8)                 */
#define CLP_STATUS_SIZE  15U  /* state(1) rx(4) tx(4) drops(4) txerr(1) rxerr(1) */
#define CLP_HELLO_FIXED   3U  /* proto_ver(1) max_payload(2) + fw string        */

uint16_t clp_crc16(const uint8_t *buf, size_t len)
{
	/* CRC-16/X-25: poly 0x1021, seed 0xFFFF, reflect in/out, xorout 0xFFFF. */
	return crc16_ccitt(0xFFFFU, buf, len) ^ 0xFFFFU;
}

/*
 * CAN-FD Data Length Code -> payload byte count. Local copy of the standard
 * table so this module needs no <zephyr/drivers/can.h> / CONFIG_CAN.
 */
static uint8_t clp_dlc_to_bytes(uint8_t dlc)
{
	static const uint8_t tbl[16] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64,
	};

	return tbl[dlc & 0x0FU];
}

/* -------------------------------------------------------------------------- */
/* COBS output sink: append encoded bytes into a fixed caller buffer.         */
/* -------------------------------------------------------------------------- */
struct cobs_sink {
	uint8_t *out;
	size_t size;
	size_t pos;
};

static int cobs_sink_cb(const uint8_t *buf, size_t len, void *user_data)
{
	struct cobs_sink *s = user_data;

	if (s->pos + len > s->size) {
		return -ENOMEM;
	}
	memcpy(&s->out[s->pos], buf, len);
	s->pos += len;
	return 0;
}

/*
 * Build header + payload + CRC, then COBS-frame it (with trailing 0x00) into
 * @p out. Returns wire length or negative errno.
 */
static int clp_encode_raw(enum clp_msg_type type, uint8_t seq,
			  const uint8_t *payload, uint16_t plen,
			  uint8_t *out, size_t out_size)
{
	uint8_t logical[CLP_MAX_FRAME];
	struct cobs_sink sink = { .out = out, .size = out_size, .pos = 0 };
	struct cobs_encoder enc;
	uint16_t crc;
	size_t total;
	int ret;

	if (plen > CLP_MAX_PAYLOAD) {
		return -EINVAL;
	}

	logical[0] = CLP_VERSION;
	logical[1] = (uint8_t)type;
	logical[2] = seq;
	sys_put_le16(plen, &logical[3]);
	if (plen > 0U) {
		memcpy(&logical[CLP_HEADER_SIZE], payload, plen);
	}
	crc = clp_crc16(logical, CLP_HEADER_SIZE + plen);
	sys_put_le16(crc, &logical[CLP_HEADER_SIZE + plen]);
	total = CLP_HEADER_SIZE + plen + CLP_CRC_SIZE;

	ret = cobs_encoder_init(&enc, cobs_sink_cb, &sink,
				COBS_FLAG_TRAILING_DELIMITER);
	if (ret < 0) {
		return ret;
	}
	ret = cobs_encoder_write(&enc, logical, total);
	if (ret < 0) {
		return ret;
	}
	ret = cobs_encoder_close(&enc);
	if (ret < 0) {
		return ret;
	}

	return (int)sink.pos;
}

int clp_encode_can_frame(enum clp_msg_type type, uint8_t seq,
			 const struct clp_can_frame *f,
			 uint8_t *out, size_t out_size)
{
	uint8_t payload[CLP_MAX_PAYLOAD];
	uint8_t n;

	if (type != CLP_MSG_CAN_RX && type != CLP_MSG_CAN_TX) {
		return -EINVAL;
	}

	n = clp_dlc_to_bytes(f->dlc);
	if ((f->flags & CLP_CANF_RTR) != 0U) {
		n = 0U; /* remote frame carries no data */
	}

	sys_put_le32(f->can_id, &payload[0]);
	payload[4] = f->flags;
	payload[5] = f->dlc;
	sys_put_le16(f->tag, &payload[6]);
	sys_put_le64(f->timestamp, &payload[8]);
	memcpy(&payload[CLP_CANF_FIXED], f->data, n);

	return clp_encode_raw(type, seq, payload, CLP_CANF_FIXED + n,
			      out, out_size);
}

int clp_encode_tx_ack(uint8_t seq, const struct clp_tx_ack *ack,
		      uint8_t *out, size_t out_size)
{
	uint8_t payload[CLP_TX_ACK_SIZE];

	sys_put_le16(ack->tag, &payload[0]);
	sys_put_le16((uint16_t)ack->status, &payload[2]);
	sys_put_le64(ack->timestamp, &payload[4]);

	return clp_encode_raw(CLP_MSG_CAN_TX_ACK, seq, payload, sizeof(payload),
			      out, out_size);
}

int clp_encode_status(uint8_t seq, const struct clp_status *st,
		      uint8_t *out, size_t out_size)
{
	uint8_t payload[CLP_STATUS_SIZE];

	payload[0] = st->bus_state;
	sys_put_le32(st->rx_frames, &payload[1]);
	sys_put_le32(st->tx_frames, &payload[5]);
	sys_put_le32(st->rx_drops, &payload[9]);
	payload[13] = st->tx_err_cnt;
	payload[14] = st->rx_err_cnt;

	return clp_encode_raw(CLP_MSG_STATUS, seq, payload, sizeof(payload),
			      out, out_size);
}

int clp_encode_hello(uint8_t seq, const char *fw_version,
		     uint8_t *out, size_t out_size)
{
	uint8_t payload[CLP_MAX_PAYLOAD];
	size_t fw_len = (fw_version != NULL) ? strlen(fw_version) : 0U;

	if (fw_len > (CLP_MAX_PAYLOAD - CLP_HELLO_FIXED)) {
		fw_len = CLP_MAX_PAYLOAD - CLP_HELLO_FIXED;
	}

	payload[0] = CLP_VERSION;
	sys_put_le16(CLP_MAX_PAYLOAD, &payload[1]);
	if (fw_len > 0U) {
		memcpy(&payload[CLP_HELLO_FIXED], fw_version, fw_len);
	}

	return clp_encode_raw(CLP_MSG_HELLO, seq, payload,
			      (uint16_t)(CLP_HELLO_FIXED + fw_len),
			      out, out_size);
}

/* -------------------------------------------------------------------------- */
/* Decoding helpers                                                           */
/* -------------------------------------------------------------------------- */

int clp_decode_can_frame(const uint8_t *payload, uint16_t len,
			 struct clp_can_frame *out)
{
	uint8_t n;

	if (len < CLP_CANF_FIXED) {
		return -EBADMSG;
	}

	memset(out, 0, sizeof(*out));
	out->can_id = sys_get_le32(&payload[0]);
	out->flags = payload[4];
	out->dlc = payload[5];
	out->tag = sys_get_le16(&payload[6]);
	out->timestamp = sys_get_le64(&payload[8]);

	n = clp_dlc_to_bytes(out->dlc);
	if ((out->flags & CLP_CANF_RTR) != 0U) {
		n = 0U;
	}
	if (len != CLP_CANF_FIXED + n) {
		return -EBADMSG;
	}
	memcpy(out->data, &payload[CLP_CANF_FIXED], n);
	return 0;
}

int clp_decode_tx_ack(const uint8_t *payload, uint16_t len,
		      struct clp_tx_ack *out)
{
	if (len != CLP_TX_ACK_SIZE) {
		return -EBADMSG;
	}
	out->tag = sys_get_le16(&payload[0]);
	out->status = (int16_t)sys_get_le16(&payload[2]);
	out->timestamp = sys_get_le64(&payload[4]);
	return 0;
}

int clp_decode_status(const uint8_t *payload, uint16_t len,
		      struct clp_status *out)
{
	if (len != CLP_STATUS_SIZE) {
		return -EBADMSG;
	}
	out->bus_state = payload[0];
	out->rx_frames = sys_get_le32(&payload[1]);
	out->tx_frames = sys_get_le32(&payload[5]);
	out->rx_drops = sys_get_le32(&payload[9]);
	out->tx_err_cnt = payload[13];
	out->rx_err_cnt = payload[14];
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Streaming parser                                                           */
/* -------------------------------------------------------------------------- */

static void clp_parser_dispatch(struct clp_parser *p)
{
	uint16_t plen;
	uint16_t crc_rx, crc_calc;

	if (p->frame_len == 0U) {
		/* Stray / back-to-back delimiter - not a frame, ignore. */
		return;
	}
	if (p->overflow || p->frame_len < (CLP_HEADER_SIZE + CLP_CRC_SIZE)) {
		p->framing_errors++;
		return;
	}

	plen = sys_get_le16(&p->frame[3]);
	if ((size_t)CLP_HEADER_SIZE + plen + CLP_CRC_SIZE != p->frame_len) {
		p->framing_errors++;
		return;
	}
	if (p->frame[0] != CLP_VERSION) {
		p->framing_errors++;
		return;
	}

	crc_rx = sys_get_le16(&p->frame[CLP_HEADER_SIZE + plen]);
	crc_calc = clp_crc16(p->frame, CLP_HEADER_SIZE + plen);
	if (crc_rx != crc_calc) {
		p->crc_errors++;
		return;
	}

	p->frames_ok++;
	if (p->cb != NULL) {
		p->cb(p->frame[1], p->frame[2], &p->frame[CLP_HEADER_SIZE],
		      plen, p->user_data);
	}
}

/* COBS decoder output sink: accumulate one logical frame, flush on delimiter. */
static int clp_cobs_cb(const uint8_t *buf, size_t len, void *user_data)
{
	struct clp_parser *p = user_data;

	if (buf == NULL) {
		/* Frame delimiter reached. */
		clp_parser_dispatch(p);
		p->frame_len = 0;
		p->overflow = false;
		return 0;
	}

	if (p->frame_len + len > sizeof(p->frame)) {
		/* Too big for any CLP message - keep consuming, drop at end. */
		p->overflow = true;
		return 0;
	}
	memcpy(&p->frame[p->frame_len], buf, len);
	p->frame_len += len;
	return 0;
}

void clp_parser_init(struct clp_parser *p, clp_frame_cb_t cb, void *user_data)
{
	memset(p, 0, sizeof(*p));
	p->cb = cb;
	p->user_data = user_data;
	(void)cobs_decoder_init(&p->dec, clp_cobs_cb, p,
				COBS_FLAG_TRAILING_DELIMITER);
}

int clp_parser_feed(struct clp_parser *p, const uint8_t *buf, size_t len)
{
	/*
	 * Feed one byte at a time: on a COBS structure violation the decoder
	 * resets itself and returns < 0; byte granularity lets us drop exactly
	 * the corrupt run and resync on the very next 0x00 delimiter without
	 * losing the frames that follow it. CDC-ACM data rates make the
	 * per-byte call cost irrelevant.
	 */
	for (size_t i = 0; i < len; i++) {
		int ret = cobs_decoder_write(&p->dec, &buf[i], 1);

		if (ret < 0) {
			p->frame_len = 0;
			p->overflow = false;
			p->framing_errors++;
			/* decoder already reset by cobs_decoder_write() */
		}
	}
	return 0;
}
