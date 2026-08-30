# Hardware Bring-Up Checklist — EK-RA8D1

**Status:** plan only. Nothing here has been executed. Every Phase 0–4 result to
date is **build-only** — no image in this repo has ever run on silicon.

**Use this the day the board and PCAN arrive.** Work top to bottom. Do not start
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
- [ ] **Check wiring against the confirmed pin decisions — read the table, don't
      trust memory:**
      - transceiver **TX ← board pin P203** (CTX0)
      - transceiver **RX → board pin P202** (CRX0)
      - transceiver **STB tied to GND** (permanent normal mode; there is no MCU
        GPIO for it — the enable/standby overlay was cancelled in Phase 1)
      - *(mapping confirmed against pin list 2329.pdf, Table 1.16)*
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

- [ ] Send a **standard-ID** frame in range from PCAN → board receives it.
- [ ] Send an **extended-ID** frame → the catch-all filter gets it.
- [ ] Send an **FD frame with the bit-rate switch (BRS) flag** → handled.
- [ ] **Trigger a TX from the board** → PCAN sees it with the right ID, DLC,
      and data.
- [ ] **Watch PCAN's bus statistics for form / CRC / ACK errors.**

> Form/CRC/ACK errors almost always mean **TX and RX are swapped, or a
> termination resistor is missing** — a wiring fault, not a software bug. Go
> back to the wiring before touching code.

---

## Step 3 — USB CDC-ACM

**App: `usb_cdc`, on the USB-HS port.**

- [ ] OS enumerates a serial device.
- [ ] The port opens.
- [ ] The **CLP self-test output** appears.

> **Get this fully right before touching the GUI at all.** The GUI's entire job
> is speaking CLP over this link — prove the link and the protocol here, or
> you'll be debugging two unproven layers at once.

---

## Step 4 — Flash logging

**App: `flash_log`.**

- [ ] The littlefs mount succeeds on first boot.
- [ ] The synthetic-frame test runs.
- [ ] **Power-cycle the board and confirm the data survived the reset.**

> That last check is the whole point of persistent storage, and it is precisely
> the property a build-only session could never have proven. Don't skip it.

---

## Step 5 — Ethernet / DoIP

**App: `eth_doip`.**

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
