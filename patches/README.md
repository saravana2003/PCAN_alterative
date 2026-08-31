# Local patches to west-managed trees

`zephyr/` and `modules/` are west-managed and git-ignored by this repo, so any
fix we make there is not tracked by our normal commits. Each patch we carry
lives here and must be re-applied after a `west update` (or a fresh
`west init` + `west update`).

## Current tree

- Zephyr: `66e5135ffc3` (mainline, detached HEAD — see STATE.md)
- hal_renesas: **`f2c2aa6359e`** (detached HEAD — bumped +1 from the manifest's
  `f2eb9bc` to pick up the upstream USB ZLP fix, see below)

## Patches

| file | tree | what it fixes |
|---|---|---|
| `0001-flash-renesas-ra-ospi-b-reset-correct-cs.patch` | `zephyr/` | OSPI-B driver only ever drives `RSTCS0`; a flash on CS1 (EK-RA8D1) is never reset. Real defect, submitted upstream (see below). **Does NOT fix our board's `flash_log` failure** — that's a hardware issue (silent flash). `docs/zephyr_ospi_cs_reset_bug.md` |

### Apply / check

```sh
git -C zephyr apply "$PWD/patches/0001-flash-renesas-ra-ospi-b-reset-correct-cs.patch"
git -C zephyr apply --reverse --check "$PWD/patches/0001-*.patch" && echo "already applied"
```

## USB-HS CDC-ACM ZLP fix — RESOLVED, no local patch

The bulk-IN zero-length-packet bug (`docs/zephyr_usb_hs_bug.md`) is **fixed
upstream** by hal_renesas commit `f2c2aa6359e`
("hal: renesas: ra: fix issue r_usb_device cannot send ZLP", 2026-08-04) — the
immediate child of the manifest-pinned `f2eb9bc`. We independently found and
diagnosed the same bug; the local patch is gone and `modules/hal/renesas` is
checked out at `f2c2aa6359e` instead. Verified on hardware after the bump.

Restore after a fresh `west update` with:
```sh
git -C modules/hal/renesas fetch --depth 10 upstream main
git -C modules/hal/renesas checkout f2c2aa6359e
```
Drop this step entirely once the Zephyr west manifest bumps hal_renesas past
`f2c2aa6359e`.

## Upstream submission — `patches/upstream/`

| file | PR |
|---|---|
| `upstream/zephyr--ospi-b-reset-correct-cs.patch` | **[zephyrproject-rtos/zephyr#117908](https://github.com/zephyrproject-rtos/zephyr/pull/117908)** — OPEN |

The USB fix PR ([hal_renesas#220](https://github.com/zephyrproject-rtos/hal_renesas/pull/220))
was **closed as a duplicate** of `f2c2aa6359e` once we found the upstream fix.

The OSPI PR is flagged in its description as **code-review only** — functionally
unverified because our board's OSPI NOR is non-responsive (hardware). Full
write-up: `docs/zephyr_ospi_cs_reset_bug.md`.
