#!/usr/bin/env python3
"""
clp_client.py - host-side CLP (CAN Log Protocol) client for the EK-RA8D1
can_logger firmware, speaking over the J-Link VCOM serial port (COM12).

Protocol: docs/clp_protocol.md.
  wire  = COBS(logical) + 0x00
  logical = ver(1)=1  type(1)  seq(1)  len(u16 LE)  payload[len]  crc16(u16 LE)
  crc   = CRC-16/X-25 (poly 0x1021, init 0xFFFF, refin/refout, xorout 0xFFFF)

Usage:
  python clp_client.py COM12                 # listen + pretty-print frames
  python clp_client.py COM12 --baud 115200
  python clp_client.py COM12 --tx 0x123:0011223344556677      # send one CAN_TX
  python clp_client.py COM12 --tx 0x18DAF110:DEADBEEF --ext    # extended id
  python clp_client.py COM12 --raw           # also hexdump every decoded frame

Needs pyserial:  pip install pyserial   (already in the project .venv)
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial")

# ---------------------------------------------------------------------------
# CLP constants
# ---------------------------------------------------------------------------
CLP_VER = 0x01
MSG = {0x01: "CAN_RX", 0x02: "CAN_TX", 0x03: "CAN_TX_ACK", 0x04: "STATUS", 0x05: "HELLO"}
CANF = [("FDF", 1 << 0), ("BRS", 1 << 1), ("ESI", 1 << 2), ("IDE", 1 << 3), ("RTR", 1 << 4)]
DLC2LEN = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]
CAN_STATE = {0: "ERROR_ACTIVE", 1: "ERROR_WARNING", 2: "ERROR_PASSIVE",
             3: "BUS_OFF", 4: "STOPPED"}


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


def flags_str(f: int) -> str:
    s = "|".join(name for name, bit in CANF if f & bit)
    return s or "-"


# ---------------------------------------------------------------------------
# decode
# ---------------------------------------------------------------------------
def parse_logical(buf: bytes, raw: bool):
    if len(buf) < 7:
        print(f"  ! short frame ({len(buf)} B)")
        return
    ver, typ, seq = buf[0], buf[1], buf[2]
    plen = buf[3] | (buf[4] << 8)
    if ver != CLP_VER:
        print(f"  ! bad ver 0x{ver:02x}")
        return
    if 5 + plen + 2 != len(buf):
        print(f"  ! len mismatch: hdr says {plen}, frame is {len(buf)} B")
        return
    crc_rx = buf[5 + plen] | (buf[6 + plen] << 8)
    crc_calc = crc16_x25(buf[:5 + plen])
    if crc_rx != crc_calc:
        print(f"  ! CRC 0x{crc_rx:04x} != 0x{crc_calc:04x}")
        return
    payload = buf[5:5 + plen]
    name = MSG.get(typ, f"0x{typ:02x}")
    if raw:
        print(f"  [{name} seq={seq}] {payload.hex()}")

    if typ in (0x01, 0x02):
        _print_can(name, seq, payload)
    elif typ == 0x03:
        tag = payload[0] | (payload[1] << 8)
        status = int.from_bytes(payload[2:4], "little", signed=True)
        ts = int.from_bytes(payload[4:12], "little")
        print(f"  CAN_TX_ACK seq={seq} tag=0x{tag:04x} status={status} ts={ts}")
    elif typ == 0x04:
        st = payload[0]
        rx = int.from_bytes(payload[1:5], "little")
        tx = int.from_bytes(payload[5:9], "little")
        drops = int.from_bytes(payload[9:13], "little")
        print(f"  STATUS seq={seq} bus={CAN_STATE.get(st, st)} "
              f"rx_frames={rx} tx_frames={tx} rx_drops={drops} "
              f"tx_err={payload[13]} rx_err={payload[14]}")
    elif typ == 0x05:
        pv = payload[0]
        maxp = payload[1] | (payload[2] << 8)
        fw = payload[3:].decode("ascii", "replace")
        print(f"  HELLO seq={seq} proto_ver={pv} max_payload={maxp} fw={fw!r}")
    else:
        print(f"  {name} seq={seq} len={plen}")


def _print_can(name, seq, p):
    can_id = int.from_bytes(p[0:4], "little")
    flags, dlc = p[4], p[5]
    tag = p[6] | (p[7] << 8)
    ts = int.from_bytes(p[8:16], "little")
    data = p[16:]
    nbytes = 0 if flags & (1 << 4) else DLC2LEN[dlc & 0xF]
    idw = 8 if flags & (1 << 3) else 3
    print(f"  {name} seq={seq} id=0x{can_id:0{idw}X} dlc={dlc} "
          f"flags={flags_str(flags)} tag=0x{tag:04x} ts={ts} "
          f"data={data[:nbytes].hex()}")


# ---------------------------------------------------------------------------
# encode a CAN_TX
# ---------------------------------------------------------------------------
def build_can_tx(seq: int, can_id: int, data: bytes, ext: bool, fd: bool, brs: bool) -> bytes:
    flags = 0
    if fd:
        flags |= 1 << 0
    if brs:
        flags |= 1 << 1
    if ext:
        flags |= 1 << 3
    # smallest dlc that covers len(data)
    dlc = next(i for i, ln in enumerate(DLC2LEN) if ln >= len(data))
    payload = bytearray(16 + DLC2LEN[dlc])
    payload[0:4] = can_id.to_bytes(4, "little")
    payload[4] = flags
    payload[5] = dlc
    payload[6:8] = (0xBEEF).to_bytes(2, "little")   # tag, echoed in the ACK
    payload[8:16] = (0).to_bytes(8, "little")
    payload[16:16 + len(data)] = data
    logical = bytearray([CLP_VER, 0x02, seq & 0xFF])
    logical += len(payload).to_bytes(2, "little")
    logical += payload
    logical += crc16_x25(bytes(logical)).to_bytes(2, "little")
    return cobs_encode(bytes(logical)) + b"\x00"


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tx", help="ID:HEXDATA, e.g. 0x123:0011223344556677")
    ap.add_argument("--ext", action="store_true", help="extended (29-bit) id for --tx")
    ap.add_argument("--fd", action="store_true", help="FD frame for --tx")
    ap.add_argument("--brs", action="store_true", help="bit-rate switch for --tx")
    ap.add_argument("--raw", action="store_true", help="hexdump every decoded frame")
    ap.add_argument("--seconds", type=float, default=0, help="exit after N s (0 = forever)")
    args = ap.parse_args()

    s = serial.Serial(args.port, args.baud, timeout=0.2)
    print(f"# open {args.port} @ {args.baud}")

    if args.tx:
        id_s, _, data_s = args.tx.partition(":")
        can_id = int(id_s, 0)
        data = bytes.fromhex(data_s) if data_s else b""
        frame = build_can_tx(1, can_id, data, args.ext, args.fd, args.brs)
        s.write(frame)
        s.flush()
        print(f"# sent CAN_TX id=0x{can_id:X} data={data.hex()} ({len(frame)} wire B)")

    buf = bytearray()
    t0 = time.time()
    try:
        while True:
            chunk = s.read(256)
            if chunk:
                buf += chunk
                while b"\x00" in buf:
                    seg, _, buf = buf.partition(b"\x00")
                    if not seg:
                        continue
                    stamp = time.strftime("%H:%M:%S")
                    try:
                        logical = cobs_decode(bytes(seg))
                    except ValueError as e:
                        print(f"{stamp} ! COBS: {e} ({len(seg)} B)")
                        continue
                    print(f"{stamp}", end="")
                    parse_logical(logical, args.raw)
            if args.seconds and (time.time() - t0) >= args.seconds:
                break
    except KeyboardInterrupt:
        pass
    finally:
        s.close()


if __name__ == "__main__":
    main()
