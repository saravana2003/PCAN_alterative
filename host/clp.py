"""
clp.py - CLP (CAN Log Protocol) v1 codec, shared by clp_client.py and clp_gui.py.

Protocol: docs/clp_protocol.md.
  wire    = COBS(logical) + 0x00
  logical = ver(1)=1  type(1)  seq(1)  len(u16 LE)  payload[len]  crc16(u16 LE)
  crc     = CRC-16/X-25 (poly 0x1021, init 0xFFFF, refin/refout, xorout 0xFFFF)

Pure Python, no third-party deps. Firmware side: apps/can_logger (clp_uart.c +
shared apps/usb_cdc/src/clp_proto.c).
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# constants
# ---------------------------------------------------------------------------
CLP_VER = 0x01

MSG_CAN_RX = 0x01
MSG_CAN_TX = 0x02
MSG_CAN_TX_ACK = 0x03
MSG_STATUS = 0x04
MSG_HELLO = 0x05
MSG_NAME = {
    MSG_CAN_RX: "CAN_RX", MSG_CAN_TX: "CAN_TX", MSG_CAN_TX_ACK: "CAN_TX_ACK",
    MSG_STATUS: "STATUS", MSG_HELLO: "HELLO",
}

# CLP wire flag bits (NOT the same numbering as Zephyr's CAN_FRAME_*)
CANF_FDF = 1 << 0
CANF_BRS = 1 << 1
CANF_ESI = 1 << 2
CANF_IDE = 1 << 3
CANF_RTR = 1 << 4
CANF_ORDER = [("FDF", CANF_FDF), ("BRS", CANF_BRS), ("ESI", CANF_ESI),
              ("IDE", CANF_IDE), ("RTR", CANF_RTR)]

DLC2LEN = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]

CAN_STATE = {0: "ERROR_ACTIVE", 1: "ERROR_WARNING", 2: "ERROR_PASSIVE",
             3: "BUS_OFF", 4: "STOPPED"}


def flags_str(f: int) -> str:
    return "|".join(name for name, bit in CANF_ORDER if f & bit) or "-"


def dlc_for(nbytes: int) -> int:
    """Smallest DLC whose byte count covers nbytes."""
    return next(i for i, ln in enumerate(DLC2LEN) if ln >= nbytes)


# ---------------------------------------------------------------------------
# CRC-16/X-25 and COBS
# ---------------------------------------------------------------------------
def crc16_x25(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if (crc & 1) else crc >> 1
    return crc ^ 0xFFFF


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        code = data[i]
        i += 1
        if code == 0:
            raise ValueError("zero code byte in COBS block")
        for _ in range(code - 1):
            if i >= n:
                raise ValueError("COBS block runs past end")
            out.append(data[i])
            i += 1
        if code < 0xFF and i < n:
            out.append(0)
    return bytes(out)


def cobs_encode(data: bytes) -> bytes:
    out = bytearray([0])
    code_idx, code = 0, 1
    for b in data:
        if b != 0:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[code_idx] = code
                code_idx, code = len(out), 1
                out.append(0)
        else:
            out[code_idx] = code
            code_idx, code = len(out), 1
            out.append(0)
    out[code_idx] = code
    return bytes(out)


# ---------------------------------------------------------------------------
# decoded message types
# ---------------------------------------------------------------------------
@dataclass
class CanFrame:
    can_id: int
    flags: int
    dlc: int
    tag: int
    timestamp: int
    data: bytes

    @property
    def ext(self) -> bool:
        return bool(self.flags & CANF_IDE)

    @property
    def nbytes(self) -> int:
        return 0 if self.flags & CANF_RTR else DLC2LEN[self.dlc & 0xF]

    def id_str(self) -> str:
        return f"{self.can_id:08X}" if self.ext else f"{self.can_id:03X}"


@dataclass
class TxAck:
    tag: int
    status: int
    timestamp: int


@dataclass
class Status:
    bus_state: int
    rx_frames: int
    tx_frames: int
    rx_drops: int
    tx_err_cnt: int
    rx_err_cnt: int

    def bus_str(self) -> str:
        return CAN_STATE.get(self.bus_state, str(self.bus_state))


@dataclass
class Hello:
    proto_ver: int
    max_payload: int
    fw_version: str


@dataclass
class Message:
    type: int
    seq: int
    payload: bytes
    obj: object = None          # CanFrame / TxAck / Status / Hello / None
    t_host: float = field(default_factory=time.time)

    @property
    def name(self) -> str:
        return MSG_NAME.get(self.type, f"0x{self.type:02x}")


# ---------------------------------------------------------------------------
# payload parsing
# ---------------------------------------------------------------------------
def _decode_can(p: bytes) -> CanFrame:
    can_id = int.from_bytes(p[0:4], "little")
    flags, dlc = p[4], p[5]
    tag = int.from_bytes(p[6:8], "little")
    ts = int.from_bytes(p[8:16], "little")
    return CanFrame(can_id, flags, dlc, tag, ts, bytes(p[16:]))


def _decode_obj(typ: int, payload: bytes):
    try:
        if typ in (MSG_CAN_RX, MSG_CAN_TX):
            return _decode_can(payload)
        if typ == MSG_CAN_TX_ACK:
            return TxAck(int.from_bytes(payload[0:2], "little"),
                         int.from_bytes(payload[2:4], "little", signed=True),
                         int.from_bytes(payload[4:12], "little"))
        if typ == MSG_STATUS:
            return Status(payload[0],
                          int.from_bytes(payload[1:5], "little"),
                          int.from_bytes(payload[5:9], "little"),
                          int.from_bytes(payload[9:13], "little"),
                          payload[13], payload[14])
        if typ == MSG_HELLO:
            return Hello(payload[0], int.from_bytes(payload[1:3], "little"),
                         payload[3:].decode("ascii", "replace"))
    except (IndexError, ValueError):
        return None
    return None


# ---------------------------------------------------------------------------
# streaming decoder
# ---------------------------------------------------------------------------
class Decoder:
    """Feed raw bytes, get back a list of Message. Tracks error counts."""

    def __init__(self):
        self._buf = bytearray()
        self.crc_errors = 0
        self.framing_errors = 0
        self.frames_ok = 0

    def feed(self, chunk: bytes) -> list[Message]:
        out: list[Message] = []
        self._buf += chunk
        while b"\x00" in self._buf:
            seg, _, rest = self._buf.partition(b"\x00")
            self._buf = bytearray(rest)
            if not seg:
                continue
            try:
                logical = cobs_decode(bytes(seg))
            except ValueError:
                self.framing_errors += 1
                continue
            msg = self._parse_logical(logical)
            if msg is not None:
                out.append(msg)
        return out

    def _parse_logical(self, buf: bytes):
        if len(buf) < 7 or buf[0] != CLP_VER:
            self.framing_errors += 1
            return None
        plen = buf[3] | (buf[4] << 8)
        if 5 + plen + 2 != len(buf):
            self.framing_errors += 1
            return None
        crc_rx = buf[5 + plen] | (buf[6 + plen] << 8)
        if crc_rx != crc16_x25(buf[:5 + plen]):
            self.crc_errors += 1
            return None
        self.frames_ok += 1
        typ, seq = buf[1], buf[2]
        payload = bytes(buf[5:5 + plen])
        return Message(typ, seq, payload, _decode_obj(typ, payload))


# ---------------------------------------------------------------------------
# encoding a CAN_TX
# ---------------------------------------------------------------------------
def encode_can_tx(seq: int, can_id: int, data: bytes, *, ext=False, fd=False,
                  brs=False, rtr=False, tag=0xBEEF) -> bytes:
    flags = 0
    if fd:
        flags |= CANF_FDF
    if brs:
        flags |= CANF_BRS
    if ext:
        flags |= CANF_IDE
    if rtr:
        flags |= CANF_RTR
    n = 0 if rtr else len(data)
    dlc = dlc_for(n)
    payload = bytearray(16 + DLC2LEN[dlc])
    payload[0:4] = can_id.to_bytes(4, "little")
    payload[4] = flags
    payload[5] = dlc
    payload[6:8] = (tag & 0xFFFF).to_bytes(2, "little")
    payload[8:16] = (0).to_bytes(8, "little")
    payload[16:16 + n] = data[:n]
    logical = bytearray([CLP_VER, MSG_CAN_TX, seq & 0xFF])
    logical += len(payload).to_bytes(2, "little")
    logical += payload
    logical += crc16_x25(bytes(logical)).to_bytes(2, "little")
    return cobs_encode(bytes(logical)) + b"\x00"
