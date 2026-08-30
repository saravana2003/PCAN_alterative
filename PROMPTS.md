# PROMPTS.md — Copy-paste these into Claude Code, one phase at a time

Rules for using this file:
- Run ONE phase per session (or per few sessions if a phase is big). Don't
  paste two phases into one prompt.
- Start every session with: "Read CLAUDE.md and STATE.md before doing
  anything else." (Claude Code auto-loads CLAUDE.md, but say it anyway —
  cheap insurance.)
- After Claude Code confirms its understanding of the phase, paste the phase
  block below as the actual task.
- When the phase is done, tell it: "Update STATE.md with today's summary
  before we finish." Don't let a session end without that.
- Board is not physically available yet for Phases 0–4. Every prompt below
  says "build-only" — hold it to that; if it claims to have flashed or
  tested on hardware, that's a hallucination to correct immediately.

---

## PHASE 0 — Toolchain & Repo Init

```
Goal: get a clean, reproducible Zephyr build environment for the ek_ra8d1
board target, build-only (no hardware attached).

Do:
1. Install west and Zephyr SDK v0.16.6 or later (required for Cortex-M85
   GCC support). Verify the SDK version actually installed.
2. Initialize a west workspace for this project.
3. Confirm the ek_ra8d1 board target exists in this Zephyr checkout by
   listing it, not by assuming.
4. Build samples/hello_world for -b ek_ra8d1 and show me the build output.
   Do not attempt to flash — no board is connected.
5. Set up the repo structure: apps/ (our application code), boards/ (any
   overlay files we add later), docs/ (for the university report later).

Do not: write any application logic yet. Do not touch CAN, USB, Ethernet,
or flash code in this phase. Stop after hello_world builds cleanly and
report the result.
```

---

## PHASE 1 — Devicetree Verification & CAN Transceiver Overlay

```
Goal: confirm the devicetree nodes we depend on actually exist and compile,
and add an overlay for our external CAN transceiver's enable/standby pin.
Build-only, no hardware attached.

Do:
1. Grep the actual devicetree source (dts/arm/renesas/ra/ra8/*.dtsi and
   boards/renesas/ek_ra8d1/*.dts in this workspace) — don't assume from
   memory — and confirm the exact node names and status for: canfd0,
   canfd1, the on-chip ethernet + phy, usbfs, usbhs, and the ospi flash
   controller.
2. Report back exactly what you found, including whether canfd0/canfd1 are
   status "okay" by default or need enabling.
3. Create a devicetree overlay file for our external transceiver's
   enable/standby GPIO pin (I'll tell you which physical pin once I check
   the schematic — ask me for it, don't guess a pin number).
4. Build hello_world again with this overlay applied to confirm it doesn't
   break the build.

Do not: enable canfd0 as the active channel yet if the onboard 3-pin header
already claims it — flag this as an open question in STATE.md rather than
guessing which channel is free.
```

---

## PHASE 2A — CAN-FD Driver Module (build-only)

```
Goal: write a self-contained module that initializes canfd0 (or canfd1,
per Phase 1's finding), sets up standard + extended + FD filters, and
implements TX/RX with an interrupt-driven callback. Build-only.

Do:
1. Use Zephyr's native CAN API (can.h) — verify exact function signatures
   against the actual zephyr/include/zephyr/drivers/can.h in this
   workspace, don't recall them from memory.
2. Implement: driver init, one RX callback registered via ISR-driven
   interrupt (this is our "Interrupt + NVIC" deliverable for the
   university report — add a comment noting this explicitly), a TX
   function, and a simple frame struct (ID, DLC, flags, data, timestamp).
3. Add a Kconfig fragment / prj.conf entries needed for CAN-FD support,
   confirming each option actually exists.
4. Build only. No flashing.

Do not: touch Ethernet, USB, or flash storage code in this phase.
```

---

## PHASE 2B — GPIO Buttons/LEDs + Timer Module (build-only)

```
Goal: button-driven start/stop logging control, LED status indication, and
a periodic timer used for timestamping and periodic flash flush triggers.

Do:
1. Use gpio-keys devicetree node (already present per ek_ra8d1.dts) for the
   2 user buttons, gpio-leds for the 3 user LEDs.
2. Implement interrupt-driven button handling (start/stop capture) and LED
   state indication (e.g. green = idle, red blinking = logging).
3. Add a periodic timer (k_timer or the on-chip AGT counter — check which
   is simpler in this Zephyr version) used to timestamp CAN frames and to
   trigger periodic buffer flush. Add a comment noting this is our "Timer"
   deliverable for the university report.
4. Build only.

Do not: wire this to the CAN module yet — that's an integration step for
when hardware is available. Keep modules independent and build-testable
in isolation for now.
```

---

## PHASE 2C — USB CDC-ACM Device Module (build-only)

```
Goal: bring up USB as a CDC-ACM device so a laptop can enumerate this board
as a serial device — this is our primary GUI transport path.

Do:
1. Confirm the exact Kconfig/devicetree setup Zephyr's usb_cdc_acm sample
   uses for ek_ra8d1 (check zephyr/samples/subsys/usb/cdc_acm), don't
   assume it's identical to another board's setup.
2. Adapt it into our own module: define our simple binary frame protocol
   (I'll give you the exact struct layout — ask if I haven't yet) for
   sending logged/live CAN frames out over this CDC-ACM link, and for
   receiving TX commands from the GUI.
3. Build only.

Do not: implement USB Host / mass storage in this phase — that's a
separate phase.
```

---

## PHASE 2D — Littlefs on Octo-SPI Flash Module (build-only)

```
Goal: persistent log storage on the external Octo-SPI flash using littlefs.

Do:
1. Confirm the ospi flash controller node and its partition setup in the
   board's devicetree (grep, don't assume).
2. Set up a littlefs filesystem mount on a defined partition.
3. Implement: open a new log file on "start logging" trigger, append
   timestamped CAN frames in batches (not one write per frame — batch to
   reduce flash wear and match our earlier design decision in STATE.md),
   close cleanly on "stop logging."
4. Build only.

Do not: assume SDRAM is used as an intermediate buffer unless STATE.md's
open question about SDRAM population has been resolved — default to an
SRAM ring buffer per the decisions log.
```

---

## PHASE 2E — Ethernet / Log-Retrieval Skeleton (build-only)

```
Goal: bring up the on-chip Ethernet with a static IP, and a minimal
DoIP-style responder skeleton for log retrieval (vehicle announcement +
routing activation + a simple data request/response — full UDS semantics
can come later).

Do:
1. Confirm ethernet + phy devicetree nodes and the SW1 switch requirement
   (SW1-5 ETHB = ON per the board docs) — note this as a hardware setup
   reminder in STATE.md, since it can't be verified without the board.
2. Configure a static IP (ask me for the address to use, don't invent one).
3. Implement a minimal DoIP-style message skeleton: vehicle identification
   response, routing activation response, and a placeholder data-download
   request/response pair. Keep it simple — this does not need to be a full
   ISO 13400 implementation for this project's scope.
4. Build only.
```

---

## PHASE 3 — Bootloader (MCUboot)

```
Goal: integrate MCUboot as the bootloader for our application image, using
Zephyr's sysbuild support, giving us image versioning/rollback and a first
step toward a verified boot chain.

Do:
1. Confirm ek_ra8d1 + MCUboot compatibility by checking actual sysbuild
   samples/config in this Zephyr checkout — don't assume support exists
   without checking.
2. Set up sysbuild so our application builds as an MCUboot-bootable image.
3. Build only — do not attempt to flash/verify boot behavior (needs
   hardware).
4. Note in STATE.md whether this MCUboot setup could later carry a signing
   key as a stepping stone toward the SecOC-style message authentication
   we discussed, even without full TrustZone.
```

---

## PHASE 4 — TrustZone Feasibility Spike (time-boxed, max ~1 day)

```
Goal: produce a written go/no-go conclusion on whether TrustZone is
practically achievable for this board within our timeline — NOT a working
TrustZone implementation. This is a research task.

Do:
1. Search the actual Zephyr repo (not memory) for any Renesas RA8-family
   board with SAU/TF-M support, and for any open PRs/issues discussing
   ek_ra8d1 TrustZone support.
2. Check Renesas's own FSP (Flexible Software Package) documentation for
   whether TrustZone is supported outside Zephyr (e.g. via e2 studio) —
   this tells us if the silicon-level support could theoretically be
   ported.
3. Write a short feasibility report (docs/trustzone_feasibility.md):
   what would be required, rough effort estimate, and a clear
   recommendation: attempt it, or document it as future work.

Do not write any SAU/TF-M code in this phase unless the feasibility report
concludes it's realistic within remaining time — check with me first.
```

---

## PHASE 5 — Hardware Bring-Up (once board is physically available)

```
Not written yet — revisit CLAUDE.md and STATE.md once the board arrives,
and write this phase's prompt based on which Phase 0–4 modules are ready
to integrate and flash. Likely order: flash hello_world first to confirm
J-Link connectivity, then bring up each module from Phase 2 individually
with real hardware before integrating them together.
```

---

## PHASE 6 — Documentation (do this LAST, after real results exist)

```
Goal: assemble the BITS Pilani WILP Embedded Systems Design report from
STATE.md's decisions log and real build/test output — do not write
speculative or fabricated results.

Do:
1. Read STATE.md's full decisions log and completed-phases history.
2. Draft the report using this mapping to the assignment's rubric:
   - Problem ID: cheap PCAN alternative for automotive CAN logging.
   - System Design: block diagram of RA8D1 + CAN transceiver + external
     Octo-SPI flash + Ethernet PHY + USB, with Zephyr as the RTOS layer.
   - Firmware Design (ADC/Timer/Interrupt): CAN-FD RX interrupt (NVIC) +
     AGT/SysTick timer for timestamping/flush triggers. Note ADC was
     out of scope for this iteration if it wasn't added.
   - Communication/Integration (SPI/I2C/UART/DMA): Octo-SPI flash logging
     (SPI-family, master/slave, note actual clock rate used) + UART debug
     console. CAN-FD and Ethernet documented as the project's primary,
     more domain-relevant communication channels beyond the rubric's
     illustrative list.
   - Implementation Plan / Outcomes / Validation: pull directly from
     STATE.md's dated log and any serial/log captures we actually have —
     do not invent metrics we didn't measure.
3. Flag any rubric item we genuinely didn't complete (e.g. TrustZone) as
   "documented as future work, see docs/trustzone_feasibility.md" rather
   than glossing over it.

Do not fabricate screenshots, oscilloscope traces, or performance numbers.
Only use what's actually in this repo's history.
```
