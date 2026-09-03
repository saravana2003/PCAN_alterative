# host/ — PC-side tools for the EK-RA8D1 CAN logger

Talk to the `apps/can_logger` firmware over the J-Link VCOM serial port
(COM12 by default, 115200 baud). The firmware speaks **CLP** (CAN Log Protocol)
— a COBS-framed, CRC-16/X-25 binary protocol; full spec in
`../docs/clp_protocol.md`.

Requires the project `.venv` (has `pyserial`); `tkinter` ships with CPython.

| file | what it is |
|------|------------|
| `clp.py` | the CLP codec (COBS, CRC-16/X-25, encode/decode, streaming `Decoder`). Shared, no deps. |
| `clp_client.py` | headless CLI: print frames, inject one `CAN_TX`. |
| `clp_gui.py` | tkinter GUI: live per-ID bus table, STATUS/link panel, event log, TX form. |

## GUI

```
.venv\Scripts\python host\clp_gui.py            # COM12 @ 115200
.venv\Scripts\python host\clp_gui.py COM7 921600
```

Enter the port, click **Connect**. The *Bus* table shows one row per CAN ID
(count + cycle time, PCAN-View style); *STATUS* / *Link* update from the 1 Hz
`STATUS` frame and the boot `HELLO`. Fill the *Transmit* box (ID in hex, flags,
hex data) and **Send** to put a frame on the bus — the `CAN_TX_ACK` shows in the
event log.

## CLI

```
.venv\Scripts\python host\clp_client.py COM12
.venv\Scripts\python host\clp_client.py COM12 --tx 0x7DF:0201000000000000
.venv\Scripts\python host\clp_client.py COM12 --tx 0x18DAF110:DEADBEEF --ext
.venv\Scripts\python host\clp_client.py COM12 --seconds 10 --raw
```

## Note

COM12 carries **only** CLP binary — the firmware routes its own logs to SEGGER
RTT (`JLinkRTTViewer` / `JLinkRTTClient`). Don't open COM12 in a plain terminal
expecting text.
