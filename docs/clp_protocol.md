# CLP — CAN Log Protocol v1

The binary framing protocol on the USB CDC-ACM link between the EK-RA8D1 board
and the host GUI. Implemented in `apps/usb_cdc/` (Phase 2C).

The transport (USB CDC-ACM) is a raw byte stream — USB packet boundaries are
**not** message boundaries and must not be relied on. CLP layers its own
framing, integrity and typing on top, following common practice for industrial
serial links (SLCAN, Vector BLF, MAVLink-style telemetry, nanopb+COBS).

## 1. Framing

Every logical frame is **COBS-encoded** (Consistent Overhead Byte Stuffing) and
followed by a single **`0x00`** delimiter.

- COBS guarantees the encoded bytes contain no `0x00`, so `0x00` is an
  unambiguous frame boundary and an instant resync point after any corruption.
- A bare "magic byte" cannot do this: e.g. `0xA5` occurs naturally in CAN
  identifiers, data and timestamps, so a receiver could false-lock for a long
  time after one dropped byte.
- Overhead: 1 byte per frame + 1 byte per 254 bytes of payload + the delimiter.

Reference implementations: Zephyr `<zephyr/data/cobs.h>` (`CONFIG_COBS`);
countless host-side libraries (`cobs` on PyPI, etc.).

## 2. Logical frame (before COBS)

All multi-byte fields are **little-endian** (both ends are LE; zero-cost).

| Offset | Field   | Type   | Notes                                             |
|-------:|---------|--------|--------------------------------------------------|
| 0      | `ver`   | u8     | protocol version, `0x01`                          |
| 1      | `type`  | u8     | message type (see §4)                             |
| 2      | `seq`   | u8     | sender's rolling sequence number, per direction  |
| 3      | `len`   | u16    | payload length in bytes (0 … 80)                  |
| 5      | payload | `len` B| type-specific (see §4)                            |
| 5+len  | `crc`   | u16    | CRC-16/X-25 over bytes `[0 .. 5+len-1]`           |

Total logical size = `5 + len + 2`. Max (`len` = 80) = 87 bytes → ≤ 90 on the
wire after COBS + delimiter.

### CRC-16/X-25

Polynomial `0x1021`, init `0xFFFF`, input reflected, output reflected, xor-out
`0xFFFF` (a.k.a. CRC-16/IBM-SDLC / CRC-16/ISO-HDLC — the MAVLink / HDLC family).
Zephyr: `crc16_ccitt(0xFFFF, buf, len) ^ 0xFFFF`.
Python: `crccheck.crc.Crc16X25` or a 16-entry table.

A sum/XOR checksum is **not** used — it misses most multi-bit and byte-
transposition errors.

## 3. Sequence numbers & loss detection

`seq` increments by 1 (mod 256) for every frame a side sends, independently per
direction. A gap in the received `seq` sequence means one or more frames were
lost (e.g. the device dropped frames because the host was not draining USB — see
`STATUS.rx_drops`). CLP does not retransmit; live CAN logging favours freshness
over completeness.

## 4. Message types

| Value | Name           | Dir    | Payload |
|------:|----------------|--------|---------|
| 0x01  | `CAN_RX`       | dev→host | CAN frame (§4.1) |
| 0x02  | `CAN_TX`       | host→dev | CAN frame (§4.1) |
| 0x03  | `CAN_TX_ACK`   | dev→host | TX ack (§4.2) |
| 0x04  | `STATUS`       | dev→host | status (§4.3) |
| 0x05  | `HELLO`        | dev→host | hello (§4.4) |

Unknown `type` values MUST be ignored (forward compatibility).

### 4.1 CAN frame payload (`CAN_RX`, `CAN_TX`)

| Offset | Field       | Type | Notes |
|-------:|-------------|------|-------|
| 0      | `can_id`    | u32  | 11-bit or 29-bit id; **no** flag bits packed in |
| 4      | `flags`     | u8   | bit0 `FDF`, bit1 `BRS`, bit2 `ESI`, bit3 `IDE` (extended), bit4 `RTR` |
| 5      | `dlc`       | u8   | raw DLC 0…15; data byte count = CAN-FD DLC table |
| 6      | `tag`       | u16  | `CAN_TX`: host correlation id, echoed in `CAN_TX_ACK`. `CAN_RX`: 0 |
| 8      | `timestamp` | u64  | `CAN_RX`: `k_uptime_ticks()` sampled in the RX ISR. `CAN_TX`: 0 |
| 16     | `data`      | N B  | N = `dlc_to_bytes(dlc)`, or 0 if `RTR` set |

`dlc_to_bytes`: 0-8 → 0-8, 9→12, 10→16, 11→20, 12→24, 13→32, 14→48, 15→64.

`flags` bit meanings match Zephyr's `CAN_FRAME_*` so the field maps 1:1 onto the
Phase 2A `struct canlog_frame.flags`.

Payload length: `16 + N` (23 for a classic 0-byte frame, 24 for RTR, 31 for an
8-byte frame, 80 for FD-64). Only the valid data bytes are transmitted — never a
fixed 64-byte array.

### 4.2 `CAN_TX_ACK` payload (12 bytes)

| Offset | Field       | Type | Notes |
|-------:|-------------|------|-------|
| 0      | `tag`       | u16  | echoes the `CAN_TX` `tag` |
| 2      | `status`    | i16  | 0 = queued/sent OK, else negative errno |
| 4      | `timestamp` | u64  | `k_uptime_ticks()` at the TX-done callback |

### 4.3 `STATUS` payload (15 bytes)

| Offset | Field        | Type | Notes |
|-------:|--------------|------|-------|
| 0      | `bus_state`  | u8   | Zephyr `enum can_state` |
| 1      | `rx_frames`  | u32  | CAN frames handed to the link |
| 5      | `tx_frames`  | u32  | `CAN_TX` requests accepted |
| 9      | `rx_drops`   | u32  | frames dropped: host not draining USB |
| 13     | `tx_err_cnt` | u8   | controller TX error counter |
| 14     | `rx_err_cnt` | u8   | controller RX error counter |

### 4.4 `HELLO` payload (3 + fw-string bytes)

Sent by the device when the host opens the port (DTR asserted). The GUI should
reset its parser and check `proto_ver` on receipt.

| Offset | Field         | Type | Notes |
|-------:|---------------|------|-------|
| 0      | `proto_ver`   | u8   | `0x01` |
| 1      | `max_payload` | u16  | largest payload the device will send (80) |
| 3      | `fw_version`  | ASCII| not NUL-terminated; length = `len - 3` |

## 5. Receiver algorithm

1. Accumulate bytes until `0x00`.
2. COBS-decode the bytes before the `0x00`.
3. Reject if decoded length < 7, or `ver != 0x01`, or
   `5 + len + 2 != decoded_length`.
4. Verify `crc`. On mismatch, drop and count.
5. Dispatch on `type`; ignore unknown types.
6. On any COBS/length/CRC error, discard and resume at the next `0x00`. COBS
   guarantees resync within at most one following frame.

## 6. Notes for the GUI implementer

- Open the CDC-ACM port, assert DTR, wait for `HELLO`.
- The device never blocks CAN RX on USB back-pressure: if the GUI stops reading,
  frames are dropped and counted in `STATUS.rx_drops` / the `seq` gap. Keep a
  reader thread draining the port.
- `CAN_TX`: set a unique `tag`, expect a `CAN_TX_ACK` with the same `tag`.
- Device VID/PID are currently the Zephyr test values `0x2FE3:0x0001`
  (placeholder — see `apps/usb_cdc/src/usbd_ctx.c`).
