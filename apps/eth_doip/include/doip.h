/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2E - DoIP (Diagnostics over IP, ISO 13400) message skeleton for
 * retrieving CAN logs from the board over Ethernet.
 *
 * Scope (per PROMPTS.md Phase 2E): a *skeleton*, not a full ISO 13400 stack -
 *   - respond to a Vehicle Identification Request with a Vehicle
 *     Announcement / Identification Response,
 *   - accept a Routing Activation Request and answer with a Routing
 *     Activation Response,
 *   - accept a Diagnostic Message carrying a placeholder UDS "upload" request
 *     (RequestUpload / TransferData / RequestTransferExit) and answer it.
 *
 * This file is transport-independent: it parses and builds byte buffers only,
 * so it is unit-testable without a network (see main.c's self-test). The
 * socket plumbing lives in doip_server.c.
 *
 * DoIP message = 8-byte header + payload:
 *   off 0 : protocol version        u8   (0x02 = ISO 13400-2:2012)
 *   off 1 : inverse protocol version u8  (~version)
 *   off 2 : payload type            u16  big-endian
 *   off 4 : payload length          u32  big-endian
 *   off 8 : payload                 (payload length bytes)
 */

#ifndef ETH_DOIP_DOIP_H_
#define ETH_DOIP_DOIP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOIP_PROTOCOL_VERSION      0x02U   /* ISO 13400-2:2012 */
#define DOIP_INVERSE_VERSION       0xFDU
#define DOIP_PORT                  13400
#define DOIP_HEADER_SIZE           8U

/* Payload types (ISO 13400-2). */
#define DOIP_PT_GENERIC_NACK               0x0000U
#define DOIP_PT_VEHICLE_ID_REQ             0x0001U
#define DOIP_PT_VEHICLE_ID_REQ_EID         0x0002U
#define DOIP_PT_VEHICLE_ID_REQ_VIN         0x0003U
#define DOIP_PT_VEHICLE_ANNOUNCEMENT       0x0004U /* == vehicle id response */
#define DOIP_PT_ROUTING_ACTIVATION_REQ     0x0005U
#define DOIP_PT_ROUTING_ACTIVATION_RESP    0x0006U
#define DOIP_PT_ALIVE_CHECK_REQ            0x0007U
#define DOIP_PT_ALIVE_CHECK_RESP           0x0008U
#define DOIP_PT_DIAG_MESSAGE              0x8001U
#define DOIP_PT_DIAG_MESSAGE_POS_ACK      0x8002U
#define DOIP_PT_DIAG_MESSAGE_NEG_ACK      0x8003U

/* Generic header NACK codes. */
#define DOIP_NACK_INCORRECT_PATTERN        0x00U
#define DOIP_NACK_UNKNOWN_PAYLOAD_TYPE     0x01U
#define DOIP_NACK_MESSAGE_TOO_LARGE        0x02U
#define DOIP_NACK_INVALID_LENGTH           0x04U

/* Routing activation response codes. */
#define DOIP_RA_CODE_SUCCESS               0x10U
#define DOIP_RA_CODE_UNKNOWN_SA            0x00U
#define DOIP_RA_CODE_UNSUPPORTED_TYPE      0x06U

/* Diagnostic message positive/negative ack codes. */
#define DOIP_DIAG_ACK_OK                   0x00U
#define DOIP_DIAG_NACK_UNKNOWN_TA          0x03U

/*
 * Our identity. These are placeholders for the project - a real deployment
 * assigns addresses per the vehicle's DoIP address plan.
 */
#define DOIP_ENTITY_LOGICAL_ADDR   0x1234U  /* this logger, as a DoIP entity   */
#define DOIP_TESTER_ADDR_MIN       0x0E00U  /* accepted external tester range  */
#define DOIP_TESTER_ADDR_MAX       0x0FFFU

/* UDS service IDs used by the placeholder log-download flow. */
#define UDS_SID_READ_DATA_BY_ID    0x22U
#define UDS_SID_REQUEST_UPLOAD     0x35U
#define UDS_SID_TRANSFER_DATA      0x36U
#define UDS_SID_REQUEST_XFER_EXIT  0x37U
#define UDS_POSITIVE_RESPONSE_MASK 0x40U
#define UDS_NRC_SERVICE_NOT_SUPPORTED   0x11U
#define UDS_NRC_REQUEST_OUT_OF_RANGE    0x31U

/** Per-TCP-connection DoIP state. */
struct doip_conn {
	bool     routing_active;
	uint16_t tester_addr;
	uint8_t  block_seq;   /* TransferData block sequence counter */
};

static inline void doip_conn_init(struct doip_conn *c)
{
	c->routing_active = false;
	c->tester_addr = 0;
	c->block_seq = 0;
}

/**
 * @brief Parse and validate a DoIP header.
 *
 * @param buf        message bytes
 * @param len        bytes available
 * @param type[out]  payload type
 * @param plen[out]  payload length from the header
 * @retval 0         header OK (does not check that @p len covers the payload)
 * @retval -EBADMSG  bad protocol version / inverse version
 * @retval -EINVAL   fewer than 8 bytes
 */
int doip_parse_header(const uint8_t *buf, size_t len, uint16_t *type, uint32_t *plen);

/**
 * @brief Handle one DoIP message received over UDP (discovery).
 *
 * Answers Vehicle Identification Requests with a Vehicle Announcement.
 *
 * @return response length written to @p resp, 0 if nothing to send, or <0.
 */
int doip_handle_udp(const uint8_t *msg, size_t len, uint8_t *resp, size_t resp_size);

/**
 * @brief Handle one complete DoIP message received over a TCP connection.
 *
 * Routing activation, alive-check and diagnostic-message (UDS) dispatch.
 * Updates @p conn.
 *
 * @return response length written to @p resp, 0 if nothing to send, or <0.
 */
int doip_handle_tcp(struct doip_conn *conn, const uint8_t *msg, size_t len,
		    uint8_t *resp, size_t resp_size);

#ifdef __cplusplus
}
#endif

#endif /* ETH_DOIP_DOIP_H_ */
