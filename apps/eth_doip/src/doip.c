/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2E - DoIP message skeleton. Build-only; no board, no live network.
 *
 * Transport-independent: every function here takes and returns byte buffers.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include "doip.h"

LOG_MODULE_REGISTER(doip, LOG_LEVEL_INF);

/* Placeholder vehicle identity for the project. */
static const char DOIP_VIN[17] = "EKRA8D1CANLOGGER0";
static const uint8_t DOIP_EID[6] = { 0x74, 0x90, 0x50, 0xB0, 0x5D, 0xE9 }; /* board MAC */
static const uint8_t DOIP_GID[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

/* Our "log store status" diagnostic data identifier. */
#define UDS_DID_VIN            0xF190U
#define UDS_DID_LOG_STATUS     0xFD00U

/* -------------------------------------------------------------------------- */

int doip_parse_header(const uint8_t *buf, size_t len, uint16_t *type, uint32_t *plen)
{
	if (len < DOIP_HEADER_SIZE) {
		return -EINVAL;
	}
	if (buf[0] != DOIP_PROTOCOL_VERSION ||
	    buf[1] != (uint8_t)~DOIP_PROTOCOL_VERSION) {
		return -EBADMSG;
	}
	*type = sys_get_be16(&buf[2]);
	*plen = sys_get_be32(&buf[4]);
	return 0;
}

static int emit(uint8_t *resp, size_t resp_size, uint16_t type,
		const uint8_t *payload, uint32_t plen)
{
	if (resp_size < DOIP_HEADER_SIZE + plen) {
		return -ENOMEM;
	}
	resp[0] = DOIP_PROTOCOL_VERSION;
	resp[1] = (uint8_t)~DOIP_PROTOCOL_VERSION;
	sys_put_be16(type, &resp[2]);
	sys_put_be32(plen, &resp[4]);
	if (plen > 0U) {
		memcpy(&resp[DOIP_HEADER_SIZE], payload, plen);
	}
	return (int)(DOIP_HEADER_SIZE + plen);
}

static int emit_generic_nack(uint8_t *resp, size_t resp_size, uint8_t code)
{
	return emit(resp, resp_size, DOIP_PT_GENERIC_NACK, &code, 1);
}

static int build_vehicle_announcement(uint8_t *resp, size_t resp_size)
{
	uint8_t p[32];

	memcpy(&p[0], DOIP_VIN, 17);
	sys_put_be16(DOIP_ENTITY_LOGICAL_ADDR, &p[17]);
	memcpy(&p[19], DOIP_EID, 6);
	memcpy(&p[25], DOIP_GID, 6);
	p[31] = 0x00; /* further action required: none */

	return emit(resp, resp_size, DOIP_PT_VEHICLE_ANNOUNCEMENT, p, sizeof(p));
}

/* -------------------------------------------------------------------------- */

int doip_handle_udp(const uint8_t *msg, size_t len, uint8_t *resp, size_t resp_size)
{
	uint16_t type;
	uint32_t plen;
	int rc = doip_parse_header(msg, len, &type, &plen);

	if (rc == -EBADMSG) {
		return emit_generic_nack(resp, resp_size, DOIP_NACK_INCORRECT_PATTERN);
	}
	if (rc < 0) {
		return 0;
	}

	switch (type) {
	case DOIP_PT_VEHICLE_ID_REQ:
	case DOIP_PT_VEHICLE_ID_REQ_EID:
	case DOIP_PT_VEHICLE_ID_REQ_VIN:
		LOG_INF("UDP: vehicle identification request -> announcement");
		return build_vehicle_announcement(resp, resp_size);
	default:
		return emit_generic_nack(resp, resp_size,
					 DOIP_NACK_UNKNOWN_PAYLOAD_TYPE);
	}
}

/* -------------------------------------------------------------------------- */
/* UDS placeholder - the "data download" flow for pulling logs off the board. */
/* -------------------------------------------------------------------------- */

static uint16_t uds_nrc(uint8_t sid, uint8_t nrc, uint8_t *out)
{
	out[0] = 0x7F;
	out[1] = sid;
	out[2] = nrc;
	return 3;
}

static uint16_t uds_dispatch(struct doip_conn *conn, const uint8_t *uds, size_t n,
			     uint8_t *out, size_t out_size)
{
	if (n == 0U || out_size < 8U) {
		return 0;
	}

	switch (uds[0]) {
	case UDS_SID_READ_DATA_BY_ID: {
		if (n < 3U) {
			return uds_nrc(uds[0], UDS_NRC_REQUEST_OUT_OF_RANGE, out);
		}
		uint16_t did = sys_get_be16(&uds[1]);

		out[0] = UDS_SID_READ_DATA_BY_ID | UDS_POSITIVE_RESPONSE_MASK;
		sys_put_be16(did, &out[1]);

		if (did == UDS_DID_VIN) {
			memcpy(&out[3], DOIP_VIN, 17);
			return 3 + 17;
		}
		if (did == UDS_DID_LOG_STATUS) {
			/* Placeholder: [num_files u16][total_bytes u32]. A real
			 * build queries the Phase 2D littlefs store.
			 */
			sys_put_be16(0, &out[3]);
			sys_put_be32(0, &out[5]);
			return 3 + 6;
		}
		return uds_nrc(uds[0], UDS_NRC_REQUEST_OUT_OF_RANGE, out);
	}

	case UDS_SID_REQUEST_UPLOAD:
		/* 0x35 + dataFormatId + addrAndLenFormatId + addr + size.
		 * Response: 0x75 + lengthFormatId(0x20) + maxNumberOfBlockLength.
		 */
		out[0] = UDS_SID_REQUEST_UPLOAD | UDS_POSITIVE_RESPONSE_MASK;
		out[1] = 0x20;                    /* 2-byte maxNumberOfBlockLength */
		sys_put_be16(1024, &out[2]);
		conn->block_seq = 0;
		return 4;

	case UDS_SID_TRANSFER_DATA: {
		if (n < 2U) {
			return uds_nrc(uds[0], UDS_NRC_REQUEST_OUT_OF_RANGE, out);
		}
		uint8_t seq = uds[1];

		conn->block_seq = seq;
		out[0] = UDS_SID_TRANSFER_DATA | UDS_POSITIVE_RESPONSE_MASK;
		out[1] = seq;
		/* Placeholder log chunk. A real build returns the next slice of
		 * the requested /lfs/log_*.clb file here.
		 */
		memset(&out[2], 0xAA, 8);
		return 10;
	}

	case UDS_SID_REQUEST_XFER_EXIT:
		out[0] = UDS_SID_REQUEST_XFER_EXIT | UDS_POSITIVE_RESPONSE_MASK;
		return 1;

	default:
		return uds_nrc(uds[0], UDS_NRC_SERVICE_NOT_SUPPORTED, out);
	}
}

/* -------------------------------------------------------------------------- */

static int handle_routing_activation(struct doip_conn *conn, const uint8_t *pl,
				     uint32_t plen, uint8_t *resp, size_t resp_size)
{
	uint8_t rp[9];
	uint16_t sa;
	uint8_t code;

	if (plen < 7U) {
		return emit_generic_nack(resp, resp_size, DOIP_NACK_INVALID_LENGTH);
	}

	sa = sys_get_be16(&pl[0]);
	if (sa >= DOIP_TESTER_ADDR_MIN && sa <= DOIP_TESTER_ADDR_MAX) {
		conn->routing_active = true;
		conn->tester_addr = sa;
		code = DOIP_RA_CODE_SUCCESS;
		LOG_INF("TCP: routing activation from tester 0x%04x -> success", sa);
	} else {
		code = DOIP_RA_CODE_UNKNOWN_SA;
		LOG_WRN("TCP: routing activation from unknown SA 0x%04x", sa);
	}

	sys_put_be16(sa, &rp[0]);                       /* logical addr tester   */
	sys_put_be16(DOIP_ENTITY_LOGICAL_ADDR, &rp[2]); /* logical addr entity   */
	rp[4] = code;
	memset(&rp[5], 0, 4);                           /* reserved              */

	return emit(resp, resp_size, DOIP_PT_ROUTING_ACTIVATION_RESP,
		    rp, sizeof(rp));
}

static int handle_diag_message(struct doip_conn *conn, const uint8_t *pl,
			       uint32_t plen, uint8_t *resp, size_t resp_size)
{
	uint8_t ack[5];
	uint16_t sa, ta;
	int off = 0;
	int rc;

	if (plen < 4U) {
		return emit_generic_nack(resp, resp_size, DOIP_NACK_INVALID_LENGTH);
	}

	sa = sys_get_be16(&pl[0]);
	ta = sys_get_be16(&pl[2]);

	if (!conn->routing_active) {
		sys_put_be16(ta, &ack[0]);
		sys_put_be16(sa, &ack[2]);
		ack[4] = 0x06; /* out of memory / not activated */
		return emit(resp, resp_size, DOIP_PT_DIAG_MESSAGE_NEG_ACK,
			    ack, sizeof(ack));
	}
	if (ta != DOIP_ENTITY_LOGICAL_ADDR) {
		sys_put_be16(ta, &ack[0]);
		sys_put_be16(sa, &ack[2]);
		ack[4] = DOIP_DIAG_NACK_UNKNOWN_TA;
		return emit(resp, resp_size, DOIP_PT_DIAG_MESSAGE_NEG_ACK,
			    ack, sizeof(ack));
	}

	/* 1) Diagnostic message positive acknowledge (SA/TA swapped). */
	sys_put_be16(DOIP_ENTITY_LOGICAL_ADDR, &ack[0]);
	sys_put_be16(sa, &ack[2]);
	ack[4] = DOIP_DIAG_ACK_OK;
	rc = emit(resp, resp_size, DOIP_PT_DIAG_MESSAGE_POS_ACK, ack, sizeof(ack));
	if (rc < 0) {
		return rc;
	}
	off = rc;

	/* 2) The UDS response, carried in its own diagnostic message. */
	{
		uint8_t dm[4 + 64];
		uint16_t uds_len;

		sys_put_be16(DOIP_ENTITY_LOGICAL_ADDR, &dm[0]);
		sys_put_be16(sa, &dm[2]);
		uds_len = uds_dispatch(conn, &pl[4], plen - 4U, &dm[4],
				       sizeof(dm) - 4U);
		if (uds_len == 0U) {
			return off;
		}

		rc = emit(resp + off, resp_size - off, DOIP_PT_DIAG_MESSAGE,
			  dm, 4U + uds_len);
		if (rc < 0) {
			return rc;
		}
		off += rc;
	}

	return off;
}

int doip_handle_tcp(struct doip_conn *conn, const uint8_t *msg, size_t len,
		    uint8_t *resp, size_t resp_size)
{
	uint16_t type;
	uint32_t plen;
	const uint8_t *pl;
	int rc = doip_parse_header(msg, len, &type, &plen);

	if (rc == -EBADMSG) {
		return emit_generic_nack(resp, resp_size, DOIP_NACK_INCORRECT_PATTERN);
	}
	if (rc < 0) {
		return 0;
	}
	if (len < (size_t)DOIP_HEADER_SIZE + plen) {
		return emit_generic_nack(resp, resp_size, DOIP_NACK_INVALID_LENGTH);
	}
	pl = &msg[DOIP_HEADER_SIZE];

	switch (type) {
	case DOIP_PT_ROUTING_ACTIVATION_REQ:
		return handle_routing_activation(conn, pl, plen, resp, resp_size);

	case DOIP_PT_ALIVE_CHECK_REQ: {
		uint8_t ap[2];

		sys_put_be16(DOIP_ENTITY_LOGICAL_ADDR, ap);
		return emit(resp, resp_size, DOIP_PT_ALIVE_CHECK_RESP, ap, sizeof(ap));
	}

	case DOIP_PT_DIAG_MESSAGE:
		return handle_diag_message(conn, pl, plen, resp, resp_size);

	default:
		return emit_generic_nack(resp, resp_size,
					 DOIP_NACK_UNKNOWN_PAYLOAD_TYPE);
	}
}
