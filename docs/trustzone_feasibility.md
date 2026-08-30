# TrustZone Feasibility Spike — EK-RA8D1 / R7FA8D1BHECBD

**Phase 4 deliverable (PROMPTS.md).** Research only — no SAU/TF-M/RSIP driver
code was written. Build-only; the board is still not physically available.

- **Date:** 2026-08-30
- **Zephyr:** mainline v4.4.99, commit `f80761e` (2026-08-28)
- **hal_renesas:** commit `f2eb9bc` (2026-07-17), vendors **FSP 6.2.0**
  (`modules/hal/renesas/drivers/ra/fsp/inc/fsp_version.h:31-37`)
- **Datasheet:** RA8D1 Group Datasheet **R01DS0416EJ0130 Rev.1.30, Dec 19 2025**,
  supplied as `security_and_trustzone_research.pdf` (Appendices 3–5, pp. 184–190)

---

## 1. Verdict

> **NO-GO on TrustZone. Document as future work.**
>
> **But the spike found something better for our actual goal:** the RSIP-E51A
> security engine — including **AES-128 CMAC with hardware-wrapped keys** — is
> already compiled into our existing `ek_ra8d1` builds. A hardware-backed SecOC
> key store is reachable **today, without TrustZone**. See §5.

TrustZone on `ek_ra8d1` in mainline Zephyr is not a "turn on a Kconfig" job and
not a one-week job. It is an upstream SoC-support effort (§4.3) that additionally
**cannot be validated without the physical board plus Renesas Flash Programmer**,
because the secure/non-secure boundary is programmed into the device, not into
the image.

---

## 2. Correction to a "hard constraint" in CLAUDE.md

CLAUDE.md currently states:

> Zephyr `ek_ra8d1` board target has NO TrustZone / Secure-Non-Secure split
> today: **no SAU binding**, no `/ns` build variant, only plain ARMv8.1-M MPU is
> in the supported-features list.

Two of those three claims hold. **"No SAU binding" is wrong** and should be
corrected — the SoC *does* declare SAU hardware:

```
soc/renesas/ra/ra8d1/Kconfig:9        select CPU_HAS_ARM_SAU
arch/arm/Kconfig:179-181              CPU_HAS_ARM_SAU -> select CPU_HAS_TEE
arch/arm/core/cortex_m/Kconfig:92     CPU_CORTEX_M85 -> select ARMV8_M_SE if CPU_HAS_TEE
arch/arm/core/cortex_m/tz/Kconfig:6-9 ARM_TRUSTZONE_M depends on CPU_HAS_TEE && ARMV8_M_SE
```

So `CONFIG_ARM_TRUSTZONE_M` is **reachable** on this board. Verified by building
it, not by reading Kconfig:

| Spike build (`samples/hello_world`, `-b ek_ra8d1`, pristine) | Result |
|---|---|
| `-DCONFIG_TRUSTED_EXECUTION_SECURE=y` alone (`build/tz_spike`) | Builds. Symbol **silently dropped** — absent from `.config`, because `arch/Kconfig:324` gates it behind `ARCH_HAS_TRUSTED_EXECUTION`, which `arch/arm/core/Kconfig:23` selects only `if ARM_TRUSTZONE_M`. FLASH 27352 B. |
| `-DCONFIG_ARM_TRUSTZONE_M=y -DCONFIG_TRUSTED_EXECUTION_SECURE=y` (`build/tz_spike2`) | **Builds and links cleanly.** `.config` resolves `ARM_TRUSTZONE_M=y`, `ARM_SECURE_FIRMWARE=y`, `TRUSTED_EXECUTION_SECURE=y`, `ARCH_HAS_TRUSTED_EXECUTION=y`. FLASH 28268 B (+916 B, the `-mcmse` codegen). |

Both were run in this session; output in the build logs. Neither was flashed.

**This "success" is a trap, and it is the central finding of the spike.** The
build succeeds because Zephyr's `arch/arm/core/cortex_m/tz/` contains *only*
build plumbing — `CMakeLists.txt` (adds `-mcmse`, emits the CMSE import library),
`Kconfig`, and `secure_entry_functions.ld`. There is **no `.c` file**. Zephyr's
only other SAU code is *fault reporting* (`arch/arm/core/cortex_m/fault.c:118`,
`:579-594`) and address queries (`arch/arm/core/cortex_m/cmse/arm_core_cmse.c:85`).
Grepping `arch/arm/core/` and `soc/renesas/` finds **no SAU region-programming
code at all**. A `TRUSTED_EXECUTION_SECURE=y` image on this board would compile,
link, and boot with the SAU never configured — i.e. it produces a secure-marked
binary with no actual isolation. It looks like TrustZone support and is not.

---

## 3. What the Zephyr tree actually contains (searched, not recalled)

### 3.1 No Renesas board anywhere has a `/ns` variant
Every `boards/renesas/*/board.yml` was read. None declares a variant; all list a
single SoC. Confirmed for all 11 RA8-family boards (`ek_ra8d1`, `ek_ra8d2`,
`ek_ra8m1`, `ek_ra8m2`, `ek_ra8p1`, `ek_ra8t2`, `aik_ra8d1`, `cpkcor_ra8d1b`,
`fpb_ra8e1`, `mck_ra8t1`, `mck_ra8t2`). Every "TrustZone" hit under
`boards/renesas/` is a **marketing URL in a doc `index.rst`**, including
`boards/renesas/ek_ra8d1/doc/index.rst:190`. `ek_ra8d1.yaml` lists supported
features as `gpio, uart, watchdog, usbd, display, counter, i2s, i3c, video` —
no security feature of any kind.

### 3.2 There IS Renesas RA TrustZone infrastructure — but not for RA8, and not what it sounds like
`soc/renesas/ra/Kconfig:22` defines `CPU_HAS_RENESAS_RA_IDAU`, selected by
exactly two SoCs: `ra6m4/Kconfig:8` and `ra6m5/Kconfig:8` (both Cortex-M33).
`ra8d1` does **not** select it — it selects `CPU_HAS_ARM_SAU` instead.

Critically, even for RA6M4/RA6M5 this is **not** a secure/non-secure split. It is
`CONFIG_OUTPUT_RPD` (`soc/renesas/ra/Kconfig:15-20`), which runs
`soc/renesas/ra/tools/gen_rpd.py` to emit a `zephyr.rpd` partition file from ELF
symbols. Its stated purpose, per `boards/renesas/ek_ra6m4/doc/index.rst:93-95`:

> "In applications using ethernet, ethernet buffers must be placed in non-secure
> RAM. This requires configuration of the Implementation Defined Attribution Unit
> (IDAU), which must be applied by partition memory using Renesas Flash Programmer."

That is a **single-image workaround for a DMA constraint**, applied off-target via
`rfp-cli`. It is not a trusted-execution environment, has no secure image, no NSC
veneers, and no secure services. And `gen_rpd.py` is gated on
`CPU_HAS_RENESAS_RA_IDAU`, so it does not apply to RA8D1 as written.

### 3.3 No TF-M port exists for any Renesas device
`modules/tee/tf-m/trusted-firmware-m/platform/ext/target/` contains: `adi`, `arm`,
`armchina`, `cypress`, `infineon`, `nordic_nrf`, `nuvoton`, `nxp`, `rpi`, `stm`.
**No `renesas` directory.** The standard Zephyr TrustZone route (`BUILD_WITH_TFM`
on a `/ns` target) is therefore closed for this MCU — not merely unconfigured.

### 3.4 Open PRs / issues
GitHub API searches against `zephyrproject-rtos/zephyr`:

- `ra8 + trustzone` → **0 results.**
- `renesas + trustzone` → the only relevant item is **PR #101704,
  "soc: renesas: ra4m2: add TrustZone/IDAU support"** — **closed 2026-04-28
  WITHOUT being merged** (`merged: false`, `merged_at: null`), 3 files / +67 lines.
  Verified against our checkout: `soc/renesas/ra/ra4m2/Kconfig` does **not**
  select `CPU_HAS_RENESAS_RA_IDAU`, consistent with the PR never landing.
- `ek_ra8d1`, open only → one unrelated item (**PR #82730**, USB UHC driver).

**No one upstream is working on ek_ra8d1 TrustZone or secure boot.** Even the
much smaller RA4M2 IDAU patch — 67 lines, hardware-tested by its author — failed
to land. That is the most honest available estimate of upstream appetite.

---

## 4. The real gap: silicon capability vs. Zephyr exposure

Cross-referenced against the supplied appendix. **Nothing in the table below has
a Zephyr driver or devicetree binding.**

| Silicon capability | Datasheet citation | Zephyr status (verified) |
|---|---|---|
| Per-peripheral **secure (`0x4xxx_xxxx`) / non-secure (`0x5xxx_xxxx`) alias regions** for every peripheral | **Table A3.1**, pp. 184–186 | **Only secure aliases exist in DT.** `grep "reg = <0x5"` over `dts/arm/renesas/ra/ra8/` → **zero matches**. Every node uses the secure alias: `ospi0@40268000`, `eth@40354100`, `spi0@4035c000` (:694, :776, :488 of `ra8x1.dtsi`). A non-secure world would have no peripheral nodes to bind to. |
| **CPSCU** — CPU System Security Control Unit | Table A3.1, p. 184 (`0x4000_8000` / NS `0x5000_8000`) | No binding, no driver, no reference anywhere in `dts/`, `drivers/`, `soc/renesas/`. |
| **PSCU** — Peripheral Security Control Unit (the unit that actually assigns S/NS attribution per peripheral) | Table A3.1, p. 184 (`0x4020_4000`); access cycles Table A3.2, p. 187 | Same — **absent entirely**. This is the blocking gap: without it, peripheral security attribution cannot be set from Zephyr. |
| **RMPU** — Renesas Memory Protection Unit (bus-master protection, *distinct from* the Arm MPU Zephyr uses) | Table A3.1, p. 184 (`0x4000_0000`) | Absent. Zephyr's `ARM_MPU` is the Armv8.1-M core MPU only. |
| **RSIP-E51A** security engine | **Table A3.2, p. 188** — listed with 1–3 read / 1–2 write PCLKA cycles | **Partially reachable — see §5.** FSP procedures compiled; only the TRNG is exposed through a Zephyr API. |
| **DOTF0** — Decryption On-The-Fly (transparent decryption of external OSPI flash) | Table A3.1, p. 185 (`0x4026_8800` / NS `0x5026_8800`); access cycles p. 188 | **FSP supports it** — 73 references in `modules/hal/renesas/drivers/ra/fsp/src/r_ospi_b/r_ospi_b.c`. **Zephyr does not expose it**: `dts/bindings/ospi/renesas,ra-ospi-b.yaml` and `.../renesas,ra-ospi-b-nor.yaml` have no DOTF property, and `grep -ri dotf zephyr/drivers/` → nothing. Directly relevant to us: this would encrypt our Phase 2D log store at rest, for free, in hardware. |
| **FDFS** — Data Flash Security Setting | Table A3.1, p. 186 (`0x2703_0000`, **no NS alias** — secure-only by construction) | Absent. |
| **S-TYPE-1…7 / P-TYPE-1…5** register access rules (which registers ignore NS writes silently vs. raise a TrustZone access error) | **Tables A4.1 and A4.2, p. 189** | Zephyr has **no model of per-register security or privilege attribution** for RA. Note the hazard documented on p. 189: S-TYPE-1/-2/-4 and P-TYPE-1/-4 **silently ignore** disallowed writes with *no* TrustZone access error. A misconfigured non-secure driver would fail invisibly — no fault, no log, just a peripheral that never comes up. That is a brutal debugging surface to take on without the board in hand. |
| RSIP tamper-detection event | `ELC_EVENT_RSIP_TADI = 0x1BC`, `hal_renesas/.../bsp/mcu/ra8d1/bsp_elc.h:356` | No Zephyr consumer. |

**Boundary-marking note (p. 189):** the appendix states a secure bus master issues
secure accesses "using an address marked as secure by **IDAU/SAU or MSAU**". Zephyr
has `CPU_HAS_ARM_SAU` for this SoC and IDAU support for RA6M4/M5, but **`MSAU`
appears nowhere in the Zephyr tree** (`grep MSAU soc/renesas/` → no match).

### 4.1 Honest limits of the supplied PDF
The brief described the PDF as covering the "RSIP-E51A register map" and a
"256-bit HUK". **Neither is in the supplied excerpt.** Pages 184–190 are
Appendices 3–5 only. RSIP-E51A appears **once**, in Table A3.2 (p. 188), and its
address range columns are **blank (`—`)** — the register map is deliberately not
published there. It is also **absent from Table A3.1 entirely**, so it has no
documented secure/non-secure base-address pair in this document. Nothing in
pp. 184–190 mentions a HUK, key wrapping, or key sizes. Renesas' public product
page describes a **128-bit unique ID**, not a 256-bit HUK — treat the 256-bit
figure as unverified until sourced from the RA8D1 *User's Manual: Hardware*
security chapter or the RSIP application note. This does not change the verdict:
we can drive the engine through FSP without its register map (§5).

### 4.2 Getting the FSP version straight
Our `hal_renesas` vendors **FSP 6.2.0**. Upstream FSP is at **6.5.1**. FSP ≥ 6.x
ships a newer `r_rsip` / `r_rsip_protected` driver, but **that driver is not in
our tree**: `ls modules/hal/renesas/drivers/ra/fsp/src/r_rsip*` → no such
directory, and `fsp/inc/api/` has no RSIP header. What we have is the **legacy
`r_sce` driver** with RSIP-E51A crypto procedures underneath it (§5).

### 4.3 What a real TrustZone bring-up would require
1. **Secure/non-secure devicetree.** Author non-secure alias nodes (`0x5xxx_xxxx`,
   Table A3.1) for every peripheral the NS image needs. Zero exist today.
2. **A `/ns` board variant** — `board.yml` variant, defconfigs, and a flash/RAM
   partition scheme. No Renesas board in Zephyr has ever had one (§3.1).
3. **SAU/MSAU region programming** in `soc/renesas/ra/ra8d1/`. Does not exist for
   any RA8 SoC. RA6M4/M5's IDAU path (§3.2) is a different mechanism and does not
   transfer.
4. **A CPSCU/PSCU security-attribution driver** — binding, driver, and DT
   description of per-peripheral attribution. Nothing exists.
5. **NSC veneers and secure services.** Zephyr provides the linker/CMake plumbing
   (`arch/arm/core/cortex_m/tz/`); every actual service must be written.
6. **Off-target boundary programming.** The S/NS boundary is programmed into the
   device via Renesas Flash Programmer. `gen_rpd.py` covers IDAU parts only and
   would need extending for RA8D1's SAU/MSAU model. **Requires the physical board
   and `rfp-cli`.**
7. **TF-M is not an option** — no Renesas platform port exists (§3.3).

**Effort:** items 1–5 are upstream-quality SoC enablement — realistically
**multi-month**, by someone with the hardware and the RA8D1 User's Manual
security chapter. A deliberately minimal, non-upstreamable "secure app boots a
non-secure app" demo is still **several weeks with a board on the desk**. Against
a spike time-boxed at ~1 day, with **no board available at all**, and with the
silent-failure hazard of §4 (S-TYPE/P-TYPE), this is not a defensible use of the
remaining schedule.

---

## 5. The genuinely useful finding: RSIP-E51A is already reachable

This answers the Phase 3 SecOC follow-up question directly.

**Is the RA RSIP/SCE security engine exposed in `modules/hal_renesas` for
R7FA8D1BH? Yes — and it is already being compiled into our existing builds.**

Evidence, from our **own Phase 2 build artifacts** (not a new build):

```
build/eth_doip/zephyr/misc/generated/configs.c:198   CONFIG_USE_RA_FSP_SCE = 1
build/eth_doip/zephyr/misc/generated/configs.c:199   CONFIG_HAS_RENESAS_RA_RSIP_E51A = 1
build/eth_doip/zephyr/misc/generated/configs.c:48    CONFIG_DT_HAS_RENESAS_RA_RSIP_E51A_TRNG_ENABLED = 1
```

Supporting chain, all verified in-tree:

- `hal_renesas/drivers/ra/fsp/src/bsp/mcu/ra8d1/bsp_peripheral.h:158` —
  `BSP_PERIPHERAL_RSIP_PRESENT (1)`
- `.../ra8d1/bsp_feature.h:480` — `BSP_FEATURE_RSIP_RSIP_E51A_SUPPORTED (1UL)`
  (every other engine variant is `0UL`)
- `hal_renesas/drivers/ra/CMakeLists.txt:169-177` — under `CONFIG_HAS_RENESAS_RA_RSIP_E51A`,
  globs and compiles `r_sce/crypto_procedures/src/rsip_e51a/plainkey/{adaptors,primitive}/*.c`
  (**235 primitive source files**)
- `dts/arm/renesas/ra/ra8/ra8x1.dtsi:487-489` — `trng` node,
  `compatible = "renesas,ra-rsip-e51a-trng"`
- `boards/renesas/ek_ra8d1/ek_ra8d1.dts:28` — `zephyr,entropy = &trng;`;
  `:294` — `&trng { status = "okay"; }`
- `drivers/entropy/entropy_renesas_ra.c` — binds it, calls `HW_SCE_RNG_Read()`

### 5.1 What we can use today, with no new Zephyr driver

**Already working through a standard Zephyr API — hardware TRNG.** `ek_ra8d1` is
the system entropy source via `zephyr,entropy`. Nothing to do.

**Compiled in, but with no Zephyr API on top — the wrapped-key crypto engine.**
`r_sce/crypto_procedures/src/rsip_e51a/plainkey/public/inc/r_sce_if.h` (1722
lines) exports, among much else:

- `HW_SCE_Aes128CmacGenerateInit/Update/Final` and
  `HW_SCE_Aes128CmacVerifyInit/Update/Final` (`:1475-1480`), plus AES-256
  variants (`:1481-1486`) — **taking an `sce_aes_key_index_t`, not a raw key**
- AES-128/192/256 GCM (`:1280-1330`)
- `HW_SCE_GenerateAes128RandomKeyIndex()` (`:985`) — generate a key that
  **never exists in plaintext outside the engine**
- `HW_SCE_GenerateAes128PlainKeyIndex()` (`:1007`) — wrap a provisioned key
- ECC P-192/224/256/384 and RSA 1024–4096 key-index generation (`:907-998`)

**Why this matters for SecOC.** AUTOSAR/ISO SecOC authenticates CAN frames with a
**symmetric AES-128 CMAC**. That primitive is present, hardware-accelerated, and
**operates on a wrapped key index rather than a plaintext key**. This is precisely
the "hardware key store" that STATE.md's Phase 3 note said "would need TrustZone."

**That conclusion was too pessimistic and should be revised.** The key-index
mechanism gives key confidentiality via the engine itself, independent of the
Armv8-M security state — an unwrapped key is never resident in CPU-addressable
RAM. It is genuinely weaker than TrustZone in one specific respect: any code on
the device can *use* the key index to compute MACs. But it removes the
"secret sits in plain flash/RAM" problem that motivated wanting TrustZone in the
first place, and it costs days rather than months.

**The gap is only the Zephyr-side glue.** `grep -rl "HW_SCE_" zephyr/drivers/
zephyr/subsys/` returns **exactly one file**: `entropy_renesas_ra.c`. There is no
`drivers/crypto/` driver for RA (`ls drivers/crypto/ | grep -i "renesas\|ra_"` →
nothing) and no PSA/mbedTLS accelerator binding. Application code would call
`HW_SCE_*` directly — headers and objects are already on the include path and in
the link, so **no new Kconfig, no HAL patch, no devicetree change is required**.

---

## 6. Recommendation

1. **TrustZone: NO-GO.** Document as future work. For PROMPTS.md Phase 6, the
   rubric line is *"documented as future work, see docs/trustzone_feasibility.md"* —
   and this report is a substantive negative result with reproducible evidence
   (two spike builds, an unmerged upstream PR, an empty TF-M target directory),
   not a shrug.
2. **Fix the CLAUDE.md hard-constraint bullet** — strike "no SAU binding"
   (§2). *Not yet applied; CLAUDE.md says it should rarely change, so this needs
   your explicit go-ahead.*
3. **Proposed replacement spike — SecOC over RSIP-E51A (days, not months).** A
   build-only module that generates an AES-128 key index and computes a CMAC over
   a CAN frame payload, wired to the Phase 2A frame struct. Delivers a real
   security story for the report, is hardware-honest about what it can't validate
   yet, and reuses work already in the tree. **PROMPTS.md Phase 4 says no such
   code without checking first — this needs your go-ahead.**
4. **Log DOTF as a cheap future win** (§4). Decryption-on-the-fly would encrypt
   the Phase 2D OSPI log store at rest in hardware. FSP already implements it;
   only the Zephyr binding is missing. Smaller than TrustZone by orders of
   magnitude.
5. **Open question to close later:** the 256-bit HUK claim is unverified (§4.1).
   Resolve against the RA8D1 *User's Manual: Hardware* security chapter — the
   supplied datasheet appendix does not cover it.

---

## 7. Reproducing this

```powershell
# Spike 1 — TRUSTED_EXECUTION_SECURE alone (silently dropped)
.\.venv\Scripts\west build -b ek_ra8d1 zephyr\samples\hello_world `
    -d build\tz_spike -p always -- -DCONFIG_TRUSTED_EXECUTION_SECURE=y
Select-String "TRUSTED_EXECUTION|ARM_TRUSTZONE_M" build\tz_spike\zephyr\.config

# Spike 2 — with ARM_TRUSTZONE_M (builds cleanly, SAU never programmed)
.\.venv\Scripts\west build -b ek_ra8d1 zephyr\samples\hello_world `
    -d build\tz_spike2 -p always `
    -- -DCONFIG_ARM_TRUSTZONE_M=y -DCONFIG_TRUSTED_EXECUTION_SECURE=y
Select-String "TRUSTED_EXECUTION|ARM_SECURE_FIRMWARE|ARM_TRUSTZONE_M" build\tz_spike2\zephyr\.config
```

Neither image was flashed — no board is attached.

## 8. References

**Datasheet** — RA8D1 Group, R01DS0416EJ0130 Rev.1.30 (Dec 19 2025), supplied as
`security_and_trustzone_research.pdf`:
Table A3.1 Peripheral base address, pp. 184–186 ·
Table A3.2 Access cycles, pp. 187–188 (RSIP-E51A row, p. 188, address columns blank) ·
Appendix 4 / Table A4.1 (S-TYPE) and Table A4.2 (P-TYPE), p. 189 ·
Table A5.1 Peripheral variant, p. 190 (SCI→SCI_B, SPI→SPI_B, OSPI→OSPI_B, DOC→DOC_B).

**Upstream** — [Zephyr PR #101704, ra4m2 TrustZone/IDAU (closed unmerged)](https://github.com/zephyrproject-rtos/zephyr/pull/101704) ·
[Zephyr ek_ra8d1 board docs](https://docs.zephyrproject.org/latest/boards/renesas/ek_ra8d1/doc/index.html)

**Renesas (outside Zephyr)** — TrustZone *is* fully supported via FSP + e2 studio,
which is why porting is theoretically possible even though Zephyr cannot drive it:
[FSP documentation, "Primer: Arm TrustZone Project Development", v6.5.1](https://renesas.github.io/fsp/_s_t_a_r_t__d_e_v.html) ·
[r_rsip_protected driver (FSP ≥6.x; NOT in our vendored FSP 6.2.0)](https://renesas.github.io/fsp/group___r_s_i_p___p_r_o_t_e_c_t_e_d.html) ·
[TrustZone on RA8 Series MCUs (Renesas KB)](https://en-support.renesas.com/knowledgeBase/21606622) ·
[RA Flexible Software Package](https://www.renesas.com/en/software-tool/ra-flexible-software-package-fsp) ·
[Renesas RA Security Design with Arm TrustZone — IP Protection, R11AN0467EU0170](https://www.renesas.com/us/en/document/apn/renesas-ra-security-design-arm-trustzone-ip-protection) ·
[RA8 Secure Factory Programming](https://www.renesas.com/en/document/apn/ra8-secure-factory-programming) ·
[RA8D1 product page](https://www.renesas.com/en/products/microcontrollers-microprocessors/ra-cortex-m-mcus/ra8d1-480-mhz-arm-cortex-m85-based-graphics-microcontroller-helium-and-trustzone)
