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
- **Board is not physically available until further notice.** All work until
  then must be build-only (`west build`), never assume flash/run succeeded.

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
