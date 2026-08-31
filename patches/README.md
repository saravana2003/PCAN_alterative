# Local patches to west-managed trees

`zephyr/` and `modules/` are west-managed and git-ignored by this repo, so any
fix we make there is not tracked by our normal commits. Each patch we carry
lives here and must be re-applied after a `west update` (or a fresh
`west init` + `west update`).

## Current tree

- Zephyr: `66e5135ffc3` (mainline, checked out 2026-08-31 — see STATE.md)
- hal_renesas: `f2eb9bc7352` (FSP 6.2.0)

## Patches

| file | tree | what it fixes |
|---|---|---|
| `0001-flash-renesas-ra-ospi-b-reset-correct-cs.patch` | `zephyr/` | OSPI-B driver only ever drives `RSTCS0`; a flash on CS1 (EK-RA8D1) is never reset. Real defect, worth upstreaming. **Does NOT fix our board's `flash_log` failure** — that's a hardware issue (silent flash). `docs/zephyr_ospi_cs_reset_bug.md` |
| `0002-usb-device-ra-send-zlp-on-bulk-in.patch` | `modules/hal/renesas/` | RA USB device never commits a bulk-IN zero-length packet → CDC-ACM TX wedges forever. **Hardware-verified fix.** `docs/zephyr_usb_hs_bug.md` |

## Apply

```sh
git -C zephyr apply ../patches/0001-flash-renesas-ra-ospi-b-reset-correct-cs.patch
git -C modules/hal/renesas apply ../patches/0002-usb-device-ra-send-zlp-on-bulk-in.patch
```

Check whether they are already applied:

```sh
git -C zephyr apply --reverse --check ../patches/0001-*.patch 2>/dev/null && echo "0001 applied"
git -C modules/hal/renesas apply --reverse --check ../patches/0002-*.patch 2>/dev/null && echo "0002 applied"
```

Once merged upstream and pulled in, drop the corresponding patch.

## Upstream submission — `patches/upstream/`

`git am`-ready commits (proper message + `Signed-off-by`), one per fix:

| file | send to |
|---|---|
| `upstream/hal_renesas--usb-device-commit-bulk-in-zlp.patch` | `zephyrproject-rtos/hal_renesas` |
| `upstream/zephyr--ospi-b-reset-correct-cs.patch` | `zephyrproject-rtos/zephyr` |

**Before submitting**, fix the author/sign-off name — it was guessed from the
git email as `Saravanakumar A`. Zephyr's DCO check wants your real name:

```sh
# edit the From: and Signed-off-by: lines in each .patch, OR after `git am`:
git commit --amend --reset-author -s
```

To turn one into a PR (needs a GitHub fork + push access):

```sh
git clone git@github.com:<you>/hal_renesas   # your fork
cd hal_renesas && git checkout -b fix/ra-usb-bulk-in-zlp
git am /path/to/patches/upstream/hal_renesas--usb-device-commit-bulk-in-zlp.patch
git push -u origin fix/ra-usb-bulk-in-zlp
# then open the PR on github.com; base = zephyrproject-rtos/hal_renesas:main
```

Same for the Zephyr patch against a fork of `zephyrproject-rtos/zephyr`.
The issue write-ups in `docs/zephyr_*_bug.md` are the text to paste into the
PR description / a companion issue.

**Honesty note for the OSPI PR:** functionally unverified — the OSPI NOR on our
board sample is non-responsive (hardware), so we could only confirm the fix by
code review. Say so in the PR.
