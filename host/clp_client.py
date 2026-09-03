#!/usr/bin/env python3
"""
clp_client.py - headless CLP client for the EK-RA8D1 can_logger firmware over
the J-Link VCOM (COM12). Pretty-prints frames; can inject one CAN_TX.

Protocol + codec: clp.py / docs/clp_protocol.md.

Usage:
  python clp_client.py COM12
  python clp_client.py COM12 --baud 115200 --raw
  python clp_client.py COM12 --tx 0x123:0011223344556677
  python clp_client.py COM12 --tx 0x18DAF110:DEADBEEF --ext
  python clp_client.py COM12 --tx 0x7DF: --fd --brs
  python clp_client.py COM12 --seconds 10

Needs pyserial:  pip install pyserial   (already in the project .venv)
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial")

import clp


def show(m: clp.Message, raw: bool):
    stamp = time.strftime("%H:%M:%S")
    head = f"{stamp}  {m.name} seq={m.seq}"
    if raw:
        head += f"  [{m.payload.hex()}]"
    o = m.obj
    if isinstance(o, clp.CanFrame):
        print(f"{head} id=0x{o.id_str()} dlc={o.dlc} flags={clp.flags_str(o.flags)} "
              f"tag=0x{o.tag:04x} ts={o.timestamp} data={o.data[:o.nbytes].hex()}")
    elif isinstance(o, clp.TxAck):
        print(f"{head} tag=0x{o.tag:04x} status={o.status} ts={o.timestamp}")
    elif isinstance(o, clp.Status):
        print(f"{head} bus={o.bus_str()} rx_frames={o.rx_frames} tx_frames={o.tx_frames} "
              f"rx_drops={o.rx_drops} tx_err={o.tx_err_cnt} rx_err={o.rx_err_cnt}")
    elif isinstance(o, clp.Hello):
        print(f"{head} proto_ver={o.proto_ver} max_payload={o.max_payload} fw={o.fw_version!r}")
    else:
        print(f"{head} len={len(m.payload)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tx", help="ID:HEXDATA, e.g. 0x123:0011223344556677")
    ap.add_argument("--ext", action="store_true", help="extended id for --tx")
    ap.add_argument("--fd", action="store_true", help="FD frame for --tx")
    ap.add_argument("--brs", action="store_true", help="bit-rate switch for --tx")
    ap.add_argument("--raw", action="store_true", help="also hexdump each payload")
    ap.add_argument("--seconds", type=float, default=0, help="exit after N s (0=forever)")
    args = ap.parse_args()

    s = serial.Serial(args.port, args.baud, timeout=0.2)
    print(f"# open {args.port} @ {args.baud}")

    if args.tx:
        id_s, _, data_s = args.tx.partition(":")
        frame = clp.encode_can_tx(1, int(id_s, 0), bytes.fromhex(data_s),
                                  ext=args.ext, fd=args.fd, brs=args.brs)
        s.write(frame)
        s.flush()
        print(f"# sent CAN_TX id={id_s} data={data_s} ({len(frame)} wire B)")

    dec = clp.Decoder()
    t0 = time.time()
    try:
        while True:
            chunk = s.read(256)
            for m in dec.feed(chunk):
                show(m, args.raw)
            if args.seconds and (time.time() - t0) >= args.seconds:
                break
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
        print(f"# frames_ok={dec.frames_ok} crc_err={dec.crc_errors} "
              f"framing_err={dec.framing_errors}")


if __name__ == "__main__":
    main()
