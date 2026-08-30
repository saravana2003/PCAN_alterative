# apps/ — application code

Each subdirectory is a standalone Zephyr application (build with
`west build -b ek_ra8d1 apps/<name> -d build/<name>`).

- **can_logger/** — Phase 2A. Self-contained CAN-FD interface module for
  `canfd0` (init, ISR-driven RX callback, TX, frame struct) plus a minimal
  `main.c` that exercises it. Build-only; not wired to LEDs/flash/USB yet.
  Carries its own `boards/ek_ra8d1.overlay` (CANFD0 pins P202/P203 + bitrates).
- **gpio_timer/** — Phase 2B. Two independent modules: `io_control` (S1/S2
  buttons via edge interrupts drive capture start/stop; LEDs show state —
  green idle / red blinking while logging) and `log_timer` (the report's
  "Timer" deliverable — a `k_timer` periodic tick for CAN-frame timestamping
  and batched flash-flush triggers). `main.c` wires them for the build/demo
  only. Build-only; no overlay needed (all stock `ek_ra8d1.dts` nodes).
- **usb_cdc/** — Phase 2C. USB CDC-ACM device (USB device stack "next", on
  `usbhs`) plus **CLP** ("CAN Log Protocol") v1 — a COBS-framed, CRC-16,
  typed binary protocol for the board↔GUI link (`clp_proto.*`), and the
  CDC-ACM transport glue (`usb_link.*`). `main.c` runs a hardware-free CLP
  encode→parse self-test. Protocol spec: `../docs/clp_protocol.md`. Build-only;
  carries its own `boards/ek_ra8d1.overlay` (adds the `cdc-acm-uart` node).
- **flash_log/** — Phase 2D. littlefs on the on-board 64 MB Octo-SPI NOR
  (`&s28hl512t`) mounted at `/lfs`, plus a batch-writing CAN-frame logger:
  `flash_log_record()` stages a compact `"CLB1"` record into an 8 KB SRAM ring;
  a writer thread drains it to the open log file in ≤1 KB `fs_write()` batches.
  `main.c` runs mount→start→200 synthetic frames→flush→stop. Build-only;
  carries its own `boards/ek_ra8d1.overlay` (re-partitions the OSPI flash +
  adds the `zephyr,fstab` node).
- **eth_doip/** — Phase 2E. On-chip Ethernet with a static IP (`192.168.1.50`)
  plus a minimal **DoIP** (ISO 13400) responder for log retrieval: vehicle
  announcement, routing activation, and a placeholder UDS
  upload/transfer flow (`doip.c`, transport-free), served over UDP + TCP
  `:13400` (`doip_server.c`). `main.c` runs a network-free DoIP self-test.
  Spec: `../docs/doip_skeleton.md`. Build-only. Carries a small
  `boards/ek_ra8d1.overlay` that disables `&i3c0` (it collides with Ethernet
  MDC on P401 in the stock board files). HW: SW1-5 ON, SW1-4/SW1-3/SW1-8 OFF.
