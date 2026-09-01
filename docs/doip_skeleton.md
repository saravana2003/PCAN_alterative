# DoIP log-retrieval skeleton (Phase 2E)

`apps/eth_doip/` brings up the on-chip Ethernet with a **static IP** and a
minimal **DoIP** (Diagnostics over IP, ISO 13400-2) responder for pulling CAN
logs off the board. It is a *skeleton* — enough of the protocol to demonstrate
the retrieval path, not a conformant ISO 13400 stack.

## Network

| Item | Value |
|------|-------|
| IP address | `192.168.1.50` (static, no DHCP) |
| Netmask | `255.255.255.0` |
| Gateway | `192.168.1.1` (bench default — adjust on site) |
| DoIP UDP (discovery) | `0.0.0.0:13400` |
| DoIP TCP (diagnostics) | `0.0.0.0:13400` |

Applied at boot by `CONFIG_NET_CONFIG_SETTINGS`. On real hardware it only takes
effect once the PHY link is up.

**Board switches:** SW1 is the 8-way DIP switch beside the RJ45 / PHY (UM
Figure 1, silkscreen "SW1 / CONFIGURATION SWITCHES") — not a push-button. The
stock Zephyr `ek_ra8d1` uses the *ETHERNET B* RMII pin-mux (manual Table 24).
Set **SW1-5 ON** (ETH-B), **SW1-4 OFF** (ETH-A), **SW1-3 OFF** (camera),
**SW1-8 OFF** (I3C). SW1-7 (SDRAM, ON by default) is fine — SDRAM only conflicts
with ETH-A. NOTE the factory default is **SW1-3 CAMERA ON / SW1-5 ETH-B OFF**,
so Ethernet is *not* wired to the PHY out of the box — you must flip SW1-3 off
and SW1-5 on. Verified on hardware 2026-09-01 (ping + DoIP round-trip).

**Pin conflict (fixed in the app overlay):** stock `ek_ra8d1.dts` enables both
`&mdio` (ET0_MDC = P401) and `&i3c0` (I3C0_SDA = P401). `apps/eth_doip`'s
overlay disables `&i3c0` so MDIO owns P401.

**PHY reset:** the stock `ethernet-phy@5` node has no `reset-gpios`. If the
ICS1894-32 PHY doesn't link, add `reset-gpios = <&ioport7 6 GPIO_ACTIVE_LOW>`
(ETH-B RESET_N = P706).

## DoIP identity

| Item | Value | Notes |
|------|-------|-------|
| Entity logical address | `0x1234` | this logger, as a DoIP node — placeholder |
| Accepted tester address range | `0x0E00`–`0x0FFF` | standard external-tester range |
| Protocol version | `0x02` | ISO 13400-2:2012 |
| VIN (placeholder) | `EKRA8D1CANLOGGER0` | |
| EID | board MAC `74:90:50:B0:5D:E9` | |

## Message flow

DoIP message = 8-byte header (`ver`, `~ver`, `payload_type` u16 BE,
`payload_length` u32 BE) + payload.

1. **Discovery (UDP).** Tester sends a Vehicle Identification Request
   (`0x0001` / `0x0002` / `0x0003`). Board replies with a Vehicle
   Announcement / Identification Response (`0x0004`): VIN(17) + logical
   address(2) + EID(6) + GID(6) + further-action(1).

2. **Routing activation (TCP).** Tester connects and sends a Routing
   Activation Request (`0x0005`): source address(2) + activation type(1) +
   reserved(4). Board replies `0x0006`: tester LA(2) + entity LA(2) +
   response code(1, `0x10` = success) + reserved(4). The connection is now
   "routing active".

3. **Diagnostics (TCP).** Tester sends a Diagnostic Message (`0x8001`):
   SA(2) + TA(2) + UDS bytes. Board replies with a Diagnostic Message
   Positive Acknowledge (`0x8002`) immediately followed by a Diagnostic
   Message (`0x8001`) carrying the UDS response. (Not routing-active, or
   wrong TA → Diagnostic Message Negative Acknowledge `0x8003`.)

   Also handled: Alive Check Request (`0x0007`) → `0x0008`.

## UDS placeholder — the log-download flow

Carried inside DoIP diagnostic messages. All responses are placeholders in this
phase; a real build wires them to the Phase 2D littlefs store (`/lfs/log_*.clb`).

| Request | Bytes | Response |
|---------|-------|----------|
| ReadDataByIdentifier, DID `0xF190` (VIN) | `22 F1 90` | `62 F1 90` + VIN(17) |
| ReadDataByIdentifier, DID `0xFD00` (log-store status) | `22 FD 00` | `62 FD 00` + num_files(u16) + total_bytes(u32) |
| RequestUpload | `35 …` | `75 20` + maxNumberOfBlockLength(u16 = 1024) |
| TransferData | `36 <seq> …` | `76 <seq>` + log chunk |
| RequestTransferExit | `37` | `77` |
| anything else | | `7F <sid> 11` (serviceNotSupported) |

Retrieval-tool sketch: discover → routing-activate → `RequestUpload` → loop
`TransferData` incrementing the block sequence counter until the chunk is
short → `RequestTransferExit`.

## Not implemented (future work)

Unsolicited startup vehicle announcements (3×), alive-check timers, the
`0x8002`/`0x8003` ack timing rules, TLS (DoIP-over-TLS), power-mode info
(`0x4003`), entity status (`0x4001`), full UDS session/security handling, and
actually streaming file bytes from littlefs.
