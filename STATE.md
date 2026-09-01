# STATE.md — Progress Log (update at the end of every session)

Keep entries short. This file exists so a brand-new chat session can pick up
exactly where the last one left off without re-reading old chat history.

---

## >>> START HERE (new session, read this box first) <<<

**Project state as of 2026-09-01 (session 3).** Board on the bench. **PHASE 5
HARDWARE BRING-UP IS COMPLETE.** Steps 0/1/2/3 PASS, Step 4 (flash) DROPPED
(scope change), **Step 5 (Ethernet/DoIP) PASS**, **Step 6 (MCUboot) PASS**.
Every remaining capability the project cares about is proven on real silicon.

**SCOPE CHANGE (user, 2026-09-01):** on-board flash logging is **out of scope**.
The goal is now simply "USB + CAN + Ethernet work on the board" — all three done.
Step 4 (`flash_log`) is abandoned — it was hardware-blocked anyway (silent OSPI
NOR). `can_logger` still keeps its SRAM ring; nothing was ripped out. CLAUDE.md's
Mission paragraph still says "data logging" — leave it, but this is the operative
direction now.

**Do this next:** bring-up is done — the project moves to **the host GUI** (over
the J-Link VCOM/UART; CLP is transport-agnostic and its self-test passes on
hardware every boot) and **Phase 6 documentation** (the BITS WILP ESD report,
assembled from the real hardware results now in hand). No more bring-up steps.
Board is currently left running MCUboot + signed `can_logger` in the CAN bench
config (SW1-5 OFF, transceiver on P704/P705). For Ethernet again: SW1-5 ON /
SW1-3 OFF and move the transceiver wires off P704/P705.

**Six things that will otherwise waste your time:**

1. **Zephyr is now on mainline `66e5135ffc3`** (detached HEAD), bumped from
   `f80761e4940` on user instruction, `west update` clean. All 6 apps rebuild
   OK; can_logger/gpio_timer are byte-identical to the old tree. To go back:
   `git -C zephyr checkout f80761e4940 && west update`.
2. **USB-HS CDC-ACM bulk transfer is FIXED** — the bulk-IN ZLP was never
   committed in hal_renesas FSP `r_usb_device.c`. **Already fixed upstream** by
   hal_renesas `f2c2aa6359e` (2026-08-04) — the very next commit after the
   manifest's `f2eb9bc`. We independently found + diagnosed it (session 1-2),
   then bumped `modules/hal/renesas` to `f2c2aa6359e` and dropped our local
   patch. Hardware-verified. `docs/zephyr_usb_hs_bug.md`. **USB CDC transport is
   viable again.** (usb_cdc also keeps the session-1 MAIN_STACK_SIZE fix.)
3. **`patches/` holds ONE local fix** — `0001-*` (OSPI CS-reset), for `zephyr/`.
   Re-apply after `west update`; also `git -C modules/hal/renesas checkout
   f2c2aa6359e` to keep the USB fix. See `patches/README.md`.
   **Upstream:** OSPI fix -> [zephyr#117908](https://github.com/zephyrproject-rtos/zephyr/pull/117908)
   (OPEN, flagged code-review-only). USB fix PR (hal_renesas#220) closed as
   duplicate of `f2c2aa6359e`.
4. **Step 4 / OSPI flash: blocked on HARDWARE, not software.** The S28HL512T
   (U3) answers `0xFF` to every command — JEDEC ID, RDSR, CFR2V — before and
   after reset, warm boot and confirmed power cycle. Controller transacts fine
   (`FSP_SUCCESS`); the chip is electrically silent. Needs a physical check of
   U3 population + the OSPI config links on the "MCU Native Pin Access" area
   against the board schematic. The wrong-CS-reset driver bug we found+fixed
   (`patches/0001-*`) is real but is NOT the cause. `docs/zephyr_ospi_cs_reset_bug.md`.
5. **Renesas DLM lock** (session 1): if `west flash` fails weirdly, it's the
   lifecycle state — section 1 below. Fixed with RFP "Initialize Device".
6. **Bench PCAN-USB is classic-CAN only** — CAN-FD/BRS unvalidated.
   **CAN + Ethernet mutually exclusive** — move the transceiver wires before Step 5.

**Build/flash quick reference:**
```
.venv\Scripts\west build -b ek_ra8d1 apps/<app> -d build/<app> [-p always]
.venv\Scripts\west flash -d build/<app>          # J-Link, board attached
```
Console = **COM12** (J-Link VCOM, 115200). Capture helpers: session scratchpad
`cap.py` / `capwait.py` / `capcycle.py` (pyserial). **A full power cycle also
re-enumerates the J-Link VCOM**, so `capcycle.py` (wait-for-disappear-then-
reappear) is the one to use for power-cycle tests.

**Python env note (session 2):** the venv's base install `C:\Python312\Lib` had
been deleted, making every build painfully slow ("Could not find platform
independent libraries <prefix>"). Restored `Lib` + `DLLs\*.pyd` from the
identical 3.12.3 at `C:\Users\sumit\AppData\Local\Programs\Python\Python312`.
If builds crawl again, check `C:\Python312\Lib\os.py` exists.

---

## Current Phase
Phase 5 — HARDWARE BRING-UP. **First real hardware session: 2026-08-31.**
Board, PCAN unit and rig are physically on the bench. Everything before today
was build-only; CLAUDE.md's old "board not available" constraint is gone.

STATUS (after session 3, 2026-09-01): **Phase 5 bring-up COMPLETE.** Steps 0, 1,
2, 3 **PASS**. Step 5 (eth_doip) **PASS** — PHY links 100M full-duplex, ping 6/6,
full DoIP path verified from the laptop. Step 6 (MCUboot) **PASS** — bootloader
banner precedes the app, signed image chainloads, bad image rejected. Step 4
(flash_log) **DROPPED** — user removed logging from scope; was hardware-blocked
anyway (silent OSPI NOR). Next: host GUI + Phase 6 report.

### Session 3 (2026-09-01) — Step 5 Ethernet / DoIP PASS; flash logging dropped
- **SCOPE CHANGE (user):** flash logging is out of scope. Goal = USB + CAN +
  Ethernet working. Step 4 (`flash_log`) abandoned (was hardware-blocked anyway).
- **Step 5 (`eth_doip`) PASS on hardware.** Pristine rebuild against the current
  tree (Zephyr `66e5135` + OSPI patch 0001, hal_renesas `f2c2aa6`): exit 0,
  FLASH 112000 B / RAM 57720 B (~= the Phase 2E build-only record). `west flash`
  exit 0.
  - Console over a clean reset: `phy_mii: PHY (5) ID 15F450` -> boot banner
    `66e5135ffc3c` -> (`net_config: Waiting interface 1 to be up...`) ->
    **at t≈23 s** `phy_mii: PHY (5) Link speed 100 Mb, full duplex` ->
    `net_config: IPv4 address: 192.168.1.50` -> DoIP self-test PASS ->
    `doip_server: DoIP UDP discovery on :13400` + `TCP server on :13400`.
  - **`ping 192.168.1.50` from the laptop: 6/6, <1 ms, TTL 64.** ARP resolved
    board MAC **74:90:50:B0:5D:E9** (Renesas OUI, matches the doip_skeleton EID).
  - **Full DoIP retrieval path exercised from a Python test client** on the
    laptop (`scratchpad/doip_test.py`), all three stages PASS:
    1. UDP Vehicle Identification Request (0x0001) -> Vehicle Announcement
       (0x0004): VIN `EKRA8D1CANLOGGER0`, logical addr 0x1234, EID = board MAC.
    2. TCP Routing Activation Request (0x0005), tester SA 0x0E80 -> 0x0006,
       response code **0x10 (success)**, entity LA 0x1234.
    3. TCP Diagnostic Message (0x8001) SA 0x0E80 / TA 0x1234 / UDS `22 F1 90`
       -> 0x8002 ACK then 0x8001 carrying `62 F1 90` + `EKRA8D1CANLOGGER0`.
  - So Ethernet is proven end-to-end: PHY + RMII (ETH-B mux) + Zephyr net stack
    + UDP & TCP sockets + the DoIP/UDS skeleton logic, all on real silicon.
- **SW1 config-switch reality (corrected):** SW1 is the single 8-way DIP switch
  next to the RJ45 / PHY (U15), silkscreen "SW1 / CONFIGURATION SWITCHES", UM
  Figure 1. The board's **factory default is SW1-3 CAMERA=ON, SW1-5 ETH-B=OFF**
  — i.e. Ethernet is NOT wired to the PHY out of the box (STATE.md previously
  implied SW1-5 ON was default; it is not). For Ethernet-B: set **SW1-5 ON,
  SW1-3 OFF** (SW1-4/SW1-6/SW1-8 already OFF, SW1-7 SDRAM stays ON). Used this
  session: SW1-5 ON + SW1-7 ON, everything else OFF. CAN transceiver wires
  physically off P704/P705.
- **Laptop NIC:** USB-C Ethernet dongle ("Ethernet 2", ASIX AX88179) set static
  **192.168.1.10 / 255.255.255.0**, no gateway. Link came up 100 Mbps full duplex.
- **Minor observation, not chased:** PHY link takes ~23 s to report up after an
  MCU reset (normal auto-neg is 2-4 s). `ethernet-phy@5` has no `reset-gpios`;
  `docs/doip_skeleton.md` already notes adding
  `reset-gpios = <&ioport7 6 GPIO_ACTIVE_LOW>` (ETH-B RESET_N = P706) if link
  ever fails. It doesn't fail, just starts slow — left alone.
- **Step 6 (MCUboot) PASS on hardware.** Pristine `--sysbuild -p always` rebuild
  of `build/can_logger_mcuboot` against the current tree: exit 0. mcuboot 39048 B
  (fits the 128K boot_partition), signed can_logger 46108 B of the 928K slot0
  (4.86%). `west flash -d build/can_logger_mcuboot` flashes BOTH domains
  (mcuboot @ 0x0, `zephyr.signed.hex` @ 0x20000), exit 0.
  - Console over reset: `*** Using Zephyr OS build 66e5135ffc3c ***` /
    `I: Starting bootloader` / `I: Image index: 0, Swap type: none` /
    `I: Bootloader chainload address offset: 0x20000` / `I: Image version:
    v0.0.0` / `I: Jumping to the first image slot` -> THEN the app:
    `*** Booting Zephyr OS build 66e5135ffc3c ***` + `can_iface: canfd0 started`.
    MCUboot verified the ECDSA-P256 signature and chainloaded.
  - **Negative test PASS:** erased the 32K block at slot0 start (0x02020000) with
    J-Link (`erase 0x02020000 0x02027FFF`; verified mem there = FF, and 0x0
    untouched). MCUboot then prints `E: Unable to find bootable image` and halts
    — no app banner. Re-flashed the signed image; good boot restored and
    confirmed. So MCUboot does NOT boot an image with an invalid header/sig.
  - Mode is OVERWRITE_ONLY (Phase 3 decision — flash0 write-block-size 128 vs
    MCUboot's BOOT_MAX_ALIGN<=32 for swap modes). No A/B auto-revert; recovery
    from a bad update = re-flash a good higher-versioned image.
  - Still on the module DEBUG signing key (`root-ec-p256.pem`) — build logs the
    "for debug use" warning. A real project key is a one-line config change
    (`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`), private key kept offline. Report item.
- Scratchpad helpers this session: `cap.py <port> <secs>` (pyserial console
  capture), `reset.jlink` (JLink CommanderScript: reset+go),
  `corrupt_slot0.jlink` (erase slot0 header for the MCUboot negative test),
  `doip_test.py` (the DoIP client). Recreate from git history / these entries.

### Session 2 (2026-08-31) — Zephyr bump + both upstream bugs
- **Zephyr `f80761e4940` -> `66e5135ffc3`** (mainline HEAD), detached HEAD, on
  user instruction. `west update` clean (only fatfs/hal_nxp/nrf_wifi moved — none
  used by ek_ra8d1; hal_renesas unchanged at f2eb9bc). Revert: `git -C zephyr
  checkout f80761e4940 && west update`.
- **Regression on the new tree: all 6 apps build clean.**
  hello_world OK; can_logger 45428 B / gpio_timer 38012 B — **byte-identical** to
  the session-1 records; flash_log 74424 B (+CS patch); usb_cdc 70932 B (~same);
  eth_doip building at session end.
- **USB fix — DONE + HW-verified.** `modules/hal/renesas/.../r_usb_device.c`
  `process_pipe_xfer()`: the bulk-IN ZLP branch only re-asserted `BVAL` if
  already set, so the ZLP was never sent, no `BRDY`, no `XFER_COMPLETE`, and
  `usbd_cdc_acm`'s `CDC_ACM_TX_FIFO_BUSY` stuck forever (it enqueues a ZLP on
  enable). Fix: select FIFO + `pipe_wait_for_ready` + set `BVAL` unconditionally,
  like `pipe_xfer_in()`. `patches/0002-*`. HW: host now reads the 33-byte CLP
  HELLO frame (was 0 bytes); host->device parse also verified.
- **OSPI CS-reset fix — real defect, does NOT fix this board.** Driver drove
  `RSTCS0` unconditionally; ek_ra8d1 NOR is on CS1. Fixed (`patches/0001-*`).
  But bench diagnosis (heartbeat build + JEDEC/RDSR probes) shows the S28HL512T
  answers `0xFF` to everything pre- and post-reset, warm and after a **confirmed
  power cycle**. `LIOCTL=0x00030003` (resets released), controller OK. => the
  flash chip is not responding at all — a board/population/config-link problem
  on this unit. Step 4 needs a physical hardware check, not more driver work.
- **`patches/`** (new dir) + `patches/README.md`: carries both fixes since
  zephyr/ and modules/ are git-ignored; re-apply after `west update`.
- Both bugs still need upstream filing (no `gh` CLI here). Issue text is in
  `docs/zephyr_usb_hs_bug.md` (-> hal_renesas) and `docs/zephyr_ospi_cs_reset_bug.md`
  (-> zephyr).
- Fixed a broken venv (missing `C:\Python312\Lib`) that was crippling build speed.

### Where things stand in one paragraph
The board works. hello_world, gpio_timer and can_logger have all been flashed,
booted and exercised on real silicon. GPIO/LEDs/buttons/timer are validated,
and the CAN-FD driver is proven bidirectionally against a real PCAN analyser
on the corrected P704/P705 pins. Two significant things were discovered today:
the board shipped in a locked Renesas DLM state that made most of the flash
unprogrammable (fixed), and the bench analyser is classic-CAN-only, so CAN-FD
with BRS cannot be validated with current equipment (deferred, not broken).

---

## 1. ROOT CAUSE OF THE DAY'S FLASH FAILURES: RENESAS DLM STATE (FIXED)
**READ THIS BEFORE RE-DIAGNOSING ANY FLASH FAILURE ON THIS BOARD.**

SYMPTOM: `west flash` succeeded for hello_world (27352 B) but failed every time
for gpio_timer (38012 B) and can_logger (45436 B). J-Link reported only
"Timeout while calculating CRC, RAMCode did not respond in time!" then "Failed
to erase sectors". pyocd localised it precisely: `flash erase sector failure
(address 0x02008000)`. A sector map showed everything below 0x02008000 erasing
fine and **everything at or above it failing** — only the first 32 KiB of the
2 MB code flash was usable. Chip erase failed too.

ACTUAL CAUSE: the device was in Renesas **Device Lifecycle Management state
OEM_PL2, Authentication Level AL2** — almost certainly left from Renesas's
factory secure-provisioning flow on this board sample. In that state code flash
above the first 32 KiB refuses to erase, and **nothing in the J-Link or pyocd
output ever mentions protection or lifecycle state.**

FIX: **Renesas Flash Programmer (RFP) -> "Initialize Device"** (run by the
user). Completed cleanly. Immediately afterwards the SAME command with the SAME
unchanged artifact — `west flash -d build/gpio_timer` — returned EXIT 0 and the
app booted. gpio_timer went 0/4 -> 1/1 with zero changes to code, config or
tooling. can_logger (45436 B) then flashed cleanly too, re-confirming the fix.

IT WAS **NOT** ANY OF THESE — all tested and eliminated, do not resurrect:
- NOT image size or duration. The 27 KB-passes / 38 KB-fails pattern was a
  COINCIDENCE of where the 32 KiB boundary fell. Very convincing, entirely
  wrong.
- NOT AV / Windows Defender. Tested directly with exclusions on JLink.exe, the
  SEGGER folder and the project tree; failed identically. (Exclusions were
  added elevated and should be REMOVED if that was never done.)
- NOT board damage or bad silicon. We came close to declaring a defect and
  starting an RMA. The flash was fine all along.
- NOT SWD speed, probe contention, board state, OFS/option-memory contents, or
  block protection. BPS/PBPS/BPS_SEL all read FFFFFFFF from the live device —
  genuinely unprotected, because the lock was in the DLM state, which is
  invisible to both J-Link and pyocd.

LESSON: neither J-Link nor pyocd can see or report DLM / Authentication Level.
If flash erase fails on an RA part in a way that makes no sense, reach for the
VENDOR tool (RFP) EARLY to read the lifecycle state. Hours went into bisecting
image sizes and AV settings that no amount of J-Link/pyocd work could ever have
resolved.

---

## 2. BRING-UP RESULTS (docs/bringup_checklist.md)

### Step 0 — Debug link: **PASS**
- Probe: on-board SEGGER J-Link OB (JLink_V948), enumerates as "JLink CDC UART
  Port (COM12)". Console = uart9 (SCI9) @ 115200, board default.
- `west flash -d build/hello_world` -> EXIT 0; J-Link IDs the part correctly
  ("Cortex-M85 identified", I-/D-Cache L1 16 KB each).
- Console: `*** Booting Zephyr OS build f80761e49401 ***` /
  `Hello World! ek_ra8d1/r7fa8d1bhecbd`. Build hash matches our checkout.

### Step 1 — GPIO / LEDs / buttons / timer (apps/gpio_timer): **PASS**
Zero code changes were needed.
- [x] Idle LED = GREEN, user-confirmed visually. Matches io_control.c's led2
      (P40E). Silkscreen and code AGREE; the Phase 2B / Table 22 mapping is now
      hardware-confirmed.
- [x] S1 (P009) -> red (led3/P107) blinking @250 ms. Confirms P009 ->
      port_irq13 -> NVIC 89.
- [x] S2 (P008) -> solid green. Confirms P008 -> port_irq12 -> NVIC 88.
- [x] No double-triggers: 240 s capture over 8 press-pairs gave exactly 8
      "capture START" and 8 "capture STOP", STRICTLY ALTERNATING. Shortest
      STOP->START gap 855 ms, far outside the 40 ms debounce window.
      HONEST CAVEAT: this proves correct BEHAVIOUR, not that the debounce code
      was stressed. set_state() early-returns when the requested state equals
      the current one, so a same-button bounce is silently absorbed and never
      logged. Exercising DEBOUNCE_MS would need instrumentation in
      button_isr(). Not worth doing unless bouncing is actually observed.
- [x] Timer accuracy MEASURED over a long run: 299 ticks across 29.979 s =
      **100.26 ms/tick against a 100 ms target, no drift**. Shorter runs agree
      (68/6.846 s, 33/3.310 s, 30/3.050 s). The "Timer" report deliverable is
      now a measured number, not a compiled promise.

### Step 2 — CAN-FD (apps/can_logger, plain build): **PASS, both directions**
- Boot line on silicon: `canfd0 started: CAN-FD, core clock 80000000 Hz,
  RX IRQ 45 (NVIC)`. Proves canfd0 bound, CAN_MODE_FD accepted, all 3 RX
  filters registered, can_start() OK, RX ISR on NVIC IRQ 45.
- **PCAN -> board (RX): PASS.** 10 frames, zero errors, zero drops:
    - `id=0x00000123 dlc=8 flags=0x00` — 11-bit classic. Matched targeted
      filter [0] (id=0x100 mask=0x700, covering 0x100..0x1FF).
    - `id=0x18daf110 dlc=8 flags=0x01` — flags 0x01 = CAN_FRAME_IDE
      (can.h:147). The 29-bit ID arrived INTACT (not truncated to 11 bits) and
      was correctly flagged extended. Matched catch-all filter [2].
- **Board -> PCAN (TX): PASS.** On S3 reset the analyser receives the boot
  frame ID 0x123, 8 bytes 00..07, decoded correctly.
- **Bus health: PASS.** BUSOK maintained; no form/CRC/ACK error storm, no
  bus-off. The board ACKed every frame — impossible if TX/RX were swapped or
  termination were missing, so both of those classic wiring faults are ruled
  out by evidence.
- Software RX timestamps validated: ts=1971050 ticks @10 kHz = 197.1 s,
  matching the console's own [00:03:17] stamp. The k_uptime_ticks() fallback
  (needed because the RA driver has no CONFIG_CAN_RX_TIMESTAMP) works in ISR
  context.
- **The RX interrupt path is proven end-to-end on silicon**: NVIC IRQ 45 ->
  canfd_common_fifo_rx_isr -> can_iface_rx_isr -> on_can_rx. Together with the
  Step 1 button IRQs, the "Interrupt + NVIC" report deliverable now has TWO
  hardware-verified data points.
- **THE P704/P705 PIN CORRECTION IS VALIDATED BY REAL TRAFFIC.** Frames
  physically flowed on CTX0=P704 / CRX0=P705. Today's retraction of the
  P202/P203 decision is confirmed by working hardware, not just by datasheet
  reasoning. Full reasoning: the "CANFD0 pin decision" section below.

### Step 3 — USB CDC-ACM (apps/usb_cdc): **PARTIAL — two bugs found. Bug 1 was
### ours and is FIXED. Bug 2 is IN ZEPHYR'S RA USB STACK and blocks Step 3.**

#### Bug 1 (OURS, FIXED): main-thread stack overflow
- SYMPTOM: boot reached "USB CDC-ACM up", then ~49 ms later:
      <err> os: ***** USAGE FAULT *****
      <err> os:   Illegal load of EXC_RETURN into PC
      <err> os: Faulting instruction address (r15/pc): 0x22003400
      <err> os: >>> ZEPHYR FATAL ERROR 34 ... Halting system
- CAUSE: `CONFIG_MAIN_STACK_SIZE` was never set in apps/usb_cdc/prj.conf, so it
  defaulted to **1024**. clp_selftest() puts a struct clp_parser (87-byte frame
  buffer + cobs_decoder), a struct clp_can_frame (64 data bytes) and
  uint8_t wire[CLP_MAX_WIRE] on that stack, then calls clp_encode_raw() which
  NESTS another 87-byte logical[] plus a cobs_encoder.
- EVIDENCE IT WAS THE STACK: faulting PC was in SRAM with no symbol (checked
  with arm-zephyr-eabi-nm); LR resolved (addr2line) to cobs_sink_cb,
  clp_proto.c:54; and the dumped FP registers contained the SELF-TEST'S OWN
  DATA (s[8..15] = a7a4a500 a3a0a1a6 ... = in.data[i]=i^0xA5; s[6]/s[7] =
  55667700/11223344 = the test timestamp 0x0011223344556677).
- FIX: `CONFIG_MAIN_STACK_SIZE=4096` added to apps/usb_cdc/prj.conf, with a
  comment recording the fault. Crash GONE, boot now clean. (Phase 2D's
  flash_log already set this explicitly; usb_cdc was simply missed.)

#### USB enumeration: **WORKS**
      Name     : USB Serial Device (COM13)
      DeviceID : USB\VID_2FE3&PID_0001&MI_00\7&78F25EB&0&0000
- VID 0x2FE3 / PID 0x0001 are the Zephyr test IDs recorded as PLACEHOLDERS in
  Phase 2C, so this is unambiguously our device. Windows' inbox usbser.sys
  binds it; no driver install needed.
- Device-side log confirms a correct High-Speed enumeration:
      <inf> usbd_init:    bNumInterfaces 2 wTotalLength 75
      <inf> usbd_core:    Actual device speed 2      (= High Speed)
      <inf> usbd_cdc_acm: Configuration enabled
- The USB **Full-Speed** port stays silent, which is CORRECT and expected:
  usbfs is not enabled in the stock ek_ra8d1 DTS (Phase 1 finding); this app
  deliberately targets USB-HS.

#### Bug 2 (NOT OURS — ZEPHYR RA USB-HS STACK): bulk data never reaches the host
- SYMPTOM: DTR is detected, HELLO is generated and handed to the CDC-ACM
  driver, and the driver ACCEPTS it — but ZERO bytes ever arrive at the host:
      <inf> usb_link: host connected (DTR) - sending HELLO
      <inf> usbd_cdc_acm: tx_en: trigger irq_cb_work
      <inf> usbd_cdc_acm: UART dev 0x200ed08, len 33, remaining space 991
      (second HELLO -> "remaining space 958")
  The driver's own TX FIFO free space goes 1024 -> 991 -> 958: our bytes are
  accumulating INSIDE the class driver and are never forwarded to the USB IN
  endpoint. Host read 0 bytes over repeated 5-6 s windows.
- OUR CODE IS DOING ITS JOB: tx_push() -> uart_irq_tx_enable() -> our cdc_isr
  -> uart_fifo_fill() all execute, and the driver's own log line confirms it
  accepted 33 bytes. The break is BELOW our application code.
- **PROVEN NOT OURS by running Zephyr's OWN STOCK SAMPLE.** Built and flashed
  unmodified `zephyr/samples/subsys/usb/cdc_acm` (cdc_acm_echo, which echoes
  whatever it receives) on this board. Result: it enumerates, its CONTROL
  transfers work —
      <inf> cdc_acm_echo: USBD message: CDC ACM line coding
      <inf> cdc_acm_echo: Baudrate 115200
      <inf> cdc_acm_echo: USBD message: CDC ACM control line state
  — but writing 17 bytes to it produced **0 bytes echoed back**. The stock
  sample fails exactly like ours.
  => The fault is in Zephyr's RA USB device stack (udc_renesas_ra /
     usbd_cdc_acm) for USB-HS on this board at mainline v4.4.99 (f80761e).
     apps/usb_cdc and the CLP layer are EXONERATED.
  **FULL WRITE-UP + UPSTREAM-READY REPRO: `docs/zephyr_usb_hs_bug.md`.**
  That file is prepared material for a zephyrproject-rtos/zephyr issue; it
  has not been reported upstream yet.
- CONTROL transfers work; BULK data transfer does not. That is the precise
  shape of the bug for anyone searching upstream issues.
- **SCOPE: does NOT touch the CLP protocol or the GUI contract.** The CLP
  self-test passes on hardware every boot (correct 89-byte wire length for an
  FD-64 frame, round-trip OK, resync OK, corrupted frame counted):
      <inf> main: selftest PASS (crc_err=1 framing_err=0 frames_ok=2)
  docs/clp_protocol.md needs no change. What is unproven is the TRANSPORT, not
  the protocol.
- INVESTIGATION DEAD END WORTH RECORDING: I tried to narrow this via the class
  driver's LOG_DBG lines (CONFIG_USBD_CDC_ACM_LOG_LEVEL_DBG=y, confirmed
  CONFIG_USBD_CDC_ACM_LOG_LEVEL=4 in .config) — but NO DBG output ever appeared,
  not even from paths known to execute. So "no DBG line" could NOT be used as
  evidence, and an earlier conclusion built on that (CDC_ACM_TX_FIFO_BUSY being
  stuck) is UNPROVEN — do not treat it as fact. The stock-sample comparison is
  what actually settled the question. Those temporary log-level edits have been
  REVERTED; only CONFIG_MAIN_STACK_SIZE=4096 remains.
- NEXT OPTIONS for Step 3 (none attempted, need a decision):
  1. Search/raise a Zephyr upstream issue for RA (RA8D1) USB-HS bulk transfer.
     Have a precise repro: stock cdc_acm sample, ek_ra8d1, no echo.
  2. Try a different Zephyr revision — mainline v4.4.99 was pinned on
     2026-08-29 for Cortex-M85 support; a newer or tagged release may have RA
     UDC fixes. Weigh against the "mainline can break between pulls" risk
     already logged in the decisions log.
  3. Try USB **Full-Speed** instead (usbfs), which would need enabling in an
     overlay — a DIFFERENT UDC path, so it may sidestep the HS bug.
  4. Fall back to the J-Link VCOM/UART as the GUI transport for now. CLP is
     transport-agnostic (byte stream in, byte stream out), so the GUI work is
     NOT blocked by this — only the USB transport is.

---

## 3. TEST-EQUIPMENT LIMITATION — AFFECTS PROJECT SCOPE
**The bench analyser is a PCAN-USB, which is CLASSIC CAN ONLY.** It cannot
decode CAN-FD. The giveaway is PCAN-View's status bar: "Bit rate: 500 kbit/s"
with no data bitrate field.
- **CAN-FD with BRS — the project's headline capability — CANNOT BE VALIDATED
  on this rig.** This is an EQUIPMENT GAP, not a firmware defect. It needs a
  PCAN-USB **FD** or equivalent FD-capable analyser. Do not write the Phase 6
  report as though FD were validated.
- SYMPTOM IT CAUSED: PCAN-View showed **BUSHEAVY immediately on connect**,
  before any manual frame was sent. Chain, all verified in-tree:
    1. can_logger's main() sent one boot frame with CAN_FRAME_FDF|CAN_FRAME_BRS.
    2. The classic-only analyser cannot decode FD -> emits error frames.
    3. The RA controller AUTO-RETRANSMITS (retry on lost arbitration / missing
       ACK is the CAN default, zephyr/include/zephyr/drivers/can.h:1316).
    4. That retry CANNOT be disabled here: CAN_MODE_ONE_SHOT exists
       (can.h:104, BIT(3)) but can_renesas_ra_get_capabilities()
       (drivers/can/can_renesas_ra.c:413) advertises only
       NORMAL|LOOPBACK|FD|MANUAL_RECOVERY, and can_set_mode() rejects any bit
       outside the capability mask.
    => a SINGLE can_send() becomes an unbounded error storm, not a one-time
       error a status reset would clear.
- FIX APPLIED (apps/can_logger/src/main.c): the boot self-transmit is now a
  CLASSIC frame (flags = 0), guarded by a new `#define BOOT_TX_USE_FD 0`.
  Classic frames remain valid from an FD controller in FD mode, so the
  board->analyser TX test stayed possible. **Set BOOT_TX_USE_FD to 1 the moment
  FD-capable gear is available** — FD/BRS is a real project requirement.
- DESIGN CONSEQUENCE TO REMEMBER: no CAN_MODE_ONE_SHOT on this driver is
  permanent. Any TX onto a bus that might not ACK (wrong bitrate, no peer,
  incompatible node) produces a sustained storm rather than one clean failure.

---

## 3b. ZEPHYR VERSION: PULLED BY ACCIDENT, ROLLED BACK. USB BUG IS UPSTREAM TOO.
On 2026-08-31 the zephyr repo was accidentally `git pull`ed, moving it 150
commits from our validated snapshot. It has been ROLLED BACK. Details:
- **VALIDATED COMMIT (the one to be on): `f80761e4940`** — "net: shell: fix
  -Wuninitialized-const-pointer warnings". Independently corroborated: it is
  the `f80761e49401` build hash printed in EVERY boot banner in today's
  hardware results.
- The accidental pull landed on `66e5135ffc3`. Recovered from `git reflog`
  (HEAD@{1} = the original clone point).
- CURRENT STATE: zephyr is at `f80761e4940` on a **DETACHED HEAD**, tree clean.
  This is deliberate, so nothing is lost in either direction:
      go forward to the pulled state:  git -C zephyr checkout main   (66e5135ffc3)
      return to the validated state:   git -C zephyr checkout f80761e4940
- `west update` was NOT run and modules were NOT touched. hal_renesas is pinned
  to the SAME revision (f2eb9bc) in both manifests, so the workspace is
  consistent as-is.

### KEY FINDING: upstream has NOT fixed the RA USB-HS bug
Before rolling back, the 150 new commits were checked for anything that could
matter. NOTHING relevant changed:
      drivers/usb/udc/udc_renesas_ra.c                  0 commits
      subsys/usb/device_next/class/usbd_cdc_acm.c       0 commits
      all of subsys/usb/ + drivers/usb/                 0 commits
      soc/renesas, boards/renesas/ek_ra8d1,
        dts/arm/renesas, drivers/can/can_renesas_ra.c   0 commits
`git diff --name-only f80761e4940..HEAD` matched ZERO files containing
"renesas" or "ra8". The USB code is byte-identical between the two revisions.
=> Updating Zephyr CANNOT fix the Step 3 USB-HS bulk-transfer bug, and the bug
   is present in current mainline, not just our pinned snapshot. Do not try
   "just update Zephyr" again for this problem without first re-running that
   same `git log <range> -- drivers/usb/ subsys/usb/` check.
=> Rolling back also avoided 150 commits of mainline churn against CAN/GPIO/
   flash results that had just been validated on hardware (the "mainline can
   break between pulls" risk already in the decisions log).

### Post-rollback verification (2026-08-31)
All four apps rebuilt PRISTINE (`-p always`) against the restored tree, all
exit 0, and three of four match their originally-recorded Phase 2 sizes
EXACTLY — strong evidence the rollback restored the validated tree:
      can_logger  45428 B  (2.20%)  = 45436 - 8 B, explained by our
                                      classic-boot-frame change (BOOT_TX_USE_FD 0)
      gpio_timer  38012 B  (1.84%)  identical to Phase 2B record
      usb_cdc     70924 B  (3.44%)  identical to Phase 2C record
                                    (MAIN_STACK_SIZE is RAM, not FLASH)
      flash_log   72500 B  (3.51%)  identical to Phase 2D record

### DECISION: stay on f80761e4940; use the J-Link VCOM/UART as the GUI transport
CLP is transport-agnostic (byte stream in, byte stream out) and its self-test
passes on hardware every boot, so GUI work is NOT blocked — only the USB
transport is. Revisit USB when upstream fixes the RA UDC bulk path, or try the
Full-Speed (usbfs) path, which is a different UDC route.

---

## 4. TOOLING NOTES LEARNED TODAY (save the next session hours)
- **Serial console capture**: apps print their banner ONCE at boot, so the
  reader must ALREADY be attached when the board resets. Flashing first and
  opening the port afterwards captures nothing and looks exactly like a dead
  console — it is not. Helpers written this session live in the scratchpad:
  `capreset.py` (open COM12 -> J-Link reset -> capture) and `capboot.py <secs>`
  (plain long capture). Re-create them if the scratchpad is gone; they are ~20
  lines of pyserial.
- **Never pipe `west flash` through grep and read `$?`** — you get grep's exit
  status and will misreport a failure as success. Redirect to a file instead.
- **One J-Link session at a time.** A capture script holding its own JLink
  session collides with `west flash` and produces a confusing FATAL ERROR.
- **`west flash -r pyocd` needs .venv/Scripts on PATH** ("required program
  pyocd not found" otherwise) — west does not find its own venv's scripts.
- **pyocd needed a CMSIS pack**: R7FA8D1BH is not in the built-in target list.
  `pyocd pack install R7FA8D1BH` (Renesas.RA_DFP 6.5.1) fixed it; target is now
  `r7fa8d1bh`. Worth keeping — pyocd's precise error messages are far better
  than J-Link's and are what localised the flash boundary today.
- **pyocd always prints a harmless SVD-parser traceback** ending in
  "AttributeError". A grep for /error/i therefore matches EVERY run and reports
  100% failure, INCLUDING sectors that erased fine. This produced a completely
  false sector map before it was caught. Judge pyocd by EXIT CODE, or grep the
  specific string "flash erase.*failure".

## Previous phase
Phase 4 — TrustZone feasibility spike COMPLETE 2026-08-30 (research only, no
driver code written). VERDICT: NO-GO on TrustZone — now a locked decision in
CLAUDE.md's hard constraints, not an open question. Do not revisit unless the
user explicitly asks. Full findings: docs/trustzone_feasibility.md.
SECURITY DIRECTION CHOSEN INSTEAD: RSIP-E51A hardware-wrapped-key AES-128 CMAC,
already compiled into our ek_ra8d1 builds. Locked in for a future SecOC-style
phase, NOT implemented, NOT yet green-lit for coding.

NEXT PHASE: Phase 5 hardware bring-up — the board and PCAN arrive tomorrow
(2026-08-31). Plan: docs/bringup_checklist.md. NO new Phase 2/3 code until
bring-up has run; everything in apps/ is build-only and unproven on silicon,
and the point of tomorrow is to find out which parts actually work.

Phase 3 — MCUboot bootloader via Zephyr sysbuild COMPLETE 2026-08-29
(build-only). Built against apps/can_logger. `west build -b ek_ra8d1
apps/can_logger --sysbuild` produces a signed, MCUboot-bootable app image
(slot0) + the MCUboot image.

## Phase 4 summary (2026-08-30) — TrustZone feasibility, research only
Full report: docs/trustzone_feasibility.md. Nothing was written to apps/ or
boards/; the only build artifacts are two throwaway spike dirs (build/tz_spike,
build/tz_spike2). Headlines:
- VERDICT: NO-GO on TrustZone. Multi-month upstream SoC-enablement effort, and
  it cannot be validated without the physical board + Renesas Flash Programmer
  (the S/NS boundary is programmed into the DEVICE, not the image).
- CLAUDE.md CORRECTION NEEDED (not applied, needs user OK): the hard-constraint
  bullet says "no SAU binding" — that is WRONG. soc/renesas/ra/ra8d1/Kconfig:9
  selects CPU_HAS_ARM_SAU -> CPU_HAS_TEE -> ARMV8_M_SE, so ARM_TRUSTZONE_M is
  reachable. The "/ns variant" and "supported-features" halves of the bullet
  are correct and should stay.
- PROVEN BY BUILD (this session, build-only, never flashed): hello_world with
  -DCONFIG_ARM_TRUSTZONE_M=y -DCONFIG_TRUSTED_EXECUTION_SECURE=y BUILDS AND
  LINKS CLEANLY for ek_ra8d1 (FLASH 28268 B vs 27352 B baseline). This is a
  TRAP, not a win: arch/arm/core/cortex_m/tz/ has NO .c file, and there is zero
  SAU region-programming code in arch/arm/core/ or soc/renesas/. The image
  would boot secure-marked with the SAU never configured = no real isolation.
  (Passing TRUSTED_EXECUTION_SECURE alone is silently DROPPED from .config —
  it is gated behind ARCH_HAS_TRUSTED_EXECUTION, selected only if
  ARM_TRUSTZONE_M.)
- No Renesas board in Zephyr has a /ns variant (all 11 RA8 board.yml checked).
- No TF-M port exists for ANY Renesas device (modules/tee/tf-m/.../target/ has
  adi arm armchina cypress infineon nordic_nrf nuvoton nxp rpi stm — no
  renesas). The standard Zephyr TrustZone route is closed, not just unconfigured.
- The RA "TrustZone support" that DOES exist (CPU_HAS_RENESAS_RA_IDAU, ra6m4 +
  ra6m5 only) is NOT a S/NS split — it is CONFIG_OUTPUT_RPD + gen_rpd.py
  emitting a partition file so Ethernet DMA buffers land in non-secure RAM,
  applied off-target with rfp-cli. Single image, no secure world. ra8d1 does
  not select it.
- Upstream appetite: GitHub search "ra8 trustzone" in zephyrproject-rtos/zephyr
  = 0 results. The only relevant PR, #101704 "soc: renesas: ra4m2: add
  TrustZone/IDAU support" (67 lines, hardware-tested), was CLOSED UNMERGED
  2026-04-28. Nobody upstream is working on ek_ra8d1 TrustZone/secure boot.
- Silicon-vs-Zephyr gaps (all absent from Zephyr; datasheet cites in the doc):
  CPSCU, PSCU (the unit that assigns per-peripheral S/NS attribution — this is
  the blocking gap), RMPU, FDFS, MSAU, DOTF, and any model of the S-TYPE/P-TYPE
  register rules. Also: every RA8 DT node uses the SECURE alias (0x4xxx_xxxx);
  `grep "reg = <0x5"` over dts/arm/renesas/ra/ra8/ returns ZERO matches, so a
  non-secure world would have no peripheral nodes to bind to.
- HAZARD worth remembering: per datasheet Table A4.1/A4.2 (p.189), S-TYPE-1/-2/-4
  and P-TYPE-1/-4 registers IGNORE disallowed writes with NO TrustZone access
  error. A misconfigured NS driver fails invisibly — no fault, no log.
- PDF scope correction: security_and_trustzone_research.pdf is Appendices 3-5
  (pp.184-190) of the RA8D1 datasheet R01DS0416EJ0130 Rev.1.30 ONLY. It does
  NOT contain the RSIP-E51A register map (RSIP appears once, in Table A3.2
  p.188, with BLANK address columns, and is absent from Table A3.1 entirely)
  and does NOT mention a HUK. The "256-bit HUK" figure is UNVERIFIED — Renesas'
  public page says 128-bit unique ID. Resolve against the User's Manual:
  Hardware security chapter before citing it in the Phase 6 report.
- FSP version: our hal_renesas vendors FSP 6.2.0; upstream is 6.5.1. The newer
  r_rsip / r_rsip_protected driver is NOT in our tree (no r_rsip* dir). What we
  have is the legacy r_sce driver with rsip_e51a procedures under it.

## Phase 4 ANSWER to the Phase 3 SecOC question — RSIP IS reachable (revises
## the pessimistic note below)
Q: is the RA RSIP/SCE security engine exposed in modules/hal_renesas for
R7FA8D1BH? YES — and it is already compiled into our EXISTING builds. Proof
from our own Phase 2 artifacts, not a new build:
  build/eth_doip/zephyr/misc/generated/configs.c:198  CONFIG_USE_RA_FSP_SCE=1
  build/eth_doip/zephyr/misc/generated/configs.c:199  CONFIG_HAS_RENESAS_RA_RSIP_E51A=1
- bsp_peripheral.h:158 BSP_PERIPHERAL_RSIP_PRESENT(1); bsp_feature.h:480
  BSP_FEATURE_RSIP_RSIP_E51A_SUPPORTED(1UL) (all other engines 0UL).
- hal CMakeLists.txt:169-177 compiles 235 rsip_e51a primitive source files.
- ALREADY WORKING via a standard Zephyr API: the hardware TRNG. ra8x1.dtsi:487
  trng node (renesas,ra-rsip-e51a-trng), ek_ra8d1.dts:28 zephyr,entropy=&trng,
  :294 status okay, drivers/entropy/entropy_renesas_ra.c calls HW_SCE_RNG_Read.
- COMPILED IN but with no Zephyr API on top (r_sce_if.h, 1722 lines):
  HW_SCE_Aes128CmacGenerate/Verify Init/Update/Final (:1475-1486) taking an
  sce_aes_key_index_t (NOT a raw key), AES-GCM (:1280+),
  HW_SCE_GenerateAes128RandomKeyIndex (:985), GenerateAes128PlainKeyIndex
  (:1007), ECC P-192..384 + RSA 1024..4096 key-index gen (:907-998).
- SO: the "MCUboot -> SecOC" note below is TOO PESSIMISTIC where it says a real
  key store "would need TrustZone". AES-128 CMAC on a HARDWARE-WRAPPED key index
  is available today. The unwrapped key never lands in CPU-addressable RAM.
  Weaker than TrustZone in one respect only: any on-device code can USE the key
  index to compute MACs. But the "secret sits in plain flash/RAM" problem that
  motivated wanting TrustZone is solved, for days of work instead of months.
- Glue needed = NONE at the build level. grep -rl "HW_SCE_" over zephyr/drivers
  + zephyr/subsys returns exactly ONE file (entropy_renesas_ra.c). There is no
  drivers/crypto RA driver and no PSA/mbedTLS accelerator binding, so app code
  would call HW_SCE_* directly — headers and objects are already on the include
  path and in the link. No new Kconfig, no HAL patch, no DT change.
- Cheap future win also logged: DOTF (decryption-on-the-fly) would encrypt the
  Phase 2D OSPI log store at rest in hardware. FSP implements it (73 refs in
  r_ospi_b.c) but Zephyr's renesas,ra-ospi-b binding exposes no DOTF property.

## Phase 3 summary (2026-08-29) — MCUboot + sysbuild, build-only
Image built against: apps/can_logger (the CAN-FD image; small + mission-
central). Zero changes to any Phase 2 file — the sysbuild wiring is all new
files under apps/can_logger/, and only takes effect with `--sysbuild`; a
plain `west build apps/can_logger` still builds exactly as in Phase 2A.
New files:
- apps/can_logger/sysbuild.conf                    (SB_CONFIG_* for MCUboot)
- apps/can_logger/sysbuild/mcuboot_partitions.dtsi (flash0 partition table)
- apps/can_logger/sysbuild/can_logger.overlay      (app image: partitions +
  chosen zephyr,code-partition = &slot0_partition)
- apps/can_logger/sysbuild/mcuboot.overlay         (bootloader image: partitions)

Compatibility check (done against THIS checkout, not assumed):
- bootloader/mcuboot present; sysbuild exposes SB_CONFIG_BOOTLOADER_MCUBOOT
  (zephyr/share/sysbuild/images/bootloader/Kconfig).
- No RA8D1 board ships an MCUboot partition map, BUT sibling Cortex-M85 RA8
  boards do (ek_ra8t2/d2/m2/p1) and ek_ra8t2/doc/index.rst documents the exact
  `--sysbuild -DSB_CONFIG_BOOTLOADER_MCUBOOT=y` flow -> the toolchain path is
  supported for this core.
- ek_ra8d1 code flash (flash0, renesas,ra-nv-code-flash, 2016 KiB @ 0x02000000)
  driver = soc_flash_renesas_ra_hp.c; Kconfig.renesas_ra explicitly lists
  SOC_SERIES_RA8D1, provides FLASH_PAGE_LAYOUT + FLASH_HAS_EXPLICIT_ERASE.
  flash0 erase geometry: 8x8K blocks (0..0x10000) then 61x32K (0x10000..).
- BLOCKER FOUND: flash0 write-block-size = 128. CONFIG_MCUBOOT_BOOT_MAX_ALIGN
  defaults to that (zephyr/modules/Kconfig.mcuboot:433). MCUboot
  boot/bootutil/include/bootutil/bootutil_public.h has
    _Static_assert(MCUBOOT_BOOT_MAX_ALIGN >= 8 && <= 32,
      "Unsupported value for MCUBOOT_BOOT_MAX_ALIGN for SWAP upgrade modes")
  guarded by #if SWAP_USING_MOVE || SWAP_USING_SCRATCH || SWAP_USING_OFFSET.
  The sysbuild DEFAULT mode is SWAP_USING_OFFSET -> would fail to compile.
- RESOLUTION: SB_CONFIG_MCUBOOT_MODE_OVERWRITE_ONLY (BOOT_UPGRADE_ONLY).
  That mode is outside the #if, so align=128 is accepted. Verified in
  build/.../mcuboot/zephyr/.config: CONFIG_MCUBOOT_BOOT_MAX_ALIGN=128,
  CONFIG_BOOT_UPGRADE_ONLY=y, build clean.
  Trade-off: gives image auth + versioning + downgrade prevention
  (MCUBOOT_BOOTLOADER_MODE_HAS_NO_DOWNGRADE=y on the app), but NOT
  swap-back / auto-revert on a bad update (no scratch/move). "Rollback" here
  = re-flash / push a higher-versioned good image, not automatic A/B revert.
  A true swap mode would need the 128-byte write alignment problem solved
  (e.g. slots in the external OSPI NOR, write-block-size 1 — bigger redesign).

Partition layout (apps/can_logger/sysbuild/mcuboot_partitions.dtsi, on &flash0,
all boundaries 32K-aligned so each slot is erase-block aligned):
  boot_partition  0x00000  128 KiB  (mcuboot)
  slot0_partition 0x20000  928 KiB  (image-0 / primary)
  slot1_partition 0x108000 928 KiB  (image-1 / secondary)
  0x1F0000..0x1F8000 (32 KiB) left unallocated.
flash1 (data-flash storage_partition, 12K) and the OSPI NOR (Phase 2D
littlefs canlog store) are untouched.

Signing: SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256 (smaller verify path than
RSA-2048 on M85). Currently uses the DEBUG key that ships with the module
(bootloader/mcuboot/root-ec-p256.pem) — build logs the expected
"WARNING: Using default MCUboot signing key file, this file is for debug use".
A real project key = generate a P-256 keypair, point
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE at it, keep the private key offline.

Build: `.venv\Scripts\west build -b ek_ra8d1 apps/can_logger
 -d build/can_logger_mcuboot --sysbuild -p always` -> exit 0.
 [16/16] Completed 'mcuboot' + 'can_logger'. No compiler warnings; the only
 Kconfig warnings are benign (debug-key notice; MCUBOOT_UPDATE_FOOTER_SIZE
 unmet-dep in the *bootloader's* parse where IMG_MANAGER=n, irrelevant to it;
 Windows path-slash "Rebasing domain build directories" cosmetic notice).
 Artifacts:
 - build/can_logger_mcuboot/mcuboot/zephyr/zephyr.{elf,hex}  (FLASH 39024 B
   linked view; fits the 128K boot_partition. RAM 20704 B.)
 - build/can_logger_mcuboot/can_logger/zephyr/zephyr.signed.{bin,hex}
   (app: FLASH 46116 B of the 949482 B usable slot0 = 4.86%; RAM 8792 B;
   CONFIG_FLASH_LOAD_OFFSET=0x20000, ROM_START_OFFSET=0x200 imgtool header.)
 NO merged.hex / no flash (no board; overwrite-only + no runner config).
 NEVER booted — MCUboot chainload, signature verify, slot logic all
 unverified without hardware.
GOTCHA hit + resolved this session: running two `west build` on the same -d
 dir concurrently causes "The process cannot access the file ... used by
 another process" on .obj.d depfiles (looks like the AV lock gotcha but was
 self-inflicted). One build at a time per build dir.

## MCUboot -> SecOC stepping-stone note (Phase 3, answers the phase question)
Can this MCUboot setup later carry a signing key toward the SecOC-style CAN
message authentication we discussed, even without TrustZone? PARTIALLY —
it is a necessary-but-not-sufficient step:
- REUSABLE now: the key-management + build-time signing flow
  (SB_CONFIG_BOOT_SIGNATURE_KEY_FILE, imgtool, offline private key, public
  key compiled into MCUboot). Swapping the debug PEM for a real project key
  is a one-line config change.
- What MCUboot gives toward SecOC: a verified-boot anchor, so whatever code
  provisions/holds a SecOC key runs from an image MCUboot authenticated
  (ECDSA-P256). "Don't run unauthenticated firmware" is a precondition for
  trusting any in-firmware key store. MCUboot also has encrypted-image
  support and the board has &trng + &crc for the crypto primitives.
- What it does NOT give: SecOC (AUTOSAR/ISO) uses a SYMMETRIC per-message MAC
  (CMAC/HMAC) with a freshness counter and a shared secret provisioned per
  ECU — a different key TYPE and a RUNTIME key, not the asymmetric
  boot-verification key. The MCUboot ECDSA key does not become the SecOC key.
- Without TrustZone the runtime SecOC secret still sits in normal flash/RAM
  behind only the ARMv8.1-M MPU. MCUboot doesn't change that. A real secure
  key store would need TrustZone (not on ek_ra8d1 mainline), the RA MCU
  security engine (RSIP/SCE key-wrapping — not currently exposed in the
  Zephyr HAL for this part, needs a Phase 4 check), or an external secure
  element.
- Practical path: Phase 3 MCUboot = trusted boot + firmware auth/versioning.
  A later SecOC phase reuses the key-handling discipline, runs its key
  provisioning from a verified image, and stores a wrapped SecOC key in the
  flash1 storage_partition; MPU-isolate the SecOC task.

## Phase 2E summary (2026-08-29) — apps/eth_doip, build-only
On-chip Ethernet (static IP 192.168.1.50) + a minimal DoIP (ISO 13400-2)
responder skeleton for pulling CAN logs off the board. Independent of 2A-2D
(byte buffers + plain structs only). Full spec: docs/doip_skeleton.md. Files:
- apps/eth_doip/CMakeLists.txt, prj.conf   (no overlay - eth stack is stock)
- apps/eth_doip/include+src/doip.*        (transport-free DoIP + UDS logic)
- apps/eth_doip/include+src/doip_server.* (UDP + TCP socket server threads)
- apps/eth_doip/src/main.c                (transport-free DoIP self-test + demo)
Verified against workspace + build/eth_doip/zephyr/{.config,zephyr.dts} AND
the user's board-manual Table 24 (Ethernet port assignments) / SW1 table:
- &eth (renesas,ra-ethernet @40354100, IRQ 42, RMII), &mdio, ethernet-phy@5
  all status=okay in stock ek_ra8d1.dts. Driver eth_renesas_ra.c (DT_DRV_COMPAT
  renesas_ra_ethernet); CONFIG_ETH_RENESAS_RA default y via
  DT_HAS_RENESAS_RA_ETHERNET_ENABLED. PHY = generic MII (CONFIG_PHY_GENERIC_MII;
  ICS1894-32). NB the RA8x2 renesas,ra-ethernet-rmac driver is NOT ours.
  zephyr.elf has eth_renesas_ra + ether_phy_mii_* linked.
- Pin-mux: decoded ether_default/mdio_default in ek_ra8d1-pinctrl.dtsi ->
  MDC=P401 MDIO=P402, RMII on P403/P405/P406/P700..P705. This is the board's
  "ETHERNET B" mux (manual Table 24, SW1-5 ON / SW1-4 OFF) - matches the user's
  table exactly. CANFD0 (P202/P203) and all other Phase 2 app pins were
  re-checked against BOTH ethernet mux columns + SDRAM: no collisions.
- CONFLICT FOUND + FIXED: stock ek_ra8d1.dts also enables &i3c0, whose
  i3c0_default claims I3C0_SCL=P400 + I3C0_SDA=P401 - and P401 is ET0_MDC.
  Zephyr RA pinctrl has no build-time conflict check, so on hardware the
  last driver to init wins P401 and the other peripheral dies silently.
  apps/eth_doip/boards/ek_ra8d1.overlay now does &i3c0 { status="disabled"; }
  (project uses no I3C). zephyr.dts confirms i3c0 disabled. Camera &ceu is
  already status=disabled in stock dts (no action needed).
- Static IPv4 (no DHCP) via CONFIG_NET_CONFIG_SETTINGS +
  CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.1.50" / NETMASK 255.255.255.0 /
  GW 192.168.1.1 (user-provided IP). IPv6 off. Applied at boot; needs PHY
  link up on real hw.
- Sockets: zsock_* API from <zephyr/net/socket.h> (no CONFIG_POSIX_API).
  CONFIG_NET_{IPV4,UDP,TCP,SOCKETS,L2_ETHERNET}=y, NET_MAX_CONTEXTS=8,
  NET_MAX_CONN=8, ZVFS_OPEN_MAX=8, pkt/buf pools 16/32, ENTROPY_GENERATOR=y
  (TCP ISN; board &trng). Every symbol Kconfig-parsed clean.
- doip.c: 8-byte DoIP header (ver 0x02, ~ver, payload-type u16 BE, len u32 BE).
  doip_handle_udp: Vehicle Identification Req (0x0001/2/3) -> Vehicle
  Announcement 0x0004 (VIN 17 + LA 2 + EID 6 + GID 6 + FAR 1). doip_handle_tcp:
  Routing Activation Req 0x0005 -> Resp 0x0006 (code 0x10 success if tester SA
  in 0x0E00..0x0FFF); Alive Check 0x0007 -> 0x0008; Diag Message 0x8001 ->
  0x8002 pos-ack THEN 0x8001 carrying the UDS response (0x8003 neg-ack if not
  routing-active or wrong TA). Entity logical addr 0x1234 (placeholder).
- UDS placeholder "log download" flow: 0x22 RDBI (DID 0xF190 VIN, 0xFD00
  log-store status), 0x35 RequestUpload -> 0x75 (maxBlockLen 1024), 0x36
  TransferData -> 0x76 + placeholder chunk, 0x37 -> 0x77, else 0x7F.. NRC 0x11.
  All responses are stubs - a real build wires them to the Phase 2D
  /lfs/log_*.clb store.
- doip_server.c: udp_thread (recvfrom :13400 -> doip_handle_udp -> sendto) and
  tcp_thread (accept :13400, per-conn: recv 8-byte hdr, recv payload_len,
  doip_handle_tcp, send). Started by doip_server_start() (k_thread_create,
  3K stacks). main.c waits 100ms, logs the configured IP, starts the server.
- main.c self-test (build-only logic, runs on hw only): builds a Routing
  Activation Req + a Diag Message with UDS RDBI(0xF190), feeds them through
  doip_handle_tcp, asserts 0x0006/success + 0x8002 + 0x8001/0x62F190/VIN.
Build: west build -b ek_ra8d1 apps/eth_doip -d build/eth_doip -p always ->
 exit 0, 0 warnings, [461/461] linked zephyr.elf. FLASH 112016 B (5.43%),
 RAM 57720 B (6.29%). NOT flashed (no board); no live network this session.
HW reminders (unverifiable without board):
 - SW1-5 (ETHERNET B) = ON; SW1-4 (ETH-A) = OFF; SW1-3 (CAMERA) = OFF;
   SW1-8 (I3C) = OFF. SW1-7 (SDRAM, ON by default) does NOT conflict with
   ETH-B (only ETH-A conflicts with SDRAM) -> flash_log's SDRAM option and
   eth_doip can coexist.
 - PHY RESET_N (P706 on the ETH-B mux) is NOT wired in the stock dts
   ethernet-phy@5 node (no reset-gpios). ICS1894-32 may rely on power-on
   reset; if the PHY won't link, first thing to try is adding
   reset-gpios = <&ioport7 6 GPIO_ACTIVE_LOW> to the phy node.
 - Ethernet RMII 50 MHz ref clock is an external oscillator into REF50CK0
   (P701) - hardware, nothing to configure.
Did NOT touch CAN / GPIO / USB / flash (phase scope).

## Phase 2D summary (2026-08-29) — apps/flash_log, build-only
littlefs on the on-board 64 MB Octo-SPI NOR + an SRAM-buffered, batch-writing
CAN-frame logger. Independent of the 2A/2B/2C modules (plain frame struct in).
Files:
- apps/flash_log/CMakeLists.txt, prj.conf
- apps/flash_log/boards/ek_ra8d1.overlay  (re-partitions &s28hl512t + fstab node)
- apps/flash_log/include+src/flash_log.*
- apps/flash_log/src/main.c  (mount->start->200 synthetic frames->flush->stop)
Verified against workspace + generated build/flash_log/zephyr/{zephyr.dts,.config}:
- Flash: &ospi0 (renesas,ra-ospi-b) -> &s28hl512t (renesas,ra-ospi-b-nor,
  64 MB @ 0x90000000, write-block-size=1), both status=okay in stock board.
  Driver flash_renesas_ra_ospi_b.c implements erase/write/read/get_parameters/
  page_layout/jedec-id for the S28HX512T. CONFIG_FLASH_RENESAS_RA_OSPI_B is
  default y via DT_HAS_RENESAS_RA_OSPI_B_NOR_ENABLED; selects
  FLASH_HAS_PAGE_LAYOUT + FLASH_HAS_EXPLICIT_ERASE.
- Erase geometry (board pages_layout): 0..0x20000 = 4K sectors, 0x20000..
  0x40000 = one 128K, 0x40000..0x4000000 = 256K sectors. Zephyr littlefs uses
  the LARGEST page size overlapping the partition and assumes it works across
  the whole partition (subsys/fs/littlefs_fs.c get_block_size). => overlay
  puts the "canlog" partition at 0x40000..0x4000000 (63.75 MB), entirely in
  the uniform 256K-sector region; first 256K left free for future NVS.
- Overlay: &s28hl512t { /delete-node/ partitions; ... canlog_part:
  partition@40000 reg=<0x40000 0x3FC0000> }; plus a /fstab/lfs_canlog node
  (compatible zephyr,fstab,littlefs, mount-point /lfs, partition=<&canlog_part>,
  read/prog=16 cache=64 lookahead=32 block-cycles=512, NO automount).
  zephyr.dts confirms canlog_part + lfs_canlog present, old "nor" partition
  gone. subsys/fs/littlefs_fs.c generates struct fs_mount_t
  FS_FSTAB_ENTRY(lfs_canlog) + fs_data_0 unconditionally per instance;
  flash_log.c mounts it via FS_FSTAB_DECLARE_ENTRY + fs_mount().
- Kconfig (verified): CONFIG_FLASH, FLASH_MAP, FLASH_PAGE_LAYOUT,
  FLASH_RENESAS_RA_OSPI_B, FILE_SYSTEM, FILE_SYSTEM_LITTLEFS (needs the
  modules/fs/littlefs module - present), FS_LITTLEFS_FMP_DEV (auto y),
  RING_BUFFER. MAIN_STACK_SIZE=8192 (fs structs + a 1K drain buf on the main
  thread in flash_log_stop). littlefs west module at modules/fs/littlefs.
- Batching: flash_log_record() serialises a compact LE record and ring_buf_put
  into an 8 KB SRAM ring (rb_lock spinlock-guarded, ISR-safe for a future CAN
  RX producer). A dedicated writer thread (K_THREAD_DEFINE, 6K stack, prio
  PREEMPT(7)) drains the ring to the open file in <=1 KB fs_write() batches;
  wakes on a k_sem (given when ring half-full or on flush) or every 500 ms.
  flash_log_flush() -> writer drains + fs_sync (the Phase 2B periodic-timer
  hook). flash_log_stop() drains remainder synchronously under file_lock,
  fs_sync, fs_close. Lossy-by-design: full ring -> drop + count, never blocks
  the caller.
- On-disk format "CLB1": 16-byte file header (magic/ver/hdrlen/start_ticks)
  then variable records: marker 0xC5, flags(CAN_FRAME_* bits), dlc, n, id(u32),
  timestamp(u64), data[n]. Documented in flash_log.c header comment.
- SDRAM: user confirmed the 64 MB SDRAM IS populated (SW1-7 ON by default) and
  the OSPI flash IS populated (U3 S28HL512TFPBHI010). Logger still uses on-chip
  SRAM per the decisions log (SDRAM = documented future option: bump
  FLASH_LOG_RING_SIZE, place ring in the SDRAM region). OSPI flash defaults to
  XIP after power-on; our app runs from internal flash0 so the OSPI chip is
  pure data - the driver reconfigures it on init.
Build: west build -b ek_ra8d1 apps/flash_log -d build/flash_log -p always ->
 exit 0, 0 warnings, [176/176] linked zephyr.elf. FLASH 72500 B (3.51%),
 RAM 33688 B (3.67%). flash_log_* + flash_renesas_ra_ospi_b_* (erase/write/
 read/page_layout/init) + littlefs_open/write + fs_data_0 all linked. littlefs
 NEVER actually mounted this session (no board). NOT flashed.
Did NOT touch CAN / GPIO / USB / Ethernet (phase scope).

## Phase 2C summary (2026-08-29) — apps/usb_cdc, build-only
CDC-ACM device (USB device stack "next") + "CLP" (CAN Log Protocol) v1 binary
framing for the board<->GUI link. Independent of the 2A/2B modules (deals only
in plain structs). Protocol spec: docs/clp_protocol.md. Files:
- apps/usb_cdc/CMakeLists.txt, prj.conf
- apps/usb_cdc/boards/ek_ra8d1.overlay  (adds cdc_acm_uart0 under &zephyr_udc0)
- apps/usb_cdc/include+src/clp_proto.*  (encode/decode + streaming parser)
- apps/usb_cdc/include+src/usb_link.*   (CDC-ACM irq I/O + CLP glue)
- apps/usb_cdc/include+src/usbd_ctx.*   (USB device context)
- apps/usb_cdc/src/main.c  (pure CLP round-trip self-test + USB demo)
Verified against workspace (not memory):
- USB device = usbhs (renesas,ra-usbhs, HS). zephyr_udc0 = its `udc` child
  (compatible renesas,ra-udc), status=okay in stock ek_ra8d1.dts. usbfs is NOT
  enabled by the board. CONFIG_UDC_RENESAS_RA (drivers/usb/udc/udc_renesas_ra.c)
  is default y via DT_HAS_RENESAS_RA_UDC_ENABLED; selects HS support ->
  CONFIG_USBD_MAX_SPEED_HIGH.
- Current cdc_acm sample uses CONFIG_USB_DEVICE_STACK_NEXT + a
  `zephyr,cdc-acm-uart` DT node exposed as an interrupt UART. usbd_ctx.c is
  templated from samples/subsys/usb/common/sample_usbd_init.c (its header says
  copy, don't depend).
- CONFIG_USBD_CDC_ACM_CLASS: default y once a zephyr,cdc-acm-uart node exists;
  selects RING_BUFFER + UART_INTERRUPT_DRIVEN.
- COBS: CONFIG_COBS, <zephyr/data/cobs.h> streaming encoder/decoder (no NET_BUF
  needed). CRC: CONFIG_CRC (subsys/crc), crc16_ccitt(). CONFIG_CRC_HW_HANDLER
  forced off (board has chosen zephyr,crc=&crc -> would pull an unused driver;
  we use the SW crc16_ccitt).
CLP v1 wire format (see docs/clp_protocol.md for the full spec):
  COBS( ver(1) type(1) seq(1) len(u16) payload[len] crc16(u16) ) + 0x00
  - COBS+0x00 self-synchronising framing (not a magic byte: 0xA5 occurs in CAN
    data). CRC-16/X-25 (crc16_ccitt(0xFFFF,..)^0xFFFF), MAVLink/HDLC family.
  - Types: CAN_RX 0x01, CAN_TX 0x02, CAN_TX_ACK 0x03, STATUS 0x04, HELLO 0x05.
  - CAN payload = can_id(u32) flags(u8: FDF|BRS|ESI|IDE|RTR, matches Zephyr
    CAN_FRAME_* / Phase 2A canlog_frame.flags) dlc(u8) tag(u16) timestamp(u64)
    data[can_dlc_to_bytes(dlc)]. Variable length: 8-byte frame ~31 wire bytes
    vs a fixed-80 struct.
  - seq is per-direction rolling -> GUI detects loss. Device drops (never
    blocks CAN RX) on USB backpressure and counts it (STATUS.rx_drops).
  - HELLO sent on DTR (host opens port).
- usb_link.c: outbound byte ring buf (2 KB) drained by the CDC-ACM TX irq,
  spinlock-guarded, lossy-by-design. Inbound: cdc irq -> clp_parser_feed (fed
  1 byte at a time so a COBS violation drops exactly the corrupt run and
  resyncs on the next 0x00). CLP_MSG_CAN_TX -> registered handler.
- VID/PID = Zephyr test values 0x2fe3/0x0001, flagged PLACEHOLDER in usbd_ctx.c
  (user chose this over allocating one).
Build: west build -b ek_ra8d1 apps/usb_cdc -d build/usb_cdc -p always -> exit 0,
 0 warnings, [214/214] linked zephyr.elf. FLASH 70924 B (3.44%), RAM 20516 B
 (2.24%). zephyr.dts shows cdc_acm_uart0 under usbhs/udc. .config confirms
 USB_DEVICE_STACK_NEXT / UDC_RENESAS_RA / USBD_CDC_ACM_CLASS / COBS / CRC.
 udc_renesas_ra_api, cobs_decoder_write, crc16_ccitt, all clp_*/usb_link_*
 symbols linked. main.c's CLP encode->parse self-test is build-only logic
 (would run on hw). NOT flashed (no board).
Did NOT touch CAN / flash / Ethernet / USB-host / mass-storage (phase scope).

## Phase 2B summary (2026-08-29) — apps/gpio_timer, build-only
Two independent modules + a demo main that wires them (kept decoupled so each
builds/tests alone; NOT wired to the Phase 2A CAN module). Files:
- apps/gpio_timer/CMakeLists.txt, prj.conf  (no overlay — all nodes stock)
- apps/gpio_timer/include/io_control.h, src/io_control.c
- apps/gpio_timer/include/log_timer.h, src/log_timer.c
- apps/gpio_timer/src/main.c
All DT facts verified against zephyr sources + the generated
build/gpio_timer/zephyr/zephyr.dts (not from memory):
- Buttons: stock "buttons" gpio-keys node. button0=s1=P009 (ioport0.9),
  button1=s2=P008 (ioport0.8), both GPIO_PULL_UP|GPIO_ACTIVE_LOW. Matches
  the user's board table (S1 P009 IRQ13-DS, S2 P008 IRQ12-DS).
- LEDs: stock "leds" gpio-leds node, all GPIO_ACTIVE_HIGH. led1=P600
  (ioport6.0), led2=P40E (ioport4.14), led3=P107 (ioport1.7). Colour<->
  designator map CONFIRMED 2026-08-30 against the EK-RA8D1 User's Manual,
  Table 22 — io_control.c's led2=green(idle), led3=red(blink while logging)
  is correct as written. LED_*_NODE macros still make it a one-line change if
  the physical silkscreen disagrees at bring-up.
- Interrupt-driven buttons: GPIO_INT_EDGE_TO_ACTIVE (falling edge = press),
  gpio_add_callback_dt + one shared gpio_callback on ioport0. The RA ioport
  driver (drivers/gpio/gpio_renesas_ra_ioport.c) needs
  CONFIG_RENESAS_RA_EXTERNAL_INTERRUPT (else -ENOTSUP for GPIO_INT_ENABLE);
  it is "default y" once a renesas,ra-external-interrupt node is enabled and
  the board enables &port_irq12/&port_irq13 — set explicitly in prj.conf.
  Pin->IRQ mapping from dts/arm/renesas/ra/ra8/r7fa8d1xh.dtsi: P008->port_irq12,
  P009->port_irq13. zephyr.dts confirms port_irq12 NVIC vector 0x58=88 prio 12,
  port_irq13 vector 0x59=89 prio 12. This is a 2nd "Interrupt + NVIC" report
  data point (commented in io_control.c). 40 ms software debounce (no
  debounce-interval-ms on the gpio-keys nodes).
- log_timer.c = the report's "Timer" deliverable. Uses kernel k_timer
  (always built in, no Kconfig, no overlay). Decision doc'd in-file: chose
  k_timer over on-chip AGT because agt0/agt1 (renesas,ra-agt @40221000/
  40221100, NVIC IRQ 83 "agti") are status=disabled in ra8x1.dtsi and the
  board never enables them + AGT needs CONFIG_COUNTER + CONFIG_COUNTER_RA_AGT
  (drivers/counter/counter_renesas_ra_agt.c) and a DT overlay. AGT path is
  documented as the hardware-timer alternative for a later phase.
  API: log_timer_start(period_ms, cb, ud) / log_timer_stop() /
  log_timer_seq() / log_timer_timestamp() (returns k_uptime_ticks(), same
  clock the Phase 2A RX ISR samples). Tick cb runs in system-clock ISR
  context; main.c's on_tick is the "batched flash flush" trigger stub
  (FLUSH_PERIOD_MS=100, matches the batch-writes decision).
Build: .venv\Scripts\west build -b ek_ra8d1 apps/gpio_timer -d
 build/gpio_timer -p always  -> exit 0, no warnings, [179/179] linked
 zephyr.elf. FLASH 38012 B (1.84%), RAM 8072 B (0.88%). Symbols
 io_control_init / io_control_request / log_timer_start / button_isr /
 tick_expiry / blink_expiry all in zephyr.elf. .config has
 CONFIG_RENESAS_RA_EXTERNAL_INTERRUPT=y. NOT flashed (no board).
Did NOT touch CAN / Ethernet / USB / flash (per phase scope).

## Phase 2A summary (2026-08-29) — apps/can_logger, build-only
Self-contained CAN-FD interface module over the Zephyr native CAN API
(canfd0 = chosen zephyr,canbus). Files:
- apps/can_logger/CMakeLists.txt, prj.conf
- apps/can_logger/boards/ek_ra8d1.overlay  (CANFD0 pin remap P202/P203 +
  bitrate=500k, bitrate-data=2M, passive can-transceiver max-bitrate=5M)
- apps/can_logger/include/can_iface.h, src/can_iface.c, src/main.c
API verified against zephyr/include/zephyr/drivers/can.h AND
drivers/can/can_renesas_ra.c (not from memory):
- can_iface_init(): device_is_ready -> can_get_capabilities (assert
  CAN_MODE_FD) -> can_set_mode(CAN_MODE_FD) -> 3x can_add_rx_filter ->
  can_start. Bitrates come from DT, not can_set_bitrate().
- RX: can_add_rx_filter(canfd0, can_iface_rx_isr, NULL, &filter). The
  callback runs in ISR context (RA driver canfd_common_fifo_rx_isr,
  IRQ_CONNECT to DT rx irq = 45 @ prio 12 = NVIC). This is the
  "Interrupt + NVIC" report deliverable; code has an explicit comment
  block. can_iface_rx_isr captures k_uptime_ticks() as a SOFTWARE
  timestamp (RA driver does NOT implement CONFIG_CAN_RX_TIMESTAMP - no
  HW bit-time timestamp on this SoC; confirmed by grep).
- Filters (RA driver = first-match-wins, registration order):
  [0] std 0x100..0x1FF example, [1] std catch-all, [2] ext catch-all
  (CAN_FILTER_IDE). FD is a per-frame flag (CAN_FRAME_FDF), NOT a filter
  criterion - struct can_filter only has id/mask/flags, only CAN_FILTER_IDE
  defined. FD frames arrive via whichever filter matches once in
  CAN_MODE_FD.
- TX: can_iface_send() -> can_send(canfd0, &f, timeout, can_iface_tx_done,
  NULL). Non-NULL callback => non-blocking queue (won't hang on missing
  ACK off-bench).
- struct canlog_frame { uint32_t id; uint8_t dlc; uint8_t flags;
  uint8_t data[CAN_MAX_DLEN]; uint64_t timestamp; }.
Kconfig (each verified to exist): CONFIG_CAN, CONFIG_CAN_FD_MODE,
CONFIG_CAN_RENESAS_RA_CANFD, CONFIG_LOG. NOT CONFIG_CAN_RX_TIMESTAMP.
Build: west build -b ek_ra8d1 apps/can_logger -d build/can_logger -p always
 -> exit 0, no warnings. FLASH 45436 B (2.20%), RAM 8792 B (0.96%).
 zephyr.dts confirms canfd0 rx irq 45/prio 12, pins psels 0x3022/0x3032
 (P202 CRX0 / P203 CTX0), bitrate 500000, bitrate-data 2000000.
 All can_iface_* symbols linked into zephyr.elf. NOT flashed (no board).
Did NOT touch Ethernet / USB / flash storage (per phase scope).

## CANFD0 pin decision — CURRENT: CTX0=P704, CRX0=P705 (2026-08-31)
### >>> RETRACTION of the 2026-08-29 "FINAL: CTX0=P203, CRX0=P202" decision <<<
The original entry is preserved verbatim below on purpose. It is WRONG, and
seeing both what was believed and why it failed is the point — this is the
anti-hallucination process working, catching a bad "confirmed" fact before it
cost bench time.

WHY IT WAS WRONG (CONFIRMED FACT, do not re-litigate):
- The RA8D1 datasheet's pin-function table has TWO BGA224 columns: "BGA224"
  and "BGA224 without MIPI". P202/P203/P204/P205 appear as GPIO only in the
  *without MIPI* column. The 2026-08-30 "user confirmed against pin list
  Table 1.16" check read the wrong column.
- Datasheet Table 1.13 (Product List) confirms our exact part R7FA8D1BHECBD,
  package PLBG0224GD-A, IS the MIPI variant. On this silicon P202-P205 are
  permanently committed to MIPI_CL_P/N and MIPI_DL0_P/N display signals. They
  are not GPIO and not CANFD-capable.
- Physically confirmed on the board too: NO header on the EK-RA8D1 breaks out
  P202/P203 at all. There was never anything to wire to.
- Root cause worth remembering: Zephyr's RA pinctrl does not validate PSEL
  capability at build time, so the wrong pins built perfectly cleanly for
  three phases. A clean build is not pin validation.

THE FIX (2026-08-31, software only — physical wiring unchanged):
- CTX0 = P704, CRX0 = P705 (the original Renesas FSP default). The breadboard
  transceiver was already wired to these pins; nothing on the bench moved.
- Reverted in BOTH overlays: boards/ek_ra8d1.overlay and
  apps/can_logger/boards/ek_ra8d1.overlay.
- RA_PSEL encoding re-verified from source, not memory
  (zephyr/include/zephyr/dt-bindings/pinctrl/renesas/pinctrl-ra.h:61,66,117,
  123,144): RA_PSEL(psel,port,pin) = 1<<13 | psel<<8 | pin<<4 | port<<0, with
  RA_PSEL_CANFD = 0x10. Therefore P704 = 0x3047, P705 = 0x3057.
  (Cross-check that the encoding rule is right: it reproduces the OLD values
  too — P202 = 0x3022, P203 = 0x3032, exactly what the old build recorded.)

## CAN + Ethernet are MUTUALLY EXCLUSIVE on this board (DECISION 2026-08-31)
P704/P705 are not merely a devicetree pinctrl choice that happens to collide
with RMII0_RX_ER_B / RMII0_CRS_DV_B in ether_default. On the EK-RA8D1 they are
physically BUS-SWITCH-ROUTED to the Ethernet PHY. So CAN-FD and on-chip
Ethernet are NOT electrically safe to run at the same time on this board,
regardless of which firmware is flashed.

DECISION: downgrade "CAN + Ethernet concurrent" from a hard requirement to
SEQUENTIAL USE MODES — capture over CAN, retrieve the logs over Ethernet
afterwards. This matches real product use, costs no capability, and removes
only exact simultaneity.

BENCH CONSEQUENCE (for the user, every time modes are switched): physically
disconnect the CAN transceiver's 2 wires from P704/P705 before testing
Ethernet, and reconnect them before testing CAN. This holds until a real board
SCHEMATIC (not just the MCU datasheet) turns up a routing that avoids it —
that search is FUTURE WORK, not a blocker for bring-up.

NOTE this also invalidates the original rationale for moving off the stock
P401/P402 pins: that reasoning ("we run CAN + Ethernet + OSPI concurrently")
assumed concurrency that this board cannot provide anyway.

### ----- ORIGINAL ENTRY, SUPERSEDED / RETRACTED — kept for the record -----
## CANFD0 pin decision (2026-08-29) — FINAL: CTX0=P203, CRX0=P202  [RETRACTED]
Overlay: D:\BITS_ASSIGNMENTS\ESD\SITUATIONAL_LEARNING\boards\ek_ra8d1.overlay
(remaps &pinctrl/canfd0_default group1 to RA_PSEL(CANFD,2,2)=P202 CRX0,
RA_PSEL(CANFD,2,3)=P203 CTX0). Apply to any build with:
  -- -DEXTRA_DTC_OVERLAY_FILE="D:/BITS_ASSIGNMENTS/ESD/SITUATIONAL_LEARNING/boards/ek_ra8d1.overlay"
(Phase 2A's app should instead carry its own <app>/boards/ek_ra8d1.overlay
with the same content, or #include this file.)

Why not the obvious pins (all conflicts below are workspace-verified in
zephyr/boards/renesas/ek_ra8d1/ek_ra8d1-pinctrl.dtsi with the peripheral
enabled = status okay):
- P401 (stock Zephyr CTX0): collides with ET0_MDC (mdio_default) AND I3C0
  SCL (i3c0_default). Both enabled.
- P402 (stock Zephyr CRX0, also aik_ra8d1 CRX0): collides with ET0_MDIO
  (mdio_default). Enabled.
- P103 (aik_ra8d1 CTX0 — same R7FA8D1BH MCU): collides with OSPI flash
  (ospi0_default group1). Enabled.
- P704 / P705 (user's Renesas FSP default CTX0/CRX0): collide with
  RMII0_RX_ER_B / RMII0_CRS_DV_B (ether_default). Enabled. User's own
  suspicion confirmed.
- P202 / P203: NOT used anywhere in the stock ek_ra8d1 config. ioport2 is
  not enabled; sdhc0 AND sdhc1 are both status=disabled; the only Port 2
  reference (acmphs_vcout on P208) is /omit-if-no-ref/ and unreferenced.
  Clean against CAN, Ethernet (RMII+MDIO), USB HS, USB FS, OSPI, SDRAM,
  console UART9, mikroBUS SPI/UART, PWM7, ADC0/DAC0.

CAVEAT (per CLAUDE.md anti-hallucination rule 2): the workspace has NO
per-pin peripheral-capability table (that lives in the datasheet / e2studio
device pack). So "P202=CANFD0_CRX0, P203=CANFD0_CTX0 on the RA8D1" is NOT
workspace-verified. Evidence it's right: Renesas' own ek_ra8t2 board file
routes CANFD0 to exactly CRX0=P202/CTX0=P203 (adjacent RA8 family, same
CAN-FD IP); user independently proposed this SD0-tied pair from datasheet
knowledge. ACTION FOR USER: before rewiring, confirm against R7FA8D1BH
datasheet "List of Pin and Pin Functions" that P202/P203 expose CANFD0.
Zephyr's RA pinctrl does not validate PSEL at build time -> a wrong pin
builds clean and just fails silently on the bench (no board yet anyway).

## Completed
- 2026-08-29 Phase 0 DONE. Build environment is reproducible and
  hello_world builds clean for ek_ra8d1. Details:
  - Python 3.12.3 venv at .venv/ (system Python 3.13/3.14 are too new for
    Zephyr tooling — always use .venv). west 1.5.0.
  - west workspace: T2 topology, workspace root = project root.
    zephyr/ was shallow-cloned (git clone --depth 1) then `west init -l
    zephyr`; `west update --narrow -o=--depth=1` for all ~60 modules
    (shallow, to survive flaky network). .gitignore excludes west-managed
    dirs + .venv + build/.
  - Zephyr mainline v4.4.99 (NOT a tagged release — see decisions log).
  - Zephyr SDK 1.0.1 at C:\Users\sumit\zephyr-sdk-1.0.1 (gnu toolchain
    arm-zephyr-eabi + host tools). Installed via `west sdk install`.
    NOTE: mainline pins SDK 1.0.1 via zephyr/SDK_VERSION; CLAUDE.md's
    "0.16.6+" is satisfied.
  - 7-Zip 26.02 installed (winget) — required by `west sdk` to unpack .7z.
  - ek_ra8d1 board target CONFIRMED present via `west boards` (also:
    aik_ra8d1, cpkcor_ra8d1b, ra8d1_vision_board).
  - Build: `west build -b ek_ra8d1 zephyr/samples/hello_world -d
    build/hello_world` -> exit 0, [169/169] linked zephyr.elf.
    FLASH 27352 B / 2016 KB (1.32%), RAM 5696 B / 896 KB (0.62%).
    Artifacts: build/hello_world/zephyr/zephyr.{elf,hex,bin,map}.
    NOT flashed (no board).
  - Repo scaffold: apps/ boards/ docs/ with README placeholders.
  - 2026-08-29 (later session) RE-VERIFIED independently: zephyr.elf is a
    valid ARM EXEC (entry 0x2000be9); arm-zephyr-eabi-size = text 26604 /
    data 820 / bss 4880; `ninja -n` in build/hello_world => "no work to
    do" (build complete + up to date). CMake configure re-run OK (finds
    SDK 1.0.1, arm-zephyr-eabi-gcc 14.3.0, ld.bfd 2.43.1).

## In Progress
- (nothing — between phases)

## Phase 1 findings (devicetree, verified against workspace 2026-08-29)
All node/status facts below are from grepping zephyr/dts + board dts AND
confirming in the generated build/hello_world/zephyr/zephyr.dts.
SoC nodes live in dts/arm/renesas/ra/ra8/ra8x1.dtsi (+ r7fa8d1xh.dtsi for
usbhs); board enables/pinmux in boards/renesas/ek_ra8d1/ek_ra8d1.dts and
ek_ra8d1-pinctrl.dtsi.

- canfd_global (renesas,ra-canfd-global @40380000): SoC default DISABLED;
  board sets status=okay. Node label `canfd_global` (no unit-addr alias in
  board override: `&canfd_global`).
- canfd0 (renesas,ra-canfd, channel 0, child of canfd_global): SoC default
  DISABLED; board sets status=okay, rx-max-filters=16,
  pinctrl-0=<&canfd0_default>. IRQs: err=43, tx=44, rx=45 (all prio 12 =
  NVIC). This is the channel we use. `zephyr,canbus` chosen = &canfd0.
- canfd1 (channel 1): DISABLED, board never enables it, NO pinctrl group
  defined for it in the board. IRQs err=46,tx=47,rx=48. => canfd0 is the
  free/available channel; use canfd0.
- CANFD0 pin mapping: RESOLVED — see the "CANFD0 pin decision" section
  near the top. Final: CTX0=P203, CRX0=P202 via boards/ek_ra8d1.overlay.
  (Stock Zephyr canfd0_default = CTX0 P401 / CRX0 P402, which collide with
  Ethernet MDIO; user's FSP P704/P705 collide with RMII. Port 2 is clean.)
- eth (renesas,ra-ethernet @40354100): SoC DISABLED; board status=okay,
  phy-connection-type="rmii", phy-handle=<&phy>, pinctrl ether_default
  (RMII on P43,P45,P46,P70..P75). IRQ 42.
- mdio (renesas,ra-mdio): SoC DISABLED; board status=okay, pinctrl
  mdio_default (P401/P402 — see conflict above).
- ethernet-phy@5 (compatible "ethernet-phy", reg=5): board status=okay,
  child of mdio. (SW1-5 ETHB=ON still a HW setup reminder, unverifiable
  without board.)
- usbfs (renesas,ra-usbfs @40250000): SoC DISABLED, and board does NOT set
  status=okay (only sets pinctrl + maximum-speed="full-speed"). => usbfs
  stays DISABLED in the stock board. If we want the Full-Speed port we
  must enable it in our overlay. usbfs_phy = usb-nop-xceiv. IRQs 55,56.
- usbhs (renesas,ra-usbhs @40351000, in r7fa8d1xh.dtsi): SoC DISABLED;
  board status=okay, maximum-speed="high-speed", pinctrl usbhs_default
  (VBUS on P11_1), child udc `zephyr_udc0` status=okay. IRQ 54. => usbhs
  is the USB device that's live by default. Board `ek_ra8d1.yaml` lists
  `usbd` as supported.
- ospi0 (renesas,ra-ospi-b @40268000): SoC DISABLED; board status=okay,
  pinctrl ospi0_default. Child `s28hl512t: ospi-nor-flash@90000000`
  (compatible "renesas,ra-ospi-b-nor", XSPI_OCTO_MODE, DTR, 200 MHz,
  64 MB @ 0x90000000) status=okay with ONE partition: label "nor",
  0x0..64MB (whole chip). We'll re-partition for littlefs in Phase 2D.
  NOTE: this is the flash on the MCU Native Pin Access header — CLAUDE.md
  says confirm it's actually populated before relying on it.
- Also noted: on-chip data-flash `flash1` @27000000 (12 KB) is the
  `zephyr,flash-controller`; it already has a `storage_partition` (12 KB).
  Tiny — not our log store, but usable for settings/NVS later.
- ek_ra8d1.yaml `supported:` list is twister-scoped (gpio,uart,watchdog,
  usbd,display,counter,i2s,i3c,video) — CAN/ethernet/ospi are NOT in it
  but work via DT. Don't read that list as "unsupported".

## Environment reminders (how to build in a fresh shell)
- From project root: `.venv\Scripts\west build -b ek_ra8d1 <app>`.
- Put 7-Zip on PATH for any `west sdk` op: C:\Program Files\7-Zip.
- dtc NOT installed; Zephyr builds fine without it (CMake warns "Could NOT
  find Dtc" then proceeds via python-devicetree). gperf also not installed,
  not needed so far.
- choco needs an ELEVATED shell (lock errors otherwise) — only relevant if
  we later actually need dtc/gperf.
- GOTCHA: something on this machine (AV/endpoint security suspected) kills
  long-running git clone / hidden powershell after 1-3 min. The initial
  `west update` only completed after retries. For big fetches, prefer the
  user's own foreground terminal, or add the project dir to Windows
  Security exclusions. Also: VS Code's Git extension auto-scans the multi-GB
  zephyr/ tree and spawns many git.exe that lock files — close the folder
  in VS Code before any `west update` / large clone.

## Blocked / Open Questions
- CANFD0 pins: RESOLVED (confirmed by user against pin list 2329.pdf,
  Table 1.16). CTX0 = P203, CRX0 = P202. The overlay already matches, so no
  code change was needed. Wire the breadboard transceiver accordingly:
  transceiver TX from board pin P203, RX into board pin P202, STB tied to GND.
- LED colour mapping: RESOLVED (confirmed by user against the EK-RA8D1 User's
  Manual, Table 22). The Phase 2B choice in io_control.c stands as written —
  led2 = green (idle), led3 = red (blink while logging). No code change needed.
  Still verify against the physical silkscreen at bring-up Step 1; if the board
  disagrees, the LED_*_NODE macros make it a one-line change.
- canfd0 vs canfd1: ANSWERED — use canfd0 (canfd1 has no board pinctrl).
  Onboard 3-pin CAN header population/jumper state still unverifiable
  without the board (we're using our own breadboard transceiver anyway).
- Confirm whether SDRAM is populated on this specific board revision, or
  whether logging design should assume SRAM-only buffering.
- usbfs is NOT enabled by stock board dts (only usbhs is). If Phase 2C
  wants the Full-Speed port, our overlay must set &usbfs status = "okay".

## Next Steps (the next session should start here)
0. READ FIRST: the "Current Phase" section at the top has the full 2026-08-31
   hardware bring-up record — DLM root cause, Steps 0/1/2 results, the
   classic-only-analyser limitation, and tooling gotchas. Do not re-diagnose
   any of it.
1. **PHASE 5 BRING-UP IS COMPLETE.** Steps 0/1/2/3/5/6 all PASS on hardware
   (session 3 + session 2 records above). Step 4 (flash_log) DROPPED — logging
   is out of scope. There are no more bring-up steps. Two tracks remain:
   a) **Host GUI** — a PC-side app that speaks CLP over the J-Link VCOM/UART
      (COM12, 115200). CLP is transport-agnostic and its on-device self-test
      passes every boot. `docs/clp_protocol.md` is the spec; `scratchpad`'s
      throwaway Python clients (doip_test.py etc.) show the pattern.
   b) **Phase 6 — the BITS WILP ESD report.** All the deliverables it maps to
      now have REAL measured hardware results: NVIC/interrupts (CAN RX IRQ 45 +
      button IRQs 88/89), timer (100.26 ms/tick measured), CAN-FD driver,
      Ethernet/DoIP, MCUboot verified boot. See PROMPTS.md's Documentation phase.
   Honest gaps to state in the report: CAN-FD/BRS unvalidated (classic-only
   analyser), USB-HS bulk needed an upstream hal_renesas fix, OSPI NOR dead on
   this board, MCUboot on the debug key.
   Before flashing an Ethernet build again: SW1-5 ON / SW1-3 OFF and move the
   transceiver wires off P704/P705. Board is currently in CAN config running
   MCUboot + signed can_logger.
2. Ethernet bench setup that worked (session 3), for repeat runs:
   - SW1: 5 ON, 7 ON, everything else OFF (default has SW1-3 CAMERA ON / SW1-5
     OFF, so you MUST flip SW1-3 off and SW1-5 on — Ethernet is not wired to the
     PHY by default).
   - CAN transceiver wires OFF P704/P705.
   - Laptop USB-C Ethernet dongle ("Ethernet 2") static 192.168.1.10/24, no gw.
   - Board is 192.168.1.50; DoIP on :13400 (UDP+TCP). Test client:
     scratchpad/doip_test.py. Console capture: scratchpad/cap.py COM12 30.
3. EQUIPMENT TO ACQUIRE: a CAN-FD-capable analyser (PCAN-USB FD or similar).
   Until then CAN-FD/BRS cannot be validated, and apps/can_logger/src/main.c
   keeps `#define BOOT_TX_USE_FD 0`. Flip it to 1 once FD gear is on the bench.
4. HOUSEKEEPING carried over:
   - If not already done, REMOVE the Windows Defender exclusions added during
     debugging (elevated): Remove-MpPreference -ExclusionProcess 'JLink.exe';
     -ExclusionPath 'C:\Program Files\SEGGER\JLink_V948';
     -ExclusionPath 'D:\BITS_ASSIGNMENTS\ESD\SITUATIONAL_LEARNING'.
   - build/tz_spike and build/tz_spike2 are throwaway Phase 4 dirs, deletable.
   - Commit today's hardware results (5 files changed on 2026-08-31:
     CLAUDE.md, STATE.md, both ek_ra8d1.overlay files,
     docs/bringup_checklist.md, plus apps/can_logger/src/main.c).
5. Phase 4 decisions remain CLOSED: TrustZone NO-GO; RSIP-E51A wrapped-key
   AES-128 CMAC is the chosen security direction, still NOT green-lit for
   implementation. Do not revisit without an explicit ask.
6. Remaining genuinely-open items: SDRAM population (still unconfirmed on this
   revision), the usbfs-vs-usbhs note (Step 3 targets USB-HS deliberately), and
   the unverified "256-bit HUK" figure for the Phase 6 report.

## Build commands (from project root D:\BITS_ASSIGNMENTS\ESD\SITUATIONAL_LEARNING)
PowerShell:
  # incremental build (re-run after code changes)
  .\.venv\Scripts\west build -b ek_ra8d1 <app-path> -d build\<name>
  # e.g. .\.venv\Scripts\west build -b ek_ra8d1 zephyr\samples\hello_world -d build\hello_world
  # pristine / clean build (config or devicetree changed, or just to be sure)
  .\.venv\Scripts\west build -b ek_ra8d1 <app-path> -d build\<name> -p always
  # or wipe by hand:  Remove-Item -Recurse -Force build\<name>
  # menuconfig / see resolved config:  .\.venv\Scripts\west build -d build\<name> -t menuconfig
  # apply the project CANFD0 pin overlay to an arbitrary app:
  #   ... -d build\<name> -- -DEXTRA_DTC_OVERLAY_FILE="D:/BITS_ASSIGNMENTS/ESD/SITUATIONAL_LEARNING/boards/ek_ra8d1.overlay"
  # flashing (ONLY once the board is physically attached — J-Link default):
  #   .\.venv\Scripts\west flash -d build\<name>
  #   .\.venv\Scripts\west flash -d build\<name> -r pyocd     (alt runner)
  # NOTE: board is NOT available yet -> build-only, never run west flash.

## Decisions Log (append, never delete — this is the project's memory)
- 2026-08-29: Confirmed via Zephyr docs that TrustZone is not exposed for
  `ek_ra8d1` in mainline Zephyr (no SAU binding, no /ns target). TrustZone
  work will be a time-boxed feasibility spike (Phase 4), not a committed
  feature.
- 2026-08-29: Confirmed CAN-FD is on-chip; external hardware is a
  transceiver only, connected directly to CAN-FD pins (not SPI).
- 2026-08-29: Decided on static IP (not DHCP) for Ethernet, matching real
  automotive network practice. DoIP (ISO 13400) chosen as the target
  protocol style for log retrieval over Ethernet, since it's a real
  automotive standard rather than an ad hoc TCP scheme.
- 2026-08-29: SDRAM will only be used as a CAN capture ring buffer if
  on-chip SRAM (896 KB) proves insufficient for burst absorption — default
  to SRAM-only design first, to reduce peripheral count.
- 2026-08-29: Pinned to Zephyr MAINLINE (v4.4.99, commit was HEAD of main
  on this date), not a release tag. Reason: needed newest Cortex-M85 /
  ek_ra8d1 support. RISK: mainline can break between pulls. If instability
  bites, pin to the latest v4.x release tag and re-run west update.
  hello_world memory map also showed an SDRAM region (64 MB) and ITCM/DTCM
  (64 KB each) as defined in the board DTS — presence in DTS != populated
  on the physical board (still an open question above).
- 2026-08-29: Phase 1 — transceiver STB pin is hard-wired to GND on the
  breadboard (permanent normal mode). No MCU GPIO controls it => the
  planned enable/standby devicetree overlay is CANCELLED, not deferred.
  Phase 2A CAN driver needs no standby-pin handling.
- 2026-08-29: Phase 1 — canfd0 chosen as the active CAN channel (canfd1
  has no board pinctrl and stays disabled).
- 2026-08-29: Phase 1 — CANFD0 pins FINALISED at CTX0=P203 / CRX0=P202
  (boards/ek_ra8d1.overlay). Every pin pair confirmable from the workspace
  (P401/P402 stock, P103 from aik_ra8d1, P704/P705 from user's FSP)
  collides with Ethernet or OSPI, all of which we run concurrently. Port 2
  is 100% unused in the stock ek_ra8d1 (ioport2 off, both SDHI disabled).
  P202/P203 = CANFD0 is not workspace-verifiable (no pin-capability table
  in-tree) but matches Renesas' own ek_ra8t2 board and the user's
  datasheet-based suggestion; user to confirm vs R7FA8D1BH datasheet
  before rewiring.
  VERIFIED in build/hw_canfd/zephyr/zephyr.dts: canfd0_default/group1
  psels = <0x3022>, <0x3032> = P202 (CRX0/RX) then P203 (CTX0/TX), sourced
  from boards\ek_ra8d1.overlay. Encoding: RA_PSEL bit pos MODE=13 PSEL=8
  PIN=4 PORT=0. hello_world clean build with overlay = exit 0, footprint
  unchanged (FLASH 27352 B, RAM 5696 B). Build dir: build/hw_canfd.
  Breadboard wiring for the user: transceiver TXD <- P203, RXD -> P202.
- 2026-08-29: Phase 2A — CAN-FD module is an APPLICATION module over the
  Zephyr CAN API, not a new driver (renesas,ra-canfd already exists).
  Decided: rely on devicetree bitrate/bitrate-data (no runtime
  can_set_bitrate); CAN_MODE_FD set at init; software RX timestamp
  (k_uptime_ticks) because the RA driver has no HW timestamp support;
  non-blocking TX (can_send with a completion callback). "FD filter" is
  not a real thing in the Zephyr API - documented in code.
- 2026-08-29: Phase 2B — periodic timer implemented with kernel k_timer, NOT
  the on-chip AGT. AGT nodes (agt0/agt1) are disabled in ra8x1.dtsi and the
  board never enables them; AGT would need CONFIG_COUNTER + CONFIG_COUNTER_RA_AGT
  + a DT overlay for &agt0 and its counter child. k_timer is simpler, always
  built in, and its period/callback model fits "timestamp + periodic flush".
  AGT kept as documented hardware-timer fallback for later phases.
- 2026-08-29: Phase 2C — GUI link protocol "CLP v1" designed to common
  industrial-serial practice (over the user's first-draft magic+XOR struct):
  COBS + 0x00 self-synchronising framing (a magic byte can't delimit - 0xA5
  occurs in CAN payload), CRC-16/X-25 not XOR/sum, ver+type header for a
  multi-message bidirectional link, explicit len + variable payload (ship only
  the valid DLC bytes). Frame `flags`/`timestamp` deliberately match Phase 2A
  canlog_frame. Full spec: docs/clp_protocol.md. User approved this over the
  original draft.
- 2026-08-29: Phase 2E — FOUND + FIXED a latent pin conflict in the STOCK
  ek_ra8d1 board files: &mdio (ET0_MDC = P401) and &i3c0 (I3C0_SDA = P401) are
  BOTH status=okay and both want P401. Zephyr RA pinctrl doesn't catch it at
  build time. apps/eth_doip/boards/ek_ra8d1.overlay disables &i3c0. If a later
  phase needs I3C + Ethernet together on this board it's a genuine
  hardware-level conflict (board bus switches: I3C vs ETH-B), not just a
  software one. Re-verified all other Phase 2 apps' pins are clean against
  both ethernet mux options and SDRAM.
- 2026-08-29: Phase 2E — the board uses the "ETHERNET B" RMII pin-mux
  (SW1-5 ON): MDC P401, MDIO P402, RMII P403/P405/P406/P700-P705. Confirmed
  against ek_ra8d1-pinctrl.dtsi AND the user's board manual Table 24.
- 2026-08-29: Phase 2E — static IPv4 192.168.1.50/24 gw .1 (user-provided
  address) via CONFIG_NET_CONFIG_SETTINGS, no DHCP - matches real automotive
  practice and the earlier static-IP decision. IPv6 disabled.
- 2026-08-29: Phase 2E — DoIP kept a skeleton per phase scope: discovery
  (Vehicle Announcement), routing activation, diag-message ack + a UDS
  RequestUpload/TransferData/TransferExit stub for log download. Entity
  logical address 0x1234 and DID 0xFD00 (log-store status) are project
  placeholders. Not implemented: startup announcements, alive-check timers,
  DoIP-over-TLS, real file streaming from littlefs. See docs/doip_skeleton.md.
- 2026-08-29: Phase 2D — littlefs "canlog" partition placed at OSPI offset
  0x40000 (not 0x0) so it sits entirely in the flash's uniform 256K-sector
  region; Zephyr littlefs picks the largest erase size overlapping the
  partition and needs it valid throughout. First 256K reserved for future NVS.
- 2026-08-29: Phase 2D — logger keeps an on-chip SRAM ring buffer even though
  the user confirmed SDRAM is populated. Rationale from the decisions log
  stands (fewer active peripherals; use SDRAM only if SRAM burst-absorption
  proves inadequate on real hardware). SDRAM path = bump FLASH_LOG_RING_SIZE +
  relocate the ring to the SDRAM memory region.
- 2026-08-29: Phase 2D — batch writes via a dedicated writer thread draining
  an SRAM ring to fs_write() in <=1 KB chunks; lossy on ring-full (drop+count).
  Chosen over per-frame writes (flash wear) and over blocking the producer
  (would stall CAN RX). flash_log_flush() is the hook for the Phase 2B timer.
- 2026-08-29: Phase 2C — used the USB device stack "next"
  (CONFIG_USB_DEVICE_STACK_NEXT) on usbhs, matching the current
  zephyr/samples/subsys/usb/cdc_acm. usbd_ctx.c templated from
  samples/subsys/usb/common/sample_usbd_init.c. VID/PID left as the Zephyr
  test values 0x2fe3/0x0001 (PLACEHOLDER) per user choice.
- 2026-08-29: Phase 2B — buttons handled via the raw GPIO API on the stock
  gpio-keys nodes (gpio_pin_interrupt_configure_dt + gpio_add_callback_dt),
  not the input subsystem — keeps deps minimal and makes the NVIC/IRQ path
  explicit for the report. Needs CONFIG_RENESAS_RA_EXTERNAL_INTERRUPT
  (P008->port_irq12/NVIC 88, P009->port_irq13/NVIC 89, both prio 12).
- 2026-08-29: Phase 3 — MCUboot integrated via `west build --sysbuild`, built
  against apps/can_logger. Mode = OVERWRITE_ONLY (not the sysbuild default
  SWAP_USING_OFFSET) because ek_ra8d1 flash0 has write-block-size 128 and
  MCUboot statically asserts BOOT_MAX_ALIGN <= 32 for all swap modes. Sig type
  ECDSA-P256, module debug key for now. Partitions added on &flash0 only:
  boot 128K / slot0 928K / slot1 928K (32K-aligned). All wiring is new files
  under apps/can_logger/sysbuild* — Phase 2 apps and plain (non-sysbuild)
  builds are unaffected. Build-only: exit 0, both images linked, never booted.
- 2026-08-29: Phase 3 — MCUboot is a NECESSARY-BUT-NOT-SUFFICIENT step toward
  SecOC. Reusable: the signing-key flow. Provides: a verified-boot anchor for
  code that would hold a SecOC key. Does NOT provide: the symmetric runtime
  SecOC MAC key (different key type), nor key confidentiality without
  TrustZone/RSIP/secure element. Full reasoning in the "MCUboot -> SecOC
  stepping-stone note" section above.
- 2026-08-29: Build tooling choices — shallow clones (depth 1, narrow) to
  keep the workspace small (~a few GB) and resilient to the flaky campus
  network that blocks/throttles GitHub. Trade-off: no git history in
  zephyr/ or modules/, and `west update` after a manifest bump may need
  `--fetch-opt` care. Acceptable for a build-only project.
- 2026-08-30: Phase 4 — TrustZone on ek_ra8d1 is a NO-GO, documented as future
  work (docs/trustzone_feasibility.md). Blocking reasons: no CPSCU/PSCU driver
  or binding in Zephyr (per-peripheral S/NS attribution is unreachable), no
  non-secure alias DT nodes for any RA8 peripheral, no /ns board variant for any
  Renesas board, no TF-M port for any Renesas device, and the S/NS boundary must
  be programmed into the device with Renesas Flash Programmer (needs the board).
  A TRUSTED_EXECUTION_SECURE image DOES build for ek_ra8d1 but is a trap —
  Zephyr has no SAU region-programming code, so it boots with the SAU
  unconfigured and no real isolation.
- 2026-08-30: Phase 4 — REVISED the Phase 3 conclusion that a hardware key store
  "would need TrustZone". RSIP-E51A is already compiled into our ek_ra8d1 builds
  (CONFIG_HAS_RENESAS_RA_RSIP_E51A=1) and exposes AES-128 CMAC over a wrapped
  key index. Decision: pursue SecOC via RSIP rather than TrustZone, pending user
  go-ahead. The RSIP TRNG is already live as the board's zephyr,entropy source.
- 2026-08-30: Phase 4 — CLAUDE.md's "no SAU binding" hard-constraint clause is
  factually wrong (soc/renesas/ra/ra8d1/Kconfig:9 selects CPU_HAS_ARM_SAU).
  Flagged, NOT yet edited — awaiting user OK. The rest of that bullet stands.
- 2026-08-30: Documentation session. User LOCKED both Phase 4 decisions into
  CLAUDE.md's hard constraints: (1) TrustZone NO-GO, do not revisit unless
  explicitly asked; (2) RSIP-E51A hardware-wrapped-key AES-128 CMAC is the
  project's security mechanism for a future SecOC-style phase — locked-in
  direction, not yet green-lit for implementation. The old "no SAU binding"
  clause was replaced with the accurate CPU-reachable-but-unusable wording.
- 2026-08-30: CANFD0 pin mapping RESOLVED (user confirmed CTX0=P203/CRX0=P202
  against pin list 2329.pdf, Table 1.16) and LED colour mapping RESOLVED (user
  confirmed against EK-RA8D1 User's Manual, Table 22 — led2=green idle,
  led3=red logging). Both matched what the code already had; no code changes.
- 2026-08-30: Wrote docs/bringup_checklist.md for tomorrow's Phase 5 hardware
  bring-up. Governing rule: flash ONE isolated Phase-2 app at a time, never a
  combined image, and don't advance until a step passes or its failure is
  understood and logged — so a bug stays local instead of rippling. Step 0
  (hello_world) is a hard gate: if the debug link fails, stop.
- 2026-08-30: Repo is now under git and pushed to
  https://github.com/saravana2003/PCAN_alterative (branch main, first commit
  d315a76, 53 files). Committed as saravana2003 <asaravanakumar2k3@gmail.com>
  set as REPO-LOCAL git identity — the global identity on this machine is a
  different account (saravanariver), so don't rely on the global config here.
  .gitignore excludes the whole west workspace (.west/ zephyr/ modules/
  bootloader/ tools/ doc/), .venv/, build/, and *.pdf (vendor datasheets are
  not ours to redistribute). To rebuild the workspace from a fresh clone:
  `west init -l` + `west update`. Commit the day's bring-up results tomorrow.
- 2026-08-31: Phase 5 — FIRST HARDWARE SESSION. Steps 0/1/2 of the bring-up
  checklist all PASS on real silicon: debug link + console, GPIO/LEDs/buttons +
  100.26 ms measured timer tick, and the CAN-FD driver proven BIDIRECTIONALLY
  against a PCAN analyser (standard-ID and extended-ID RX with correct IDE
  flag, board->analyser TX, BUSOK, no error storm). Zero code changes were
  needed for gpio_timer.
- 2026-08-31: Phase 5 — the day's flash failures were caused by the board
  shipping in Renesas DLM state OEM_PL2 / Authentication Level AL2, which makes
  code flash above the first 32 KiB unerasable. Fixed by Renesas Flash
  Programmer "Initialize Device". Proven by the same unchanged artifact going
  0/4 -> 1/1 immediately after. Explicitly NOT image size, NOT AV, NOT board
  damage, NOT block protection — all tested and eliminated. Neither J-Link nor
  pyocd can see DLM state; use the vendor tool early for inexplicable RA flash
  failures.
- 2026-08-31: Phase 5 — EQUIPMENT GAP: the bench PCAN-USB is CLASSIC CAN ONLY,
  so CAN-FD/BRS cannot be validated. Deferred until an FD-capable analyser is
  available; this is a scope limitation to state honestly in the Phase 6
  report, not a firmware defect. Related driver fact: the RA CAN-FD driver does
  NOT support CAN_MODE_ONE_SHOT, so auto-retransmission cannot be disabled and
  one unACKed frame becomes a sustained error storm. can_logger's boot
  self-transmit was therefore changed to a CLASSIC frame behind
  `#define BOOT_TX_USE_FD 0`.
- 2026-08-31: Phase 5 — CANFD0 pin decision RETRACTED and corrected. The
  2026-08-29 "FINAL CTX0=P203/CRX0=P202" choice, and its 2026-08-30 "user
  confirmed against Table 1.16" sign-off, were both WRONG: that pin table has
  two BGA224 columns and P202-P205 are GPIO only in the "BGA224 without MIPI"
  one. Table 1.13 confirms R7FA8D1BHECBD (PLBG0224GD-A) is the MIPI variant, so
  on our silicon those pins are permanently MIPI_CL_P/N + MIPI_DL0_P/N — and no
  board header breaks them out at all (physically confirmed). CONFIRMED FACT,
  closed. Corrected to CTX0=P704 / CRX0=P705 (Renesas FSP default) in both
  overlays; software-only change, the breadboard wiring was already on those
  pins and did not move. Lesson recorded: Zephyr RA pinctrl does not validate
  PSEL at build time, so three phases of clean builds proved nothing about pins.
- 2026-08-31: Phase 5 — "CAN + Ethernet concurrent" DOWNGRADED from a hard
  requirement to SEQUENTIAL USE MODES (capture over CAN, retrieve over Ethernet
  afterwards). Reason: P704/P705 are bus-switch-routed to the Ethernet PHY in
  the board hardware, not just multiplexed in pinctrl, so the two cannot run
  simultaneously on this board under any firmware. Costs no capability, only
  exact simultaneity, and matches real product use. Bench cost: the CAN
  transceiver's 2 wires must be physically moved off P704/P705 to test Ethernet
  and back to test CAN. Future work (not a blocker): find a real board SCHEMATIC
  — the MCU datasheet alone cannot settle the board-level routing.
- 2026-08-31: CLAUDE.md's "board is not physically available / build-only"
  constraint is STALE and was replaced — board, PCAN and rig are on the bench as
  of today. Anti-hallucination rule 3 (never claim a flash/test passed unless it
  was actually run this session) is unchanged and now matters more, not less.
- 2026-08-31 (session 2): **REVERSED the "pin Zephyr at f80761e4940" decision**
  on explicit user instruction — bumped to mainline HEAD `66e5135ffc3`. `west
  update` clean; all 6 apps build; can_logger/gpio_timer byte-identical. This is
  fine because both real bugs were fixed by local patches, not by the bump (the
  bump alone fixes neither). CLAUDE.md's "do not bump Zephyr" hard constraint is
  now stale — user overrode it.
- 2026-08-31 (session 2): **USB-HS CDC-ACM bulk transfer FIXED.** Root cause was
  NOT in Zephyr's usb subsys (which session-1 §3b checked) but in vendored FSP
  `modules/hal/renesas/.../r_usb_device.c`: `process_pipe_xfer()` never commits a
  bulk-IN zero-length packet (`BVAL` only re-asserted if already set). One-shot
  ZLP from `usbd_cdc_acm_enable()` therefore never completes -> `CDC_ACM_TX_FIFO_BUSY`
  stuck. Fix mirrors `pipe_xfer_in()`. `patches/0002-*`. Verified on hardware
  (host reads HELLO frame, both directions). The J-Link-VCOM-only decision from
  session 1 is lifted; USB CDC is a usable transport again.
- 2026-08-31 (session 2): **OSPI-B `flash_renesas_ra_ospi_b.c` drives the wrong
  chip-select on reset** (`RSTCS0` hardcoded; ek_ra8d1 NOR is on CS1). Real
  defect, fixed (`patches/0001-*`), worth upstreaming. **But it is NOT why
  `apps/flash_log` fails on our board** — bench probing shows the S28HL512T
  answers `0xFF` to JEDEC ID / RDSR / CFR2V pre- and post-reset and after a
  confirmed power cycle; the chip is electrically silent. Step 4 is blocked on a
  hardware check (U3 population, OSPI config links), not on Zephyr.
- 2026-08-31 (session 2): both upstream bugs written up.
- 2026-09-01 (session 2 cont.): installed `gh`, user auth'd, opened both PRs.
  Then found the USB ZLP bug is **already fixed upstream** by hal_renesas
  `f2c2aa6359e` (2026-08-04) -- literally the next commit after our pinned
  `f2eb9bc`. Closed our hal_renesas#220 as a dup; bumped
  `modules/hal/renesas` -> `f2c2aa6359e` (detached); removed `patches/0002`.
  Our independent diagnosis was correct -- good validation. **OSPI CS-reset PR
  [zephyr#117908](https://github.com/zephyrproject-rtos/zephyr/pull/117908)
  stays OPEN** (zephyr main still has the RSTCS0 hardcode); PR body says
  code-review-only since our board's flash is dead.
- 2026-08-31 (session 2): restored `C:\Python312\Lib` (+ `DLLs\*.pyd`) from the
  identical 3.12.3 install under `%LOCALAPPDATA%\Programs\Python\Python312` — the
  venv's base stdlib had been deleted, which was making every Zephyr build crawl.
- 2026-09-01 (session 3): **SCOPE CHANGE (user).** On-board flash logging is
  removed from scope. The project goal is now "USB + CAN + Ethernet work on the
  board" (all three now proven on hardware). Step 4 (`flash_log`) is abandoned —
  it was hardware-blocked anyway (silent OSPI NOR). No code removed; `can_logger`
  keeps its SRAM ring. CLAUDE.md's Mission still literally says "data logging" —
  not edited (rarely-change file), but this decision is operative.
- 2026-09-01 (session 3): **Step 5 (Ethernet / DoIP) PASS on hardware.** PHY
  links 100M full-duplex (slow, ~23 s), `ping 192.168.1.50` 6/6, and the full
  DoIP path — UDP Vehicle Identification -> Announcement, TCP Routing Activation
  -> success 0x10, TCP Diagnostic Message with UDS `22 F1 90` -> `62 F1 90` +
  VIN `EKRA8D1CANLOGGER0` — verified from a laptop Python client. Ethernet stack,
  UDP+TCP sockets and the DoIP/UDS skeleton are proven end-to-end on silicon.
- 2026-09-01 (session 3): **SW1 factory-default correction.** SW1 is the 8-way
  DIP switch by the RJ45/PHY (UM Fig. 1, silkscreen "SW1 / CONFIGURATION
  SWITCHES"). Factory default is **SW1-3 CAMERA ON, SW1-5 ETH-B OFF** — Ethernet
  is NOT connected to the PHY out of the box. The 2026-08-29 decision line that
  says 'the board uses ETH-B (SW1-5 ON)' is right about which mux Zephyr expects
  but wrong to imply it is the default. For Ethernet: SW1-5 ON + SW1-3 OFF
  (others OFF, SW1-7 SDRAM stays ON).
- 2026-09-01 (session 3): **Step 6 (MCUboot) PASS on hardware — Phase 5 bring-up
  COMPLETE.** Signed `can_logger` (ECDSA-P256, module debug key) chainloads from
  slot0 @ 0x20000 after the MCUboot banner; erasing the slot0 header makes
  MCUboot refuse (`E: Unable to find bootable image`). OVERWRITE_ONLY mode, no
  auto-revert. Nothing left to bring up — next work is the host GUI (CLP over
  J-Link VCOM) and the Phase 6 report.
