#!/usr/bin/env python3
"""
AC02 Serial Monitor GUI
=======================
Real-time dashboard for the DC-AC inverter via UART.
- Live status display (Vrms / Vref / Mod / Freq / KP / KI)
- One-click parameter tuning (KP / KI / Vref / Freq)
- Real-time trend charts (matplotlib)
- Raw log console + command input
- CSV data export

Requirements:
  pip install pyserial matplotlib

Usage:
  python serial_monitor_gui.py                  # auto-detect COM port
  python serial_monitor_gui.py --port COM5      # specify port
  python serial_monitor_gui.py --simulate       # demo mode, no hardware
"""

import argparse
import csv
import io
import random
import re
import serial
import serial.tools.list_ports
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from datetime import datetime

try:
    import matplotlib
    matplotlib.use("TkAgg")
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
    HAS_MPL = True
except ImportError:
    HAS_MPL = False

# ── Theme ────────────────────────────────────────────────────────────────────

_BG       = "#1a1b26"   # window background
_BG_LIGHT = "#24283b"   # panel / entry background
_FG       = "#c0caf5"   # primary text
_FG_DIM   = "#565f89"   # secondary text
_GREEN    = "#9ece6a"
_BLUE     = "#7aa2f7"
_YELLOW   = "#e0af68"
_RED      = "#f7768e"
_CYAN     = "#7dcfff"
_MAGENTA  = "#bb9af7"

# ── Parameter definitions ────────────────────────────────────────────────────

PARAM_DEFS = {
    "KP":   {"cmd": "SET P:", "min": 0.0,   "max": 5.0,  "step": 0.01, "default": 0.2,  "color": _MAGENTA},
    "KI":   {"cmd": "SET I:", "min": 0.0,   "max": 5.0,  "step": 0.01, "default": 0.1,  "color": _CYAN},
    "Vref": {"cmd": "SET V:", "min": 0.5,   "max": 20.0, "step": 0.5,  "default": 10.0, "color": _GREEN},
    "Freq": {"cmd": "SET F:", "min": 40.0,  "max": 70.0, "step": 0.5,  "default": 50.0, "color": _YELLOW},
}

# ── Simulator ────────────────────────────────────────────────────────────────

class SimSerial:
    """Fake serial port that generates realistic inverter output."""

    def __init__(self):
        self.is_open = True
        self._buf = b""
        self._last = 0.0
        self._vrms = 0.0
        self._vref = 10.0
        self._freq = 50.0
        self._kp = 0.2
        self._ki = 0.1
        self._mod = 0.0
        self._t = 0.0

    @property
    def in_waiting(self):
        return len(self._buf)

    def _generate(self):
        self._t += 0.05
        target = self._vref
        self._vrms += (target - self._vrms) * 0.03 + random.gauss(0, 0.05)
        self._vrms = max(0, self._vrms)
        self._mod = self._vrms / 20.0 * 1.1 + random.gauss(0, 0.005)
        self._mod = max(0, min(1.0, self._mod))

        def f2i1(v):
            neg = v < 0
            v = abs(v)
            r = int(v * 10.0 + 0.5)
            return -r if neg else r

        def f2i2(v):
            neg = v < 0
            v = abs(v)
            r = int(v * 100.0 + 0.5)
            return -r if neg else r

        vrms = f2i1(self._vrms)
        vr   = f2i1(self._vref)
        md   = f2i2(self._mod)
        fr   = f2i1(self._freq)
        kp   = f2i2(self._kp)
        ki   = f2i2(self._ki)

        return (
            f"Vrms={vr//10}.{abs(vr)%10}V "
            f"Vref={vr//10}.{abs(vr)%10}V "
            f"F={fr//10}.{abs(fr)%10}Hz "
            f"M={md//100}.{abs(md)%100:02d} "
            f"P={kp//100}.{abs(kp)%100:02d} "
            f"I={ki//100}.{abs(ki)%100:02d}\r\n"
        ).encode()

    def read(self, size=-1):
        now = time.time()
        if now - self._last >= 0.05:
            self._buf += self._generate()
            self._last = now
        if size < 0:
            size = len(self._buf)
        data, self._buf = self._buf[:size], self._buf[size:]
        return data

    def write(self, data):
        text = data.decode().strip()
        m = re.match(r"SET ([PIVF]):(.+)", text)
        if m:
            key, val = m.group(1), float(m.group(2))
            if key == "P": self._kp = val
            elif key == "I": self._ki = val
            elif key == "V": self._vref = val
            elif key == "F": self._freq = val
            echo = f"[ECHO] {key} set to {val}\r\n".encode()
            self._buf += echo

    def close(self):
        self.is_open = False

# ── Main Panel ───────────────────────────────────────────────────────────────

class AC02Panel:
    MAX_PLOT_POINTS = 600   # ~30 s at 20 Hz
    CHART_WINDOW = 10       # seconds to display

    def __init__(self, port: str, baud: int = 115200, simulate: bool = False):
        self.port = port
        self.baud = baud
        self.simulate = simulate
        self.ser = None
        self.running = False
        self.rx_thread = None
        self.recording = False
        self.csv_rows = []
        self._last_raw = ""
        self._parse_ok = False

        # live values
        self.vals = {
            "Vrms": 0.0, "Vref": 10.0, "Freq": 50.0,
            "Mod": 0.0, "KP": 0.2, "KI": 0.1,
        }
        # time-series buffers
        self.ts = {k: ([], []) for k in ("Vrms", "Vref", "Mod", "Freq")}

        self.root = tk.Tk()
        self.root.title("AC02 DC-AC Inverter Monitor")
        self.root.configure(bg=_BG)
        self.root.minsize(920, 720)

        # matplotlib style
        if HAS_MPL:
            plt.style.use("dark_background")
            plt.rcParams.update({
                "figure.facecolor": _BG,
                "axes.facecolor":   _BG_LIGHT,
                "axes.edgecolor":   _FG_DIM,
                "axes.labelcolor":  _FG,
                "xtick.color":      _FG_DIM,
                "ytick.color":      _FG_DIM,
                "grid.color":       _FG_DIM,
                "grid.alpha":       0.3,
                "font.size":        9,
            })

        self._vars = {}
        self._build_ui()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(100, self._tick)
        self.root.after(500, self._chart_tick)

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        self._build_toolbar()
        self._build_body()

    def _build_toolbar(self):
        bar = tk.Frame(self.root, bg=_BG_LIGHT, height=44)
        bar.pack(fill="x", padx=0, pady=0)
        bar.pack_propagate(False)

        tk.Label(bar, text="  AC02 Inverter", font=("Consolas", 12, "bold"),
                 bg=_BG_LIGHT, fg=_BLUE).pack(side="left", padx=(10, 20))

        # port selector
        tk.Label(bar, text="Port:", bg=_BG_LIGHT, fg=_FG).pack(side="left")
        self._port_var = tk.StringVar()
        ports = [p.device for p in serial.tools.list_ports.comports()] if not self.simulate else []
        default_port = self.port if self.port in ports else (ports[0] if ports else self.port)
        self._port_var.set(default_port)
        self._port_combo = ttk.Combobox(bar, textvariable=self._port_var, width=8, state="readonly")
        self._port_combo["values"] = ports if ports else [self.port]
        self._port_combo.pack(side="left", padx=4)

        # baud
        tk.Label(bar, text="Baud:", bg=_BG_LIGHT, fg=_FG).pack(side="left", padx=(8, 0))
        self._baud_var = tk.StringVar(value=str(self.baud))
        ttk.Combobox(bar, textvariable=self._baud_var, width=7, state="readonly",
                     values=["9600", "115200"]).pack(side="left", padx=4)

        # connect button
        self._conn_btn = tk.Button(
            bar, text="  Connect  ", font=("Consolas", 9, "bold"),
            bg=_GREEN, fg=_BG, activebackground=_BLUE, activeforeground="white",
            bd=0, padx=12, command=self._toggle_conn,
        )
        self._conn_btn.pack(side="left", padx=10)

        # status indicator
        self._status_dot = tk.Label(bar, text="●", font=("Consolas", 10),
                                    bg=_BG_LIGHT, fg=_RED)
        self._status_dot.pack(side="left", padx=(0, 4))
        self._status_lbl = tk.Label(bar, text="Disconnected", bg=_BG_LIGHT, fg=_FG_DIM)
        self._status_lbl.pack(side="left")

        # record button
        self._rec_btn = tk.Button(
            bar, text="  Record CSV  ", font=("Consolas", 9),
            bg=_YELLOW, fg=_BG, bd=0, padx=8, command=self._toggle_record,
        )
        self._rec_btn.pack(side="right", padx=10)

    def _build_body(self):
        body = tk.Frame(self.root, bg=_BG)
        body.pack(fill="both", expand=True, padx=8, pady=(4, 8))

        # top row: status cards + param controls
        top = tk.Frame(body, bg=_BG)
        top.pack(fill="x", pady=(0, 6))

        self._build_status_cards(top)
        self._build_param_panel(top)

        # raw data bar — shows exactly what UART sends
        raw_frame = tk.Frame(body, bg=_BG_LIGHT, height=28)
        raw_frame.pack(fill="x", pady=(0, 4))
        raw_frame.pack_propagate(False)
        tk.Label(raw_frame, text=" RAW> ", font=("Consolas", 8, "bold"),
                 bg=_BG_LIGHT, fg=_FG_DIM).pack(side="left", padx=(4, 0))
        self._raw_var = tk.StringVar(value="(waiting for data...)")
        tk.Label(raw_frame, textvariable=self._raw_var, font=("Consolas", 9),
                 bg=_BG_LIGHT, fg=_CYAN, anchor="w").pack(side="left", fill="x", expand=True, padx=4)

        # bottom: charts + log
        if HAS_MPL:
            self._build_charts(body)
        self._build_log(body)

    def _build_status_cards(self, parent):
        frame = tk.LabelFrame(parent, text="  Live Status  ", font=("Consolas", 9, "bold"),
                              bg=_BG, fg=_BLUE, bd=1, relief="groove")
        frame.pack(side="left", fill="both", expand=True, padx=(0, 6))

        cards_def = [
            ("Vrms", "V",    _GREEN,  1),
            ("Vref", "V",    _BLUE,   2),
            ("Mod",  "",     _MAGENTA, 3),
            ("Freq", "Hz",   _YELLOW, 4),
            ("KP",   "",     _CYAN,   5),
            ("KI",   "",     _CYAN,   6),
        ]
        for name, unit, color, col in cards_def:
            c = tk.Frame(frame, bg=_BG_LIGHT, padx=10, pady=6)
            c.grid(row=1, column=col, padx=4, pady=6, sticky="nsew")
            frame.columnconfigure(col, weight=1)

            tk.Label(c, text=name, font=("Consolas", 8), bg=_BG_LIGHT, fg=_FG_DIM).pack()
            var = tk.StringVar(value="  0.00")
            tk.Label(c, textvariable=var, font=("Consolas", 20, "bold"),
                     bg=_BG_LIGHT, fg=color, width=7, anchor="e").pack()
            if unit:
                tk.Label(c, text=unit, font=("Consolas", 8), bg=_BG_LIGHT, fg=_FG_DIM).pack()
            self._vars[name] = var

    def _build_param_panel(self, parent):
        frame = tk.LabelFrame(parent, text="  Parameter Control  ", font=("Consolas", 9, "bold"),
                              bg=_BG, fg=_GREEN, bd=1, relief="groove")
        frame.pack(side="right", fill="y", padx=(0, 0))

        self._param_vars = {}

        for i, (name, cfg) in enumerate(PARAM_DEFS.items()):
            row = tk.Frame(frame, bg=_BG)
            row.pack(fill="x", padx=8, pady=4)

            tk.Label(row, text=name, font=("Consolas", 9, "bold"),
                     bg=_BG, fg=cfg["color"], width=5, anchor="w").pack(side="left")

            var = tk.StringVar(value=f"{cfg['default']:.2f}")
            self._param_vars[name] = var

            spin = tk.Spinbox(
                row, textvariable=var,
                from_=cfg["min"], to=cfg["max"], increment=cfg["step"],
                width=7, font=("Consolas", 10),
                bg=_BG_LIGHT, fg=_FG, buttonbackground=_BG_LIGHT,
                relief="flat", bd=0,
            )
            spin.pack(side="left", padx=4)

            scale = tk.Scale(
                row, from_=cfg["min"], to=cfg["max"], resolution=cfg["step"],
                orient="horizontal", variable=var,
                length=120, showvalue=0,
                bg=_BG, fg=_FG, troughcolor=_BG_LIGHT,
                highlightthickness=0, sliderrelief="flat",
            )
            scale.pack(side="left", padx=2)

            tk.Button(
                row, text="Set", font=("Consolas", 8, "bold"),
                bg=cfg["color"], fg=_BG, bd=0, padx=6,
                command=lambda n=name: self._send_param(n),
            ).pack(side="left", padx=(6, 0))

        btn_row = tk.Frame(frame, bg=_BG)
        btn_row.pack(fill="x", padx=8, pady=(8, 4))

        tk.Button(
            btn_row, text="Send All", font=("Consolas", 9, "bold"),
            bg=_GREEN, fg=_BG, bd=0, padx=10,
            command=self._send_all_params,
        ).pack(side="left", padx=(0, 4))

        tk.Button(
            btn_row, text="Read", font=("Consolas", 9),
            bg=_BLUE, fg=_BG, bd=0, padx=10,
            command=lambda: self._send_raw("PARAMS"),
        ).pack(side="left", padx=2)

    def _build_charts(self, parent):
        frame = tk.LabelFrame(parent, text="  Trends  ", font=("Consolas", 9, "bold"),
                              bg=_BG, fg=_YELLOW, bd=1, relief="groove")
        frame.pack(fill="both", expand=True, pady=(0, 6))

        self.fig = Figure(figsize=(9, 2.8), dpi=90)
        self.fig.patch.set_facecolor(_BG)
        self.fig.subplots_adjust(left=0.06, right=0.92, top=0.94, bottom=0.14, hspace=0.3)

        gs = self.fig.add_gridspec(2, 1, height_ratios=[1, 1])
        self.ax_v = self.fig.add_subplot(gs[0])
        self.ax_m = self.fig.add_subplot(gs[1])

        for ax, label, color in [
            (self.ax_v, "Voltage (V)", _GREEN),
            (self.ax_m, "Modulation",  _MAGENTA),
        ]:
            ax.set_facecolor(_BG_LIGHT)
            ax.set_ylabel(label, fontsize=8, color=color)
            ax.grid(True, alpha=0.25, color=_FG_DIM)
            ax.tick_params(labelsize=7)

        self.ax_v.set_ylim(0, 22)
        self.ax_m.set_ylim(0, 1.05)
        self.ax_m.set_xlabel("Time (s)", fontsize=8, color=_FG_DIM)

        # Pre-create line objects — update data only, never clear axes
        self._line_vrms, = self.ax_v.plot([], [], color=_GREEN, linewidth=1.4, label="Vrms")
        self._line_vref, = self.ax_v.plot([], [], color=_BLUE,   linewidth=1.4, label="Vref")
        self._line_mod,  = self.ax_m.plot([], [], color=_MAGENTA, linewidth=1.4)
        self.ax_v.legend(loc="upper right", fontsize=7, framealpha=0.3)

        self.canvas = FigureCanvasTkAgg(self.fig, master=frame)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)

    def _build_log(self, parent):
        frame = tk.LabelFrame(parent, text="  Log  ", font=("Consolas", 9, "bold"),
                              bg=_BG, fg=_FG_DIM, bd=1, relief="groove")
        frame.pack(fill="x")

        self._log_text = tk.Text(
            frame, height=6, font=("Consolas", 9),
            bg=_BG_LIGHT, fg=_FG, insertbackground=_FG,
            selectbackground=_BLUE, relief="flat", bd=0,
            state="disabled",
        )
        scrollbar = tk.Scrollbar(frame, command=self._log_text.yview, bg=_BG_LIGHT, troughcolor=_BG)
        self._log_text.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self._log_text.pack(fill="x", padx=2, pady=2)

        cmd_row = tk.Frame(frame, bg=_BG)
        cmd_row.pack(fill="x", padx=2, pady=(0, 2))

        tk.Label(cmd_row, text=">", font=("Consolas", 9, "bold"),
                 bg=_BG, fg=_FG_DIM).pack(side="left", padx=(4, 2))
        self._cmd_var = tk.StringVar()
        cmd_entry = tk.Entry(
            cmd_row, textvariable=self._cmd_var, font=("Consolas", 9),
            bg=_BG_LIGHT, fg=_FG, insertbackground=_FG,
            relief="flat", bd=0,
        )
        cmd_entry.pack(side="left", fill="x", expand=True, padx=2, ipady=2)
        cmd_entry.bind("<Return>", lambda e: self._send_cmd_entry())

        tk.Button(
            cmd_row, text="Send", font=("Consolas", 8),
            bg=_BLUE, fg=_BG, bd=0, padx=8,
            command=self._send_cmd_entry,
        ).pack(side="left", padx=(2, 4))

    # ── Connection ───────────────────────────────────────────────────────────

    def _toggle_conn(self):
        if self.running:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        try:
            if self.simulate:
                self.ser = SimSerial()
            else:
                self.ser = serial.Serial(
                    port=self._port_var.get(),
                    baudrate=int(self._baud_var.get()),
                    bytesize=serial.EIGHTBITS,
                    parity=serial.PARITY_NONE,
                    stopbits=serial.STOPBITS_ONE,
                    timeout=0.2,
                )
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))
            return

        self.running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()

        self._conn_btn.configure(text="Disconnect", bg=_RED)
        self._status_dot.configure(fg=_GREEN)
        self._status_lbl.configure(text=f"Connected: {self._port_var.get()}")
        self._log(f"Connected to {self._port_var.get()} @ {self._baud_var.get()}")

    def _disconnect(self):
        self.running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=1)
        if self.ser:
            self.ser.close()
            self.ser = None

        self._conn_btn.configure(text="  Connect  ", bg=_GREEN)
        self._status_dot.configure(fg=_RED)
        self._status_lbl.configure(text="Disconnected")
        self._log("Disconnected")

    # ── Serial RX ────────────────────────────────────────────────────────────

    def _rx_loop(self):
        buf = ""
        while self.running:
            try:
                if not self.ser or not self.ser.is_open:
                    break
                waiting = self.ser.in_waiting
                if waiting > 0:
                    chunk = self.ser.read(waiting).decode("utf-8", errors="replace")
                    buf += chunk
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        self._on_line(line)
                else:
                    time.sleep(0.02)
            except Exception:
                break

    # Generic key=value extractor — works regardless of field order or format
    _RE_KV = re.compile(r"(Vrms|Vref|Freq|Mod|KP|KI|F|M|P|I)=([-]?\d+\.?\d*)")

    def _on_line(self, line):
        line = line.strip()
        if not line:
            return

        self._last_raw = line
        self._parse_ok = False
        self._log(line)

        # Skip echo/info lines that don't contain measurements
        if "=" not in line:
            return

        found = {}
        for m in self._RE_KV.finditer(line):
            found[m.group(1)] = float(m.group(2))

        # Normalize short keys to canonical names
        if "F" in found and "Freq" not in found:
            found["Freq"] = found.pop("F")
        if "M" in found and "Mod" not in found:
            found["Mod"] = found.pop("M")
        if "P" in found and "KP" not in found:
            found["KP"] = found.pop("P")
        if "I" in found and "KI" not in found:
            found["KI"] = found.pop("I")

        # Need at least 2 known keys to consider it a valid status line
        known = [k for k in ("Vrms", "Vref", "Freq", "Mod", "KP", "KI") if k in found]
        if len(known) < 2:
            return

        self._parse_ok = True
        for key in ("Vrms", "Vref", "Freq", "Mod", "KP", "KI"):
            if key in found:
                self.vals[key] = found[key]

        self._record_sample(time.time())

    def _record_sample(self, now):
        for key in ("Vrms", "Vref", "Mod", "Freq"):
            times, data = self.ts[key]
            times.append(now)
            data.append(self.vals[key])
            if len(times) > self.MAX_PLOT_POINTS:
                del times[:-self.MAX_PLOT_POINTS]
                del data[:-self.MAX_PLOT_POINTS]

        if self.recording:
            self.csv_rows.append([
                datetime.now().isoformat(timespec="milliseconds"),
                self.vals["Vrms"], self.vals["Vref"],
                self.vals["Freq"], self.vals["Mod"],
                self.vals["KP"], self.vals["KI"],
            ])

    # ── Commands ─────────────────────────────────────────────────────────────

    def _send_raw(self, cmd: str):
        if not self.ser or not self.ser.is_open:
            return
        if not cmd.endswith("\n"):
            cmd += "\n"
        try:
            self.ser.write(cmd.encode("utf-8"))
        except Exception as e:
            self._log(f"[TX ERROR] {e}")

    def _send_param(self, name):
        cfg = PARAM_DEFS[name]
        try:
            val = float(self._param_vars[name].get())
        except ValueError:
            return
        val = max(cfg["min"], min(cfg["max"], val))
        self._send_raw(f"{cfg['cmd']}{val}")
        self._log(f"TX: {cfg['cmd']}{val}")

    def _send_all_params(self):
        for name in PARAM_DEFS:
            self._send_param(name)
            time.sleep(0.05)

    def _send_cmd_entry(self):
        cmd = self._cmd_var.get().strip()
        if cmd:
            self._send_raw(cmd)
            self._log(f"TX: {cmd}")
            self._cmd_var.set("")

    # ── Recording ────────────────────────────────────────────────────────────

    def _toggle_record(self):
        if self.recording:
            self.recording = False
            self._rec_btn.configure(text="  Record CSV  ", bg=_YELLOW)
            self._save_csv()
        else:
            self.recording = True
            self.csv_rows.clear()
            self._rec_btn.configure(text="■ Stop Recording", bg=_RED)
            self._log("Recording started — click again to save")

    def _save_csv(self):
        if not self.csv_rows:
            self._log("No data recorded")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv"), ("All", "*.*")],
            initialfile=f"ac02_{datetime.now():%Y%m%d_%H%M%S}.csv",
        )
        if not path:
            self._rec_btn.configure(text="  Record CSV  ", bg=_YELLOW)
            return
        try:
            with open(path, "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["timestamp", "Vrms", "Vref", "Freq", "Mod", "KP", "KI"])
                w.writerows(self.csv_rows)
            self._log(f"Saved {len(self.csv_rows)} rows → {path}")
        except Exception as e:
            self._log(f"[SAVE ERROR] {e}")
        self._rec_btn.configure(text="  Record CSV  ", bg=_YELLOW)

    # ── Log ──────────────────────────────────────────────────────────────────

    def _log(self, text: str):
        try:
            self._log_text.configure(state="normal")
            self._log_text.insert("end", text + "\n")
            # keep max 500 lines
            line_count = int(self._log_text.index("end-1c").split(".")[0])
            if line_count > 500:
                self._log_text.delete("1.0", f"{line_count - 500}.0")
            self._log_text.see("end")
            self._log_text.configure(state="disabled")
        except tk.TclError:
            pass

    # ── Periodic UI update ───────────────────────────────────────────────────

    def _tick(self):
        try:
            self._update_status_labels()
            if self._last_raw:
                tag = "OK" if self._parse_ok else "??"
                self._raw_var.set(f"[{tag}] {self._last_raw}")
        except Exception as e:
            self._log(f"[GUI ERROR] {e}")
        finally:
            self.root.after(100, self._tick)

    def _chart_tick(self):
        try:
            if HAS_MPL:
                self._update_plots()
        except Exception as e:
            self._log(f"[CHART ERROR] {e}")
        finally:
            self.root.after(500, self._chart_tick)

    def _update_status_labels(self):
        self._vars["Vrms"].set(f"{self.vals['Vrms']:.2f}")
        self._vars["Vref"].set(f"{self.vals['Vref']:.2f}")
        self._vars["Mod"].set(f"{self.vals['Mod']:.3f}")
        self._vars["Freq"].set(f"{self.vals['Freq']:.2f}")
        self._vars["KP"].set(f"{self.vals['KP']:.2f}")
        self._vars["KI"].set(f"{self.vals['KI']:.2f}")

    def _update_plots(self):
        if not self.ts["Vrms"][0]:
            return

        if not getattr(self, '_chart_started', False):
            self._chart_started = True
            self._log(f"[CHART] Rendering started, {len(self.ts['Vrms'][0])} points")

        now = self.ts["Vrms"][0][-1]
        t_start = now - self.CHART_WINDOW

        pairs = [
            (self._line_vrms, self.ax_v, "Vrms"),
            (self._line_vref, self.ax_v, "Vref"),
            (self._line_mod,  self.ax_m, "Mod"),
        ]

        for line, ax, key in pairs:
            times, data = self.ts[key]
            # filter to visible window
            vis = [(t, d) for t, d in zip(times, data) if t >= t_start]
            if not vis:
                continue
            t_rel = [t - t_start for t, _ in vis]
            vals  = [d for _, d in vis]
            line.set_data(t_rel, vals)
            ax.set_xlim(0, self.CHART_WINDOW)

        self.canvas.draw_idle()

    # ── Shutdown ─────────────────────────────────────────────────────────────

    def _on_close(self):
        self.running = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.root.destroy()

    def run(self):
        self.root.mainloop()


# ── Entry point ──────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="AC02 Serial Monitor GUI")
    ap.add_argument("--port", default="COM5", help="Serial port (default: COM5)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    ap.add_argument("--simulate", action="store_true",
                    help="Simulate inverter data (no hardware needed)")
    args = ap.parse_args()

    if not HAS_MPL:
        print("WARNING: matplotlib not installed — charts disabled")
        print("  Install: pip install matplotlib")

    panel = AC02Panel(args.port, args.baud, args.simulate)
    panel.run()


if __name__ == "__main__":
    main()
