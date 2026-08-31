# EK-RA8D1 USB-HS CDC-ACM bulk transfers never reach the host — RESOLVED

**Status: FIXED UPSTREAM.** hal_renesas commit **`f2c2aa6359e`**
("hal: renesas: ra: fix issue r_usb_device cannot send ZLP", 2026-08-04) is the
immediate child of the west-manifest-pinned `f2eb9bc` and fixes exactly this.
We hit it because our tree was pinned one commit too early.

- This project independently found, root-caused and (locally) fixed the same
  bug on 2026-08-31 before discovering the upstream commit — an unwitting but
  clean confirmation of the diagnosis.
- `modules/hal/renesas` is now checked out at `f2c2aa6359e`; the local patch is
  removed. Re-verified on hardware after the bump (host reads the CDC data).
- A PR we opened (`zephyrproject-rtos/hal_renesas#220`) was closed as a
  duplicate of `f2c2aa6359e`.

The upstream fix sets `BVAL | BCLR` on the pipe FIFO unconditionally (plus a
CURPIPE settle wait); functionally identical to what this write-up proposed
(commit the empty buffer so the ZLP is actually transmitted).

Original investigation below, kept for the record / the report.

---

Everything below was observed on real hardware in this project's Phase 5 bring-up.
Nothing here is speculative; where a cause is unproven it is labelled as such.

---

## Summary

On `ek_ra8d1`, a USB CDC-ACM device using the **new USB device stack**
(`CONFIG_USB_DEVICE_STACK_NEXT`) on the **High-Speed** controller enumerates
correctly and services **control** transfers, but **bulk data transfers never
reach the host**. Bytes written with `uart_fifo_fill()` accumulate in the class
driver's TX ring buffer and are never forwarded to the bulk IN endpoint. The
host reads zero bytes, indefinitely.

**This reproduces with Zephyr's own unmodified `samples/subsys/usb/cdc_acm`
sample**, so it is not application code.

---

## Environment

| item | value |
|---|---|
| Board | `ek_ra8d1` (Renesas EK-RA8D1 v1.0, P/N RTK7EKA8D1S00001BE) |
| MCU | R7FA8D1BHECBD, Cortex-M85 |
| Zephyr | mainline `v4.4.99`, commit **`f80761e4940`** (2026-08-28) |
| Also present at | commit `66e5135ffc3` (150 commits later — see "Still present upstream") |
| hal_renesas | `f2eb9bc7352f4dadae08e9f5f16b05bb26779b87` (FSP 6.2.0) |
| Zephyr SDK | 1.0.1 (arm-zephyr-eabi-gcc 14.3.0) |
| USB stack | `CONFIG_USB_DEVICE_STACK_NEXT=y` |
| UDC driver | `CONFIG_UDC_RENESAS_RA` — `drivers/usb/udc/udc_renesas_ra.c` |
| Class | `subsys/usb/device_next/class/usbd_cdc_acm.c` |
| Controller | `usbhs` (`renesas,ra-usbhs`), `zephyr_udc0` = its `renesas,ra-udc` child |
| Host | Windows 11, inbox `usbser.sys`, port enumerates as `USB Serial Device (COMn)` |

Note: `usbfs` (Full-Speed) is **not** enabled in the stock `ek_ra8d1` DTS, so
USB-HS is the only USB device path the board offers out of the box. The
Full-Speed path has **not** been tested and may be unaffected.

---

## Reproduce (stock sample, no modifications)

```sh
west build -b ek_ra8d1 zephyr/samples/subsys/usb/cdc_acm -p always
west flash
```

Then, on the host, open the enumerated CDC port, assert DTR, and write some
bytes. `cdc_acm_echo` is supposed to echo them back.

```python
import serial, time
s = serial.Serial("COM13", 115200, timeout=0.3)
s.dtr = True
time.sleep(1.5)
s.reset_input_buffer()
s.write(b"HELLO-ECHO-TEST\r\n")
time.sleep(5)
print(s.read(256))     # -> b''  (expected: the echoed bytes)
```

**Observed:** `b''`. Nothing is ever echoed.

**Expected:** the 17 written bytes echoed back.

---

## Evidence

### 1. Enumeration and control transfers work

The device enumerates at High Speed, and the host's control requests reach the
device — the stock sample logs them:

```
<inf> cdc_acm_echo: USBD message: CDC ACM line coding
<inf> cdc_acm_echo: Baudrate 115200
<inf> cdc_acm_echo: USBD message: CDC ACM control line state
```

Descriptors and configuration are accepted:

```
<inf> usbd_init:    bNumInterfaces 2 wTotalLength 75
<inf> usbd_core:    Actual device speed 2          # 2 = High Speed
<inf> usbd_cdc_acm: Configuration enabled
```

`Configuration enabled` matters: it is logged from the class `enable` callback,
which sets `CDC_ACM_CLASS_ENABLED`. So `cdc_acm_tx_fifo_handler()`'s
"USB configuration is not enabled" early-return is **not** the explanation.

### 2. The class driver accepts the data, then never sends it

From an application built on the same stack (this project's `usb_cdc`), writing
33 bytes via `uart_fifo_fill()` twice:

```
<inf> usb_link:     host connected (DTR) - sending HELLO
<inf> usbd_cdc_acm: tx_en: trigger irq_cb_work
<inf> usbd_cdc_acm: UART dev 0x200ed08, len 33, remaining space 991
...
<inf> usb_link:     host connected (DTR) - sending HELLO
<inf> usbd_cdc_acm: tx_en: trigger irq_cb_work
<inf> usbd_cdc_acm: UART dev 0x200ed08, len 33, remaining space 958
```

The **driver's own** TX ring-buffer free space goes `1024 -> 991 -> 958`. The
bytes are inside `data->tx_fifo.rb` and are never drained to the bulk IN
endpoint. The host reads 0 bytes across repeated 5-6 second windows.

The application side is behaving correctly: `uart_irq_tx_enable()` is called,
the driver schedules `irq_cb_work`, the registered callback runs,
`uart_fifo_fill()` returns the full length, and `cdc_acm_fifo_fill()` sets
`data->tx_fifo.altered = true` (usbd_cdc_acm.c).

### 3. It is not the application

The stock `samples/subsys/usb/cdc_acm` sample, unmodified, fails identically.
That is the decisive test: no project code is involved.

---

## Still present upstream

Checked between `f80761e4940` and `66e5135ffc3` (150 commits of mainline):

```
drivers/usb/udc/udc_renesas_ra.c                 0 commits
subsys/usb/device_next/class/usbd_cdc_acm.c      0 commits
subsys/usb/ + drivers/usb/  (all)                0 commits
soc/renesas, boards/renesas/ek_ra8d1,
  dts/arm/renesas                                0 commits
```

`git diff --name-only f80761e4940..66e5135ffc3` matches **zero** files
containing `renesas` or `ra8`. The relevant code is byte-identical, so the bug
is present in current mainline, not only in the pinned snapshot.

---

## ROOT CAUSE FOUND (2026-08-31) — it is in hal_renesas, not Zephyr's USB subsys

The earlier "still present upstream" check only looked at `drivers/usb/` and
`subsys/usb/` in the Zephyr tree. **The bug is not there.** It is in the
vendored Renesas FSP USB device driver:
`modules/hal/renesas/drivers/ra/fsp/src/r_usb_device/r_usb_device.c`,
`process_pipe_xfer()`.

Chain:

1. `usbd_cdc_acm_enable()` sets `zlp_needed = true` and schedules the TX work.
2. `cdc_acm_tx_fifo_handler()` runs with `echo_mitigated == false`, so it
   allocates a buffer, adds **0** bytes, sets `CDC_ACM_TX_FIFO_BUSY`, and
   enqueues it. The very first bulk IN transfer of the device's life is a ZLP.
3. `R_USBD_XferStart(ep, buf, 0)` → `process_pipe_xfer(..., total_bytes = 0)` →
   the IN / ZLP branch:

   ```c
   *d0fifosel = num;
   if ((*d0fifoctr & R_USB_CFIFOCTR_BVAL_Msk) != 0) {
       *d0fifoctr = R_USB_CFIFOCTR_BVAL_Msk;
   }
   *d0fifosel = 0;
   ```

   For a freshly selected empty buffer `BVAL` is 0, so the body is skipped and
   **BVAL is never written**. Without BVAL the controller never commits the
   empty buffer, no packet goes out, and no `BRDY` interrupt is raised.
4. `process_pipe_brdy()` → `USBD_EVENT_XFER_COMPLETE` never fires →
   `udc_event_xfer_complete()` never runs → `usbd_cdc_acm_request()` never
   clears `CDC_ACM_TX_FIFO_BUSY`.
5. `CDC_ACM_TX_FIFO_BUSY` stuck forever → every later `cdc_acm_tx_fifo_handler()`
   bails at `atomic_test_and_set_bit(... CDC_ACM_TX_FIFO_BUSY)`. All real data
   is stranded in the ring buffer — exactly the observed `remaining space
   991 → 958` behaviour, and exactly candidate 1 below.

The non-ZLP path in the same file (`pipe_xfer_in()`) writes
`*d0fifoctr = R_USB_D0FIFOCTR_BVAL_Msk` **unconditionally** for a short final
packet. The ZLP branch just needs to do the same.

### Local fix applied (2026-08-31)

`r_usb_device.c`, `process_pipe_xfer()` ZLP branch — select the FIFO, wait for
it to be ready, then set BVAL unconditionally:

```c
*d0fifosel = num | R_USB_FIFOSEL_MBW_16BIT |
             (BYTE_ORDER == BIG_ENDIAN ? R_USB_FIFOSEL_BIGEND : 0);
pipe_wait_for_ready(p_ctrl, num);
*d0fifoctr = R_USB_D0FIFOCTR_BVAL_Msk;
*d0fifosel = 0;
FSP_HARDWARE_REGISTER_WAIT((*d0fifosel & R_USB_D0FIFOSEL_CURPIPE_Msk), 0);
```

Kept as `patches/0002-usb-device-ra-send-zlp-on-bulk-in.patch`; re-apply after
any `west update` (`patches/README.md`).

### Why bumping Zephyr never helped

`modules/hal/renesas` is pinned to the **same** revision (`f2eb9bc`) by both the
old (`f80761e`) and new (`66e5135`) Zephyr manifests, so the accidental mainline
pull on 2026-08-31 could not have changed this file. STATE.md §3b's conclusion
("updating Zephyr cannot fix the USB bug") was right; the reasoning was
incomplete — the fix was never going to be in the Zephyr tree at all.

### Hardware test status — FIX CONFIRMED ON HARDWARE (2026-08-31)

Tree: Zephyr `66e5135ffc3` + hal_renesas `f2eb9bc` + patch 0002.

- [x] **`apps/usb_cdc`, bulk IN**: on DTR the host reads a **33-byte** CLP HELLO
  frame over USB-HS (`05 01 05 02 ... "usb_cdc phase-2c v0.1" ... 0b 92 00`).
  Device console: `usb_link: host connected (DTR) - sending HELLO`.
  **Pre-fix this was 0 bytes, indefinitely.**
- [x] **bulk OUT** (was never the broken direction, checked anyway): host writes
  a CLP `CAN_TX` frame → device console logs
  `main: host asked to TX: id=0x00000123 dlc=8 flags=0x00 tag=48879`.
- [ ] stock `samples/subsys/usb/cdc_acm` echo — **not re-run this session** (ran
  out of bench time). The fix is class-agnostic (any CDC-ACM instance enqueues a
  ZLP on enable via `usbd_cdc_acm_enable()`), so `usb_cdc` passing is strong
  evidence, but re-running the stock sample echo is the cleanest thing to attach
  to the upstream issue — do it before filing.

---

## Where the fault is likely to be (ORIGINAL NOTE — superseded by "ROOT CAUSE FOUND" above)

The break is between `cdc_acm_tx_fifo_handler()` and the UDC driver actually
completing a bulk IN transfer. Two candidates, neither confirmed:

1. **`CDC_ACM_TX_FIFO_BUSY` is set and never cleared** — i.e. the first IN
   transfer is enqueued but its completion never comes back from
   `udc_renesas_ra.c`, so every later `tx_fifo_handler()` returns at
   `atomic_test_and_set_bit(&data->state, CDC_ACM_TX_FIFO_BUSY)`.
2. **`tx_fifo_work` is never scheduled** after the user callback returns.

**Why this was not narrowed further:** an attempt to distinguish them using the
class driver's `LOG_DBG` lines failed — with
`CONFIG_USBD_CDC_ACM_LOG_LEVEL_DBG=y` and `CONFIG_USBD_CDC_ACM_LOG_LEVEL=4`
confirmed in `.config`, **no DBG output was produced at all**, not even from
code paths known to execute. So "no DBG line appeared" could not be used as
evidence either way. That logging anomaly may itself be worth a separate look.

Narrowing this properly needs either a working DBG path or a debugger
inspecting `struct cdc_acm_uart_data.state` on the live target.

---

## Impact on this project

CAN-FD logging over USB CDC-ACM was the intended primary GUI transport
(`docs/clp_protocol.md`). It is blocked by this bug. The CLP protocol layer
itself is unaffected and passes its self-test on hardware every boot, so the
project uses the **J-Link VCOM/UART** as the GUI transport instead; CLP is
transport-agnostic (byte stream in, byte stream out).

---

## If picking this up later

1. Search existing issues for RA / `udc_renesas_ra` / CDC-ACM bulk transfer
   before filing — this may already be known.
2. Re-run the "still present upstream" check against whatever mainline is
   current:
   `git log --oneline <our-sha>..HEAD -- drivers/usb/ subsys/usb/`
3. Worth testing the **Full-Speed** (`usbfs`) path, which is a different UDC
   route and would sharpen the report considerably (it needs enabling in an
   overlay — the stock board DTS leaves `usbfs` disabled).
4. The repro above is small and self-contained; it is the strongest part of the
   report. Lead with it.
