#!/usr/bin/env python3
"""
clp_gui.py - desktop GUI for the EK-RA8D1 can_logger firmware.

Speaks CLP over the J-Link VCOM (COM12). Shows a PCAN-View-style aggregated bus
table (one row per CAN ID, with count + cycle time), a STATUS/link panel, an
event log, and a TX form for injecting CAN_TX frames.

Pure stdlib + pyserial (already in the project .venv):
    python host/clp_gui.py            # defaults to COM12 @ 115200
    python host/clp_gui.py COM7 921600

Codec: clp.py. Protocol: docs/clp_protocol.md.
"""
import queue
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial")

import clp

TICK_HZ = 10_000        # device k_uptime_ticks() rate on this build (see STATE.md)


# ---------------------------------------------------------------------------
# serial reader thread
# ---------------------------------------------------------------------------
class Link:
    def __init__(self, evq: queue.Queue):
        self.evq = evq
        self._ser: serial.Serial | None = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self.dec = clp.Decoder()
        self.tx_seq = 0

    def open(self, port: str, baud: int):
        self.close()
        self._ser = serial.Serial(port, baud, timeout=0.15)
        self.dec = clp.Decoder()
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self.evq.put(("info", f"connected {port} @ {baud}"))

    def close(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)
            self._thread = None
        with self._lock:
            if self._ser:
                try:
                    self._ser.close()
                except Exception:
                    pass
                self._ser = None

    @property
    def connected(self) -> bool:
        return self._ser is not None and self._thread is not None

    def send_tx(self, can_id: int, data: bytes, **kw):
        frame = clp.encode_can_tx(self.tx_seq, can_id, data, **kw)
        self.tx_seq = (self.tx_seq + 1) & 0xFF
        with self._lock:
            if not self._ser:
                raise RuntimeError("not connected")
            self._ser.write(frame)
            self._ser.flush()

    def _run(self):
        try:
            while not self._stop.is_set():
                with self._lock:
                    ser = self._ser
                if ser is None:
                    break
                try:
                    chunk = ser.read(512)
                except Exception as e:
                    self.evq.put(("error", f"serial read: {e}"))
                    break
                if chunk:
                    for m in self.dec.feed(chunk):
                        self.evq.put(("msg", m))
                self.evq.put(("stats", (self.dec.frames_ok, self.dec.crc_errors,
                                        self.dec.framing_errors)))
        finally:
            self.evq.put(("disc", None))


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
class App(ttk.Frame):
    BUS_COLS = ("id", "flags", "dlc", "data", "count", "cycle", "dir")

    def __init__(self, master, port: str, baud: int):
        super().__init__(master, padding=6)
        self.grid(sticky="nsew")
        master.columnconfigure(0, weight=1)
        master.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)

        self.evq: queue.Queue = queue.Queue()
        self.link = Link(self.evq)
        self.rows: dict[str, dict] = {}          # key -> {last_ts, ...}

        self._build_connbar(port, baud)
        self._build_bus_table()
        self._build_side()
        self._build_log()

        master.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(50, self._pump)

    # -- widgets ----------------------------------------------------------
    def _build_connbar(self, port, baud):
        bar = ttk.Frame(self)
        bar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        ttk.Label(bar, text="Port").pack(side="left")
        self.port_var = tk.StringVar(value=port)
        ttk.Entry(bar, textvariable=self.port_var, width=10).pack(side="left", padx=(2, 8))
        ttk.Label(bar, text="Baud").pack(side="left")
        self.baud_var = tk.StringVar(value=str(baud))
        ttk.Entry(bar, textvariable=self.baud_var, width=8).pack(side="left", padx=(2, 8))
        self.conn_btn = ttk.Button(bar, text="Connect", command=self._toggle_conn)
        self.conn_btn.pack(side="left")
        ttk.Button(bar, text="Clear", command=self._clear_bus).pack(side="left", padx=6)
        self.conn_lbl = ttk.Label(bar, text="disconnected", foreground="#a33")
        self.conn_lbl.pack(side="left", padx=10)

    def _build_bus_table(self):
        wrap = ttk.LabelFrame(self, text="Bus", padding=4)
        wrap.grid(row=1, column=0, sticky="nsew", padx=(0, 6))
        wrap.rowconfigure(0, weight=1)
        wrap.columnconfigure(0, weight=1)
        tv = ttk.Treeview(wrap, columns=self.BUS_COLS, show="headings", height=16)
        heads = {"id": "ID", "flags": "Flags", "dlc": "DLC", "data": "Data",
                 "count": "Count", "cycle": "Cycle ms", "dir": "Dir"}
        widths = {"id": 90, "flags": 90, "dlc": 44, "data": 210, "count": 64,
                  "cycle": 70, "dir": 44}
        for c in self.BUS_COLS:
            tv.heading(c, text=heads[c])
            tv.column(c, width=widths[c], anchor="w" if c in ("data", "flags") else "center")
        vs = ttk.Scrollbar(wrap, orient="vertical", command=tv.yview)
        tv.configure(yscrollcommand=vs.set)
        tv.grid(row=0, column=0, sticky="nsew")
        vs.grid(row=0, column=1, sticky="ns")
        tv.tag_configure("rx", foreground="#123")
        tv.tag_configure("tx", foreground="#1a5")
        self.bus = tv

    def _build_side(self):
        side = ttk.Frame(self)
        side.grid(row=1, column=1, sticky="ns")

        st = ttk.LabelFrame(side, text="STATUS", padding=6)
        st.pack(fill="x")
        self.stat_vars = {}
        for k in ("bus", "rx_frames", "tx_frames", "rx_drops", "tx_err", "rx_err"):
            row = ttk.Frame(st)
            row.pack(fill="x")
            ttk.Label(row, text=k, width=11).pack(side="left")
            v = tk.StringVar(value="-")
            ttk.Label(row, textvariable=v, font=("Consolas", 9)).pack(side="left")
            self.stat_vars[k] = v

        lk = ttk.LabelFrame(side, text="Link", padding=6)
        lk.pack(fill="x", pady=(6, 0))
        self.link_vars = {}
        for k in ("fw", "frames_ok", "crc_err", "framing_err"):
            row = ttk.Frame(lk)
            row.pack(fill="x")
            ttk.Label(row, text=k, width=11).pack(side="left")
            v = tk.StringVar(value="-")
            ttk.Label(row, textvariable=v, font=("Consolas", 9)).pack(side="left")
            self.link_vars[k] = v

        tx = ttk.LabelFrame(side, text="Transmit (CAN_TX)", padding=6)
        tx.pack(fill="x", pady=(6, 0))
        r1 = ttk.Frame(tx); r1.pack(fill="x")
        ttk.Label(r1, text="ID", width=5).pack(side="left")
        self.tx_id = tk.StringVar(value="0x123")
        ttk.Entry(r1, textvariable=self.tx_id, width=12).pack(side="left")
        r2 = ttk.Frame(tx); r2.pack(fill="x", pady=2)
        self.tx_ext = tk.BooleanVar(); self.tx_fd = tk.BooleanVar()
        self.tx_brs = tk.BooleanVar(); self.tx_rtr = tk.BooleanVar()
        ttk.Checkbutton(r2, text="ext", variable=self.tx_ext).pack(side="left")
        ttk.Checkbutton(r2, text="FD", variable=self.tx_fd).pack(side="left")
        ttk.Checkbutton(r2, text="BRS", variable=self.tx_brs).pack(side="left")
        ttk.Checkbutton(r2, text="RTR", variable=self.tx_rtr).pack(side="left")
        r3 = ttk.Frame(tx); r3.pack(fill="x")
        ttk.Label(r3, text="Data", width=5).pack(side="left")
        self.tx_data = tk.StringVar(value="0011223344556677")
        ttk.Entry(r3, textvariable=self.tx_data, width=20).pack(side="left")
        ttk.Button(tx, text="Send", command=self._send).pack(fill="x", pady=(4, 0))

    def _build_log(self):
        wrap = ttk.LabelFrame(self, text="Events", padding=4)
        wrap.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(6, 0))
        self.log = tk.Text(wrap, height=7, font=("Consolas", 9), wrap="none")
        self.log.pack(fill="both", expand=True)
        self.log.configure(state="disabled")

    # -- actions --------------------------------------------------------
    def _toggle_conn(self):
        if self.link.connected:
            self.link.close()
            return
        try:
            self.link.open(self.port_var.get().strip(), int(self.baud_var.get()))
        except Exception as e:
            self._logline(f"connect failed: {e}")
            return
        self.conn_btn.configure(text="Disconnect")
        self.conn_lbl.configure(text="connected", foreground="#193")

    def _send(self):
        try:
            can_id = int(self.tx_id.get(), 0)
            data = bytes.fromhex(self.tx_data.get().replace(" ", ""))
        except ValueError as e:
            self._logline(f"bad TX input: {e}")
            return
        try:
            self.link.send_tx(can_id, data, ext=self.tx_ext.get(), fd=self.tx_fd.get(),
                              brs=self.tx_brs.get(), rtr=self.tx_rtr.get())
            self._logline(f"TX  id=0x{can_id:X} data={data.hex()}")
        except Exception as e:
            self._logline(f"send failed: {e}")

    def _clear_bus(self):
        self.bus.delete(*self.bus.get_children())
        self.rows.clear()

    def _on_close(self):
        self.link.close()
        self.master.destroy()

    # -- event pump ----------------------------------------------------
    def _pump(self):
        try:
            while True:
                kind, payload = self.evq.get_nowait()
                if kind == "msg":
                    self._on_msg(payload)
                elif kind == "stats":
                    ok, crc, frm = payload
                    self.link_vars["frames_ok"].set(str(ok))
                    self.link_vars["crc_err"].set(str(crc))
                    self.link_vars["framing_err"].set(str(frm))
                elif kind in ("info", "error"):
                    self._logline(payload)
                elif kind == "disc":
                    self.conn_btn.configure(text="Connect")
                    self.conn_lbl.configure(text="disconnected", foreground="#a33")
        except queue.Empty:
            pass
        self.after(50, self._pump)

    def _on_msg(self, m: clp.Message):
        o = m.obj
        if isinstance(o, clp.CanFrame):
            self._upd_bus(o, "RX" if m.type == clp.MSG_CAN_RX else "TX")
        elif isinstance(o, clp.Status):
            self.stat_vars["bus"].set(o.bus_str())
            self.stat_vars["rx_frames"].set(str(o.rx_frames))
            self.stat_vars["tx_frames"].set(str(o.tx_frames))
            self.stat_vars["rx_drops"].set(str(o.rx_drops))
            self.stat_vars["tx_err"].set(str(o.tx_err_cnt))
            self.stat_vars["rx_err"].set(str(o.rx_err_cnt))
        elif isinstance(o, clp.Hello):
            self.link_vars["fw"].set(o.fw_version)
            self._logline(f"HELLO proto={o.proto_ver} max={o.max_payload} fw={o.fw_version!r}")
        elif isinstance(o, clp.TxAck):
            self._logline(f"ACK id-tag=0x{o.tag:04x} status={o.status} ts={o.timestamp}")
        else:
            self._logline(f"{m.name} seq={m.seq} len={len(m.payload)}")

    def _upd_bus(self, f: clp.CanFrame, direction: str):
        key = f"{int(f.ext)}:{f.can_id}"
        now = time.time()
        r = self.rows.get(key)
        cycle = ""
        if r is not None:
            r["count"] += 1
            if f.timestamp and r["last_ts"]:
                cycle = f"{(f.timestamp - r['last_ts']) / TICK_HZ * 1000:.1f}"
            elif r["last_host"]:
                cycle = f"{(now - r['last_host']) * 1000:.0f}"
            r["last_ts"] = f.timestamp
            r["last_host"] = now
        else:
            r = {"count": 1, "last_ts": f.timestamp, "last_host": now}
            self.rows[key] = r
        vals = (f"0x{f.id_str()}", clp.flags_str(f.flags), str(f.dlc),
                f.data[:f.nbytes].hex(" "), str(r["count"]), cycle, direction)
        tag = "tx" if direction == "TX" else "rx"
        if self.bus.exists(key):
            self.bus.item(key, values=vals, tags=(tag,))
        else:
            self.bus.insert("", "end", iid=key, values=vals, tags=(tag,))

    def _logline(self, s: str):
        self.log.configure(state="normal")
        self.log.insert("end", time.strftime("%H:%M:%S ") + s + "\n")
        self.log.see("end")
        # keep it bounded
        if int(self.log.index("end-1c").split(".")[0]) > 400:
            self.log.delete("1.0", "200.0")
        self.log.configure(state="disabled")


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM12"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    root = tk.Tk()
    root.title("CLP - EK-RA8D1 CAN logger")
    root.geometry("960x600")
    App(root, port, baud)
    root.mainloop()


if __name__ == "__main__":
    main()
