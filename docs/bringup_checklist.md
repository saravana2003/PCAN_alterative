# Hardware Bring-Up Checklist — EK-RA8D1

**Status (updated 2026-08-31, first hardware session):**

| step | app | result |
|---|---|---|
| 0 | `hello_world` | **PASS** — debug link, flash tool, serial console all good |
| 1 | `gpio_timer` | **PASS** — LEDs, both buttons, timer measured at 100.26 ms/tick |
| 2 | `can_logger` | **PASS** — CAN-FD proven both directions vs PCAN (FD/BRS deferred, see below) |
| 3 | `usb_cdc` | **BLOCKED** — our bug fixed + USB enumerates, but bulk transfer is broken *inside Zephyr's RA USB-HS stack*. See `zephyr_usb_hs_bug.md`. Do not re-debug as an app bug. |
| 4 | `flash_log` | **NEXT** |
| 5 | `eth_doip` | not started |
| 6 | MCUboot | not started (unblocked — step 2 passes) |

Full detail for every result is in `STATE.md`. Work top to bottom. Do not start
a step until the previous one either passes or its failure is fully understood
and written down.

---

## The core principle

**Flash one isolated Phase-2 app at a time. Never a combined image — it doesn't
exist yet.**

This is what keeps a bug local instead of letting it ripple. If only
`can_logger` is flashed and CAN misbehaves, you know *with certainty* it's CAN —
not an interaction with USB or flash-logging code that happens to be running
alongside it. The moment you flash a combined image, every failure becomes a
search across the whole system.

Log every result as you go, pass or fail. A failure you understood and recorded
is progress; a failure you skipped past will cost you twice later.

---

## Before powering anything on

- [ ] **Commit or zip the current repo state as a clean baseline.** Everything
      up to here is build-verified; you want an unambiguous "before hardware"
      point to return to.
- [ ] **Check wiring against the CORRECTED pin decisions — read the table, don't
      trust memory:**
      - transceiver **TX ← board pin P704** (CTX0)
      - transceiver **RX → board pin P705** (CRX0)
      - transceiver **STB tied to GND** (permanent normal mode; there is no MCU
        GPIO for it — the enable/standby overlay was cancelled in Phase 1)

      > **CORRECTED 2026-08-31 — the old P203/P202 instruction was WRONG.**
      > R7FA8D1BHECBD is the MIPI package variant (datasheet Table 1.13,
      > PLBG0224GD-A), so P202-P205 are permanently MIPI display signals, are
      > not GPIO, and are not broken out on any header of this board. The
      > earlier "confirmed against Table 1.16" check read the datasheet's
      > "BGA224 without MIPI" column by mistake. P704/P705 is the Renesas FSP
      > default and is what the breadboard is already wired to — **no rewiring
      > is needed, this was a software-only fix.** Full reasoning: STATE.md,
      > "CANFD0 pin decision".
- [ ] **Check CAN bus termination.** Missing termination is the single most
      common cause of the error storms you'll otherwise chase in software.
- [ ] **PCAN software installed and ready** on the laptop.
- [ ] **Laptop Ethernet adapter set to `192.168.1.10/24`**, so it can reach the
      board's static `192.168.1.50` at Step 5.

---

## Step 0 — Debug link sanity

**Flash plain `hello_world`.**

- [ ] The flash tool completes without error.
- [ ] Console output appears.

> **If this fails, STOP.** Nothing downstream can be trusted until J-Link,
> wiring, and power are sorted. Do not "try the next app to see if it works" —
> it won't, and you'll misread the result.

---

## Step 1 — GPIO / LEDs / buttons

**App: `gpio_timer`.** Needs nothing external.

- [ ] LED idle colour matches what the code assumes (`led2` = green idle,
      `led3` = red while logging). **If the silkscreen and the docs disagree,
      trust the silkscreen** and change `LED_*_NODE` — it's a one-line edit.
- [ ] **S1** flips to the logging LED state.
- [ ] **S2** flips back.
- [ ] **No double-triggers from a single press** (switch bounce).
- [ ] Console shows the timer tick counting at the expected rate.

---

## Step 2 — CAN-FD

**App: `can_logger`, plain build — NOT the MCUboot/sysbuild one yet.**

- [x] Send a **standard-ID** frame in range from PCAN → board receives it.
      (`id=0x00000123 dlc=8 flags=0x00`)
- [x] Send an **extended-ID** frame → the catch-all filter gets it.
      (`id=0x18daf110 dlc=8 flags=0x01` = `CAN_FRAME_IDE`, 29-bit ID intact)
- [ ] ~~Send an **FD frame with BRS**~~ — **NOT POSSIBLE WITH THIS RIG.** The
      bench analyser is a **PCAN-USB, classic CAN only**; it cannot generate or
      decode CAN-FD. Needs a PCAN-USB **FD**. Deferred, not failed.
- [x] **Trigger a TX from the board** → PCAN receives ID `0x123`, 8 bytes
      `00..07`, on S3 reset.
- [x] **Bus statistics clean** — BUSOK maintained, no form/CRC/ACK errors.

> **Do not let the board transmit an FD frame onto a classic-only bus.** The
> analyser emits error frames, the RA controller auto-retransmits, and
> `CAN_MODE_ONE_SHOT` is not supported by this driver — so one frame becomes an
> unbounded error storm (PCAN shows BUSHEAVY instantly). `can_logger`'s boot
> frame is therefore classic, behind `#define BOOT_TX_USE_FD 0` in
> `apps/can_logger/src/main.c`. Flip it to 1 when FD-capable gear arrives.

> Form/CRC/ACK errors almost always mean **TX and RX are swapped, or a
> termination resistor is missing** — a wiring fault, not a software bug. Go
> back to the wiring before touching code.

---

## Step 3 — USB CDC-ACM — **BLOCKED UPSTREAM, DO NOT RE-DEBUG**

**App: `usb_cdc`, on the USB-HS port.**

- [x] OS enumerates a serial device. (`USB Serial Device (COMn)`,
      `VID_2FE3&PID_0001`, High Speed)
- [x] The port opens; DTR is detected by the device.
- [x] The **CLP self-test passes** on hardware every boot.
- [ ] **Data over the link — BROKEN IN ZEPHYR, NOT IN OUR CODE.** Bulk transfers
      never reach the host. Proven with Zephyr's **own unmodified** `cdc_acm`
      sample, and confirmed still unfixed in current mainline.
      Full write-up + upstream repro: **`docs/zephyr_usb_hs_bug.md`**.

> **The GUI is NOT blocked by this.** CLP is transport-agnostic — byte stream
> in, byte stream out — so the project uses the **J-Link VCOM/UART** as the GUI
> transport instead. Revisit USB if upstream fixes the RA UDC bulk path, or try
> the Full-Speed (`usbfs`) route, which is a different UDC path and is not yet
> tested.

---

## Step 4 — Flash logging  ← **START HERE**

**App: `flash_log`.** Needs no USB and no CAN, so nothing above blocks it.

- [ ] The littlefs mount succeeds on first boot.
- [ ] The synthetic-frame test runs.
- [ ] **Power-cycle the board and confirm the data survived the reset.**

> That last check is the whole point of persistent storage, and it is precisely
> the property a build-only session could never have proven. Don't skip it.

---

## Step 5 — Ethernet / DoIP

**App: `eth_doip`.**

> **STOP — physically move the CAN wires first.** P704/P705 (CAN) are
> bus-switch-routed to the Ethernet PHY on this board, so CAN and Ethernet are
> not electrically safe to run at the same time regardless of firmware.
> Disconnect the transceiver's two wires from P704/P705 before this step, and
> reconnect them before going back to any CAN step. Project decision
> (2026-08-31): CAN and Ethernet are **sequential use modes** — capture over
> CAN, retrieve over Ethernet afterwards.

- [ ] **SW1-5 is in the ETHB position** the code assumes.
- [ ] `ping 192.168.1.50` from the laptop answers.

> If nothing answers: check the **RJ45 link LED** and the **PHY-negotiation
> lines in the serial console** *before* assuming it's a code bug. No link means
> no amount of code reading will help.

---

## Step 6 — MCUboot

**Only after Step 2 has confirmed plain `can_logger` genuinely works.** Bringing
up the bootloader over an app you haven't proven gives you two unknowns stacked
on each other.

- [ ] Flash the signed sysbuild image.
- [ ] **MCUboot's banner appears before the app's own startup.**
- [ ] *(If time allows)* Deliberately flash a **bad/unsigned image** and confirm
      it is **rejected** — that rejection is the actual feature under test. A
      bootloader that boots everything you give it has not been tested.

---

## Notes carried in from build-only phases

These are the things that could not be checked without silicon. Expect them to
be where the surprises are:

- Every app in `apps/` (`can_logger`, `gpio_timer`, `usb_cdc`, `flash_log`,
  `eth_doip`) is build-only and untested on silicon. So is the MCUboot chain.
- **SDRAM population on this board revision is still unconfirmed** (open question
  in STATE.md). The logging design assumes SRAM-only buffering by decision, so
  this should not block Step 4 — but confirm it while the board is in front of
  you.
- `usbfs` is **not** enabled by the stock board DTS (only `usbhs` is). Step 3
  targets USB-HS deliberately.
- Onboard 3-pin CAN header population / jumper state was never verifiable
  without the board. We use our own breadboard transceiver regardless — but if
  Step 2 produces bus errors, check whether the onboard header is contending.
