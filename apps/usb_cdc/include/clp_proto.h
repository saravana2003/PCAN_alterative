/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2C - CLP: "CAN Log Protocol" v1.
 *
 * A small binary framing protocol for the USB CDC-ACM link between the board
 * and the host GUI. Design follows common industrial serial-link practice
 * (SLCAN / Vector BLF / MAVLink-style telemetry / nanopb+COBS links):
 *
 *   - Self-synchronising framing: each logical frame is COBS-encoded
 *     (Consistent Overhead Byte Stuffing, RFC-less but ubiquitous) and
 *     terminated by a single 0x00 byte. COBS guarantees the encoded frame
 *     contains no 0x00, so 0x00 is an unambiguous, instant resync point after
 *     any corruption. A bare magic byte cannot do this (0xA5 occurs naturally
 *     in CAN id/data/timestamp bytes). Zephyr ships COBS: CONFIG_COBS,
 *     <zephyr/data/cobs.h> streaming API.
 *   - Strong integrity: CRC-16/X-25 (poly 0x1021, seed 0xFFFF, reflected,
 *     xorout 0xFFFF) over the logical frame - same family as MAVLink / HDLC.
 *     A sum/XOR checksum misses most multi-bit and transposition errors.
 *     Zephyr: crc16_ccitt() in <zephyr/sys/crc.h>.
 *   - Versioned, typed header so the bidirectional link can carry live frames,
 *     TX commands, status and a connect handshake, and evolve later.
 *   - Explicit length + variable payload: only can_dlc_to_bytes(dlc) data
 *     bytes are put on the wire, not a fixed 64-byte array.
 *   - Per-direction rolling seq so the GUI can detect and count lost frames.
 *
 * Logical frame (little-endian), BEFORE COBS:
 *
 *   +------+------+------+---------+-----------------+-------------+
 *   | ver  | type | seq  |  len    |  payload[len]   |  crc16      |
 *   | u8=1 | u8   | u8   | u16 LE  |                 |  u16 LE     |
 *   +------+------+------+---------+-----------------+-------------+
 *   \_______________ crc covers these ______________/
 *
 * On the wire: COBS(logical frame) followed by one 0x00 delimiter.
 *
 * This header/module is deliberately independent of the USB stack and of the
 * Phase 2A CAN module - it only deals in bytes and plain structs, so it is
 * unit-testable on its own (see main.c's round-trip self-test).
 */

#ifndef USB_CDC_CLP_PROTO_H_
#define USB_CDC_CLP_PROTO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/data/cobs.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLP_VERSION        0x01U

/* Header: ver(1) type(1) seq(1) len(2). Trailer: crc16(2). */
#define CLP_HEADER_SIZE    5U
#define CLP_CRC_SIZE       2U

/*
 * Largest payload we define is a CAN-FD frame message: 16 fixed bytes +
 * up to 64 data bytes (see struct layout in clp_encode_can_frame()).
 */
#define CLP_MAX_PAYLOAD    80U
#define CLP_MAX_FRAME      (CLP_HEADER_SIZE + CLP_MAX_PAYLOAD + CLP_CRC_SIZE)

/* Worst-case COBS expansion (+1 per 254 bytes, +1 code, +1 delimiter). */
#define CLP_MAX_WIRE       (CLP_MAX_FRAME + (CLP_MAX_FRAME / 254U) + 2U)

/** Message types. */
enum clp_msg_type {
	CLP_MSG_CAN_RX     = 0x01, /* device -> host: a received CAN(-FD) frame   */
	CLP_MSG_CAN_TX     = 0x02, /* host -> device: request to transmit a frame */
	CLP_MSG_CAN_TX_ACK = 0x03, /* device -> host: completion for a CAN_TX     */
	CLP_MSG_STATUS     = 0x04, /* device -> host: bus state + counters        */
	CLP_MSG_HELLO      = 0x05, /* device -> host: sent on connect (DTR)       */
};

/*
 * CAN-frame flags. Bit positions match the meaning of Zephyr's CAN_FRAME_*
 * flags so a value maps 1:1 onto the Phase 2A canlog_frame.flags field.
 */
#define CLP_CANF_FDF  BIT(0) /* CAN-FD format frame          */
#define CLP_CANF_BRS  BIT(1) /* bit-rate switch (FD)         */
#define CLP_CANF_ESI  BIT(2) /* error state indicator (FD)   */
#define CLP_CANF_IDE  BIT(3) /* extended 29-bit identifier   */
#define CLP_CANF_RTR  BIT(4) /* remote transmission request  */

/**
 * @brief Decoded CAN frame carried by CLP_MSG_CAN_RX and CLP_MSG_CAN_TX.
 *
 * Field semantics deliberately mirror the Phase 2A struct canlog_frame.
 */
struct clp_can_frame {
	uint32_t can_id;      /* 11- or 29-bit id (no flag bits packed in)      */
	uint8_t  flags;       /* CLP_CANF_*                                     */
	uint8_t  dlc;         /* raw DLC; byte count = can_dlc_to_bytes(dlc)    */
	uint16_t tag;         /* CAN_TX: host correlation id echoed in the ACK; */
			      /* CAN_RX: 0                                      */
	uint64_t timestamp;   /* CAN_RX: k_uptime_ticks() at RX; CAN_TX: 0      */
	uint8_t  data[64];    /* first can_dlc_to_bytes(dlc) bytes are valid    */
};

/** CLP_MSG_CAN_TX_ACK payload (decoded). */
struct clp_tx_ack {
	uint16_t tag;         /* echoes the CAN_TX tag           */
	int16_t  status;      /* 0 = queued/sent OK, else -errno */
	uint64_t timestamp;   /* k_uptime_ticks() at TX callback */
};

/** CLP_MSG_STATUS payload (decoded). */
struct clp_status {
	uint8_t  bus_state;   /* enum can_state value                  */
	uint32_t rx_frames;   /* CAN frames handed to the link         */
	uint32_t tx_frames;   /* CAN_TX requests accepted              */
	uint32_t rx_drops;    /* frames dropped: host not draining USB */
	uint8_t  tx_err_cnt;  /* controller TX error counter           */
	uint8_t  rx_err_cnt;  /* controller RX error counter           */
};

/* -------------------------------------------------------------------------- */
/* Encoding - build a COBS-framed wire buffer (incl. trailing 0x00).          */
/* Each returns the wire length written to @p out, or negative errno.         */
/* -------------------------------------------------------------------------- */

int clp_encode_can_frame(enum clp_msg_type type, uint8_t seq,
			 const struct clp_can_frame *f,
			 uint8_t *out, size_t out_size);

int clp_encode_tx_ack(uint8_t seq, const struct clp_tx_ack *ack,
		      uint8_t *out, size_t out_size);

int clp_encode_status(uint8_t seq, const struct clp_status *st,
		      uint8_t *out, size_t out_size);

int clp_encode_hello(uint8_t seq, const char *fw_version,
		     uint8_t *out, size_t out_size);

/* -------------------------------------------------------------------------- */
/* Decoding helpers - turn a validated payload into a typed struct.           */
/* -------------------------------------------------------------------------- */

int clp_decode_can_frame(const uint8_t *payload, uint16_t len,
			 struct clp_can_frame *out);
int clp_decode_tx_ack(const uint8_t *payload, uint16_t len,
		      struct clp_tx_ack *out);
int clp_decode_status(const uint8_t *payload, uint16_t len,
		      struct clp_status *out);

/* -------------------------------------------------------------------------- */
/* Streaming parser - feed received bytes, get whole CRC-checked frames.      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Called for every complete, length- and CRC-valid CLP frame.
 *
 * @param type     message type (enum clp_msg_type)
 * @param seq      sender's rolling sequence number
 * @param payload  pointer into the parser's frame buffer (valid for the call)
 * @param len      payload length
 * @param user_data pointer given to clp_parser_init()
 */
typedef void (*clp_frame_cb_t)(uint8_t type, uint8_t seq,
			       const uint8_t *payload, uint16_t len,
			       void *user_data);

struct clp_parser {
	struct cobs_decoder dec;
	clp_frame_cb_t cb;
	void *user_data;
	uint8_t frame[CLP_MAX_FRAME];
	size_t frame_len;
	bool overflow;         /* current frame exceeded CLP_MAX_FRAME */
	uint32_t crc_errors;   /* frames dropped: bad CRC              */
	uint32_t framing_errors; /* frames dropped: bad COBS/len       */
	uint32_t frames_ok;
};

void clp_parser_init(struct clp_parser *p, clp_frame_cb_t cb, void *user_data);

/**
 * @brief Feed received bytes into the parser.
 *
 * Complete frames are delivered via the callback registered at init.
 * @return 0 on success, negative errno on an unexpected internal error.
 */
int clp_parser_feed(struct clp_parser *p, const uint8_t *buf, size_t len);

/** CRC-16/X-25 over @p buf (poly 0x1021, seed 0xFFFF, reflected, xorout 0xFFFF). */
uint16_t clp_crc16(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* USB_CDC_CLP_PROTO_H_ */
