# Zephyr bug report (draft): `flash_renesas_ra_ospi_b` resets the wrong chip-select line

**Status:** found 2026-08-31 during Phase 5 Step 4 bring-up. Fixed locally
(`patches/0001-flash-renesas-ra-ospi-b-reset-correct-cs.patch`). **Not yet
reported upstream** — no `gh` CLI on the build box.

**Code defect: CONFIRMED by source inspection — worth an upstream fix.**
**That it caused our EK-RA8D1 `CFR2V verify` failure: DISPROVEN on the bench
(2026-08-31).** That failure is an electrical/board problem on this unit (the
S28HL512T answers `0xFF` to everything, before and after reset, warm and cold —
see "Hardware test status"). File the CS fix on its own merits; do not tie it to
a functional regression.

## Filing this upstream

- Repo: **`zephyrproject-rtos/zephyr`**, `drivers/flash/flash_renesas_ra_ospi_b.c`.
- Title: *drivers: flash: flash_renesas_ra_ospi_b: device reset always drives
  `RSTCS0`, never `RSTCS1` for a CS1 flash*
- Body: "The defect" + "Suggested fix" sections below; attach the patch diff.
  Include the secondary note that the failure reasons in `spi_mode_init()` are
  all `LOG_DBG`.

---

## The defect

`drivers/flash/flash_renesas_ra_ospi_b.c` configures the attached flash on
**Chip-Select 1**:

```c
/* flash_renesas_ra_ospi_b.c:757 */
.ospi_b_config_extend = {.channel = OSPI_B_DEVICE_NUMBER_1,
```

`OSPI_B_DEVICE_NUMBER_1` is documented in the HAL as *"Device connected to
Chip-Select 1"* (`modules/hal/renesas/drivers/ra/fsp/inc/instances/r_ospi_b.h`).

But the device-reset step in `flash_renesas_ra_ospi_b_spi_mode_init()` drives
the reset line for **Chip-Select 0**:

```c
/* flash_renesas_ra_ospi_b.c:174-178 */
/* Reset flash device by driving OM_RESET pin */
R_XSPI0->LIOCTL_b.RSTCS0 = 0;
k_sleep(K_USEC(500));
R_XSPI0->LIOCTL_b.RSTCS0 = 1;
k_sleep(K_NSEC(50));
```

`RSTCS0` and `RSTCS1` are distinct bits in `LIOCTL`
(`R7FA8D1BH.h:23213-23214`):

```
__IOM uint32_t RSTCS0 : 1;  /* [16..16] Reset drive for slave 0 */
__IOM uint32_t RSTCS1 : 1;  /* [17..17] Reset drive for slave 1 */
```

`grep -n "RSTCS" drivers/flash/flash_renesas_ra_ospi_b.c` returns only the two
`RSTCS0` lines. `RSTCS1` is never written.

**Result: on any board whose OSPI flash is on CS1, the driver's "reset flash
device" step resets nothing.** The reset should target the line matching
`channel` (or the DT-selected device), not a hardcoded CS0.

This is board-visible on `ek_ra8d1`, whose OSPI NOR is on CS1:
- `boards/renesas/ek_ra8d1/ek_ra8d1-pinctrl.dtsi` `ospi0_default` group2 is
  commented `/* cs1 rst ecsint1 */`
- the `s28hl512t` node is at `reg = <0x90000000 0x4000000>` (the CS1 window)

---

## Observed failure (hypothesis: caused by the above)

On EK-RA8D1 with `apps/flash_log`, OSPI init fails at boot, every time, on a
warm (J-Link / `AIRCR.SYSRESETREQ`) reset:

```
<dbg> flash_renesas_ra_ospi_b.flash_renesas_ra_ospi_b_spi_mode_init:
        Verify CFR2V register data Failed
<err> flash_renesas_ra_ospi_b: Init SPI mode failed
<inf> littlefs: LittleFS version 2.11, disk version 2.1
<err> littlefs: can't open flash area 0, err -19
<err> fs: fs mount error (-19)
```

Note the shape of it:

- `R_OSPI_B_Open()` succeeds.
- `R_OSPI_B_SpiProtocolSet(SPI_FLASH_PROTOCOL_EXTENDED_SPI)` succeeds.
- The write-enable, CFR2V write and CFR3V write all return `FSP_SUCCESS`.
- The CFR2V **read-back transfer also succeeds** — no "Read back CFR2V register
  Failed".
- Only the **compare** fails: the value read is not `DATA_CFR2V_REGISTER`
  (`0x83`, `flash_renesas_ra_ospi_b.h:62`).

So the controller is transacting happily; the device is simply not accepting or
not reflecting the register writes. That is what you would expect if the flash
is sitting in a protocol mode where those 1S-1S-1S register commands are not
interpreted as intended — precisely the state the missing hardware reset is
supposed to clear.

**Prediction that would confirm it:** a full **power cycle** (which resets the
flash through its own power-on reset) should let init succeed, while a warm
reset should keep failing. If that holds, the missing `RSTCS1` is the reason
warm resets never recover.

**Not yet ruled out** (be honest about this upstream): a board-level wiring or
population issue, or an unrelated latency/timing mismatch in the CFR2V/CFR3V
values for this specific part.

---

## Environment

| item | value |
|---|---|
| Board | `ek_ra8d1` (EK-RA8D1 v1.0) |
| MCU | R7FA8D1BHECBD, Cortex-M85 |
| Flash | Infineon/Cypress S28HL512T (U3, S28HL512TFPBHI010), 64 MB, on **CS1** |
| Zephyr | mainline `v4.4.99`, commit `f80761e4940` |
| hal_renesas | `f2eb9bc` (FSP 6.2.0) |
| Driver | `CONFIG_FLASH_RENESAS_RA_OSPI_B`, `drivers/flash/flash_renesas_ra_ospi_b.c` |
| DT | `ospi0` + `s28hl512t` both `status = "okay"` in the stock board DTS |

To see the DBG line, build with `CONFIG_FLASH_LOG_LEVEL_DBG=y` — the failure
reasons inside `spi_mode_init()` are all `LOG_DBG` and are invisible at the
default level, which makes this much harder to diagnose than it needs to be.
Arguably a second, smaller issue: `Init SPI mode failed` at `LOG_ERR` tells the
user nothing actionable.

---

## Suggested fix

Drive the reset bit that corresponds to the configured channel, e.g. select
`RSTCS0`/`RSTCS1` from `p_ctrl->channel` (which the same function already uses
correctly two lines earlier for `LIOCFGCS_b[p_ctrl->channel].DDRSMPEX`).

Note the inconsistency is *within one function*: `DDRSMPEX` is indexed by
`p_ctrl->channel`, while the reset immediately below it is hardcoded to CS0.

---

## Local fix applied in this workspace (2026-08-31)

`drivers/flash/flash_renesas_ra_ospi_b.c`, in
`flash_renesas_ra_ospi_b_spi_mode_init()`:

```c
/* Reset flash device by driving the OM_RESET pin of the configured
 * chip-select. ... */
if (p_ctrl->channel == OSPI_B_DEVICE_NUMBER_1) {
        R_XSPI0->LIOCTL_b.RSTCS1 = 0;
        k_sleep(K_USEC(500));
        R_XSPI0->LIOCTL_b.RSTCS1 = 1;
} else {
        R_XSPI0->LIOCTL_b.RSTCS0 = 0;
        k_sleep(K_USEC(500));
        R_XSPI0->LIOCTL_b.RSTCS0 = 1;
}
k_sleep(K_NSEC(50));
```

`zephyr/` is a west-managed tree and is git-ignored by this repo, so the change
is also kept as `patches/0001-flash-renesas-ra-ospi-b-reset-correct-cs.patch`
and must be re-applied after any `west update` (see `patches/README.md`).

### Hardware test status (2026-08-31) — DEFECT IS REAL, HYPOTHESIS DISPROVEN

Tree: Zephyr `66e5135ffc3` + patch 0001. Tested on the bench with a heartbeat
diagnostic build of `apps/flash_log`.

- The **code defect stands**: `RSTCS0`/`RSTCS1` are distinct bits and the driver
  only ever drives `RSTCS0` while `ek_ra8d1`'s NOR is on CS1. Still worth an
  upstream fix. Patch: `patches/0001-flash-renesas-ra-ospi-b-reset-correct-cs.patch`.
- **BUT it does not cause the `ek_ra8d1` failure, and the "power cycle fixes it"
  prediction is FALSE.** Measured, with the CS1 reset patched in:

  ```
  BRINGUP pre-reset  jedec err=0 data=0xffffffff
  BRINGUP post-reset jedec err=0 data=0xffffffff ; status 0xff ;
          LIOCTL=0x00030003 LIOCFGCS1=0x10010000
  CFR2V verify: got 0xff want 0x83
  spi_mode_init -> -5   (identical on warm reset AND on a confirmed full power cycle)
  ```

  `R_OSPI_B_DirectTransfer` returns `FSP_SUCCESS` every time — the **controller**
  transacts fine — but the S28HL512T answers **`0xFF` to every command**
  (JEDEC ID, RDSR, CFR2V), before *and* after the reset pulse, and after a real
  power cycle. `LIOCTL = 0x0003_0003` shows both reset lines de-asserted. The
  flash is simply **not electrically responding**.

- **Conclusion: this is a board/hardware issue on this unit, not a Zephyr bug.**
  The OSPI NOR (U3, S28HL512T) and/or its config links on the EK-RA8D1's
  "MCU Native Pin Access" area need a physical check against the board schematic
  (CLAUDE.md already flags "not populated by default — confirm before assuming").
  Bring-up **Step 4 (`apps/flash_log`) is blocked on hardware.**
