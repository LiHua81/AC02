#!/usr/bin/env python3
"""
AC02 Serial Monitor — COM5  115200 8N1

Displays real-time status from the inverter via UART.
Also supports sending SET commands to adjust parameters.

Usage:
  python serial_monitor.py                # Monitor only
  python serial_monitor.py --interactive  # Interactive mode with SET commands
"""

import argparse
import serial
import serial.tools.list_ports
import threading
import time
import sys
import re


class AC02Monitor:
    def __init__(self, port: str = "COM5", baud: int = 115200):
        self.port = port
        self.baud = baud
        self.ser: serial.Serial | None = None
        self.running = False
        self.rx_thread: threading.Thread | None = None

    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.5,
            )
            print(f"  Connected: {self.port} @ {self.baud} 8N1")
            return True
        except serial.SerialException as e:
            print(f"  ERROR: Cannot open {self.port} — {e}")
            print(f"  Available ports: {[p.device for p in serial.tools.list_ports.comports()]}")
            return False

    def disconnect(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        print("  Disconnected.")

    def _parse_line(self, line: str):
        """Parse and display status line from MCU."""
        line = line.strip()
        if not line:
            return

        # Detect known status patterns
        m = re.match(r"\[STATUS\] Vref=([\d.]+) Vrms=([\d.]+) Mod=([\d.]+) Freq=([\d.]+)Hz", line)
        if m:
            vref, vrms, mod, freq = m.groups()
            print(f"\r  Vref={vref:>6}V  Vrms={vrms:>6}V  Mod={mod:>6}  Freq={freq:>6}Hz", end="", flush=True)
            return

        m = re.match(r"\[PARAMS\] KP=([\d.]+) KI=([\d.]+) Vref=([\d.]+) Freq=([\d.]+)", line)
        if m:
            kp, ki, vref, freq = m.groups()
            print(f"\n  → Params updated: KP={kp} KI={ki} Vref={vref} Freq={freq}Hz")
            return

        # Echo or unknown line
        if line.startswith("[ECHO]"):
            print(f"\n  ← {line[7:]}")
        elif line.startswith("[DBG]"):
            print(f"\n  [DBG] {line[5:]}")
        elif line.startswith("[INFO]"):
            print(f"\n  [INFO] {line[6:]}")
        else:
            print(f"\n  {line}")

    def _rx_loop(self):
        """Background thread: read serial and parse lines."""
        buf = ""
        while self.running:
            try:
                if self.ser and self.ser.is_open and self.ser.in_waiting > 0:
                    chunk = self.ser.read(self.ser.in_waiting).decode("utf-8", errors="replace")
                    buf += chunk
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        self._parse_line(line)
                else:
                    time.sleep(0.01)
            except (serial.SerialException, OSError):
                break

    def start(self):
        if not self.connect():
            return False
        self.running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()
        return True

    def send_command(self, cmd: str):
        """Send a SET command to the MCU."""
        if not self.ser or not self.ser.is_open:
            print("  Not connected!")
            return
        if not cmd.endswith("\n"):
            cmd += "\n"
        self.ser.write(cmd.encode("utf-8"))
        time.sleep(0.05)  # Wait for MCU to process

    def interactive(self):
        """Interactive command mode."""
        print("\n  Interactive mode. Type commands or 'q' to quit.")
        print("  Commands:")
        print("    SET V:<value>  — Set voltage reference (0.05 ~ 0.95)")
        print("    SET P:<value>  — Set PI Kp")
        print("    SET I:<value>  — Set PI Ki")
        print("    SET F:<value>  — Set frequency (40.0 ~ 70.0 Hz)")
        print("    STATUS         — Request status report")
        print("    PARAMS         — Request params report")
        print()

        while self.running:
            try:
                cmd = input("  > ").strip()
                if not cmd:
                    continue
                if cmd.lower() == "q":
                    break
                if cmd.upper() in ("STATUS", "PARAMS"):
                    self.send_command(cmd)
                elif cmd.startswith("SET "):
                    self.send_command(cmd)
                    print(f"  Sent: {cmd}")
                else:
                    print(f"  Unknown: {cmd}")
            except (EOFError, KeyboardInterrupt):
                break


def main():
    parser = argparse.ArgumentParser(description="AC02 Serial Monitor")
    parser.add_argument("--port", default="COM5", help="Serial port (default: COM5)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--interactive", "-i", action="store_true", help="Interactive command mode")
    args = parser.parse_args()

    monitor = AC02Monitor(args.port, args.baud)

    print("=" * 50)
    print("  AC02 DC-AC Inverter — Serial Monitor")
    print(f"  Port: {args.port}  Baud: {args.baud}")
    print("=" * 50)
    print()

    if not monitor.start():
        sys.exit(1)

    print("  Monitoring... Press Ctrl+C to exit.\n")

    try:
        if args.interactive:
            monitor.interactive()
        else:
            while True:
                time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n  Interrupted.")
    finally:
        monitor.disconnect()


if __name__ == "__main__":
    # Ensure pyserial is available
    try:
        import serial
    except ImportError:
        print("  ERROR: pyserial not installed.")
        print("  Install: pip install pyserial")
        sys.exit(1)
    main()
