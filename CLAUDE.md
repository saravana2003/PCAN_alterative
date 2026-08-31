# CLAUDE.md — Project North Star (read this first, every session)

## Mission (do not deviate from this)
Build a low-cost, Zephyr-RTOS-based alternative to PCAN for automotive CAN data
logging and bus interaction, running on the Renesas EK-RA8D1 (R7FA8D1BHECBD,
Cortex-M85). Secondary goal: use this project to learn Cortex-M85 features
(MPU, security architecture) and Zephyr internals. Tertiary goal: this project
doubles as a BITS Pilani WILP Embedded Systems Design assignment — see
PROMPTS.md's final "Documentation" phase for that mapping. Do not optimize for
the assignment at the expense of the real engineering goal; the real project
comes first, the report is assembled from real results afterward.

## Hardware (do not assume anything beyond this list)
- Board: Renesas EK-RA8D1 v1.0, P/N RTK7EKA8D1S00001BE
- MCU: R7FA8D1BHECBD (Cortex-M85, 480 MHz)
- External CAN transceiver on breadboard, wired to CAN-FD pins, breakout to
  DB9 for connection to a real PCAN unit (used as ground-truth test rig)
- 512 Mb External Octo-SPI Flash, 512 Mb SDRAM (both on MCU Native Pin Access
  header, not populated by default — confirm before assuming either is wired)
- Onboard: 3 user LEDs, 2 user buttons, RJ45 Ethernet (100BASE-TX via RMII),
  2x USB micro-AB (Full Speed + High Speed)
- **Board, PCAN unit and test rig are PHYSICALLY AVAILABLE as of 2026-08-31.**
  The old "build-only, board not available" constraint is STALE and no longer
  applies: we now flash and test on real hardware (Phase 5 bring-up,
  `docs/bringup_checklist.md`). Anti-hallucination rule 3 still stands in
  full — never claim a flash or a test passed unless you actually ran it this
  session and saw the output.

## Hard constraints (confirmed facts — do not contradict these)
- TrustZone on `ek_ra8d1` is CPU-reachable but NOT usable in mainline Zephyr.
  The CPU side is real: `soc/renesas/ra/ra8d1/Kconfig:9` selects
  `CPU_HAS_ARM_SAU` -> `CPU_HAS_TEE` -> `ARMV8_M_SE`, so `ARM_TRUSTZONE_M` can
  be enabled and a secure image will even build. Everything above the CPU is
  missing: no CPSCU/PSCU driver or binding (so per-peripheral secure/non-secure
  attribution cannot be set), no non-secure devicetree aliases, no `/ns` board
  variant on any Renesas board, and no TF-M port for any Renesas device. Full
  findings: `docs/trustzone_feasibility.md`.
  **DECISION: NO-GO on TrustZone for this project. Do not revisit unless the
  user explicitly asks.**
- Security mechanism for this project is RSIP-E51A hardware-wrapped-key
  AES-128 CMAC, NOT TrustZone. The engine is already compiled into our
  `ek_ra8d1` builds (`CONFIG_HAS_RENESAS_RA_RSIP_E51A=1`, per Phase 4), and
  its CMAC API operates on a wrapped key index rather than a plaintext key.
  This is the locked-in direction for a future SecOC-style phase. NOT
  implemented yet, and this bullet is NOT a green light to start coding it —
  wait for an explicit go-ahead.
- CAN-FD is on-chip (`renesas,ra-canfd`), two channels (`canfd0`, `canfd1`).
  The external hardware is a transceiver only — do not write code assuming
  an SPI-based CAN controller IC.
- **This part is the MIPI variant — P202..P205 do not exist as GPIO.**
  Datasheet Table 1.13 (Product List) confirms R7FA8D1BHECBD is package
  PLBG0224GD-A, the MIPI variant. The datasheet's pin-function table has TWO
  BGA224 columns ("BGA224" and "BGA224 without MIPI"); P202/P203/P204/P205
  appear as GPIO only in the *without MIPI* column. On our silicon those four
  pins are permanently MIPI_CL_P/N and MIPI_DL0_P/N, and no header on the
  EK-RA8D1 breaks them out (physically confirmed on the board). Any pin
  decision that lands on Port 2 pins 2-5 is wrong by construction. CONFIRMED
  FACT — do not re-litigate.
- **CANFD0 pins are CTX0=P704 / CRX0=P705** (the Renesas FSP default), set by
  `boards/ek_ra8d1.overlay` and `apps/can_logger/boards/ek_ra8d1.overlay`.
  This matches the physical breadboard wiring; the wiring does not change.
- **CAN-FD and on-chip Ethernet are MUTUALLY EXCLUSIVE on this board.**
  P704/P705 are RMII0_RX_ER_B / RMII0_CRS_DV_B in the board's ETHERNET-B mux
  and are bus-switch-routed to the Ethernet PHY in hardware — a board routing
  fact, not just a pinctrl choice. DECISION (2026-08-31): "CAN + Ethernet
  concurrent" is downgraded from a hard requirement to SEQUENTIAL USE MODES
  (capture over CAN, retrieve logs over Ethernet afterwards). Costs no
  capability, only exact simultaneity. On the bench this means physically
  moving the transceiver's two wires off P704/P705 before Ethernet testing
  and back before CAN testing, until a real board SCHEMATIC (not the MCU
  datasheet) turns up a routing that avoids it — that search is future work.
- **Zephyr is pinned to commit `f80761e4940`** (detached HEAD in `zephyr/`, on
  purpose). Every hardware result this project has was validated on it. A later
  mainline (`66e5135ffc3`) was checked and changes NOTHING relevant — zero
  commits touch `drivers/usb/`, `subsys/usb/`, `soc/renesas`, the board, its
  DTS, or the CAN driver. Do not bump Zephyr hoping to fix USB.
- **USB CDC-ACM bulk transfer is BROKEN IN ZEPHYR on this board, not in our
  code.** USB-HS enumerates and control transfers work, but bulk data never
  reaches the host. Reproduced with Zephyr's OWN unmodified
  `samples/subsys/usb/cdc_acm`. Do not re-debug this as an application bug.
  Full write-up + upstream-ready repro: `docs/zephyr_usb_hs_bug.md`.
  **The GUI transport is therefore the J-Link VCOM/UART**, not USB. CLP is
  transport-agnostic (byte stream in, byte stream out), so no protocol change
  is needed and `docs/clp_protocol.md` still stands.
- **The bench PCAN-USB is CLASSIC CAN ONLY.** CAN-FD and BRS cannot be
  validated with current equipment — that needs a PCAN-USB **FD**. Do not
  present FD as tested. Also: this driver does NOT support
  `CAN_MODE_ONE_SHOT`, so auto-retransmission cannot be disabled and one
  unACKed frame becomes a sustained error storm. `can_logger`'s boot frame is
  classic, behind `#define BOOT_TX_USE_FD 0`.
- **Renesas DLM lock (SOLVED, do not re-diagnose):** this board shipped in DLM
  state OEM_PL2 / AL2, which made code flash above 32 KiB unerasable and
  produced misleading J-Link "CRC timeout / failed to erase" errors. Fixed with
  Renesas Flash Programmer -> "Initialize Device". If flash behaves
  inexplicably on an RA part, check the lifecycle state with the VENDOR tool
  early — neither J-Link nor pyocd can see it.
- SDK requirement: Zephyr SDK v0.16.6 or later (needed for Cortex-M85 GCC).
- Runners: jlink (default) or pyocd — both require the physical board.

## Anti-hallucination rules (follow these mechanically)
1. Never state or write code using a Kconfig symbol, devicetree property, or
   Zephyr API you have not verified. Grep the actual `zephyr/` and
   `modules/hal_renesas/` source in this workspace before using it.
2. If something can't be verified from the workspace (e.g. requires physical
   hardware behavior), say so explicitly instead of guessing.
3. Never claim a build, flash, or test "succeeded" unless you actually ran
   the command in this session and saw the output.
4. Stay inside the scope of the current phase prompt (see PROMPTS.md). If a
   task seems to require going outside that scope, stop and ask instead of
   expanding scope on your own.

## Session protocol
1. Read this file and STATE.md in full before doing anything else.
2. State back, in 2 sentences, which phase you're on and today's single
   deliverable, per STATE.md's "Next Steps". Wait for confirmation before
   writing code.
3. Work only on that deliverable.
4. Before ending the session, append a dated entry to STATE.md summarizing
   what changed and what's next. Keep it short — bullet points, not prose.

## Where things live
- Phase-by-phase task prompts: `PROMPTS.md`
- Session-to-session progress log: `STATE.md`
- This file should rarely change; STATE.md changes every session.
