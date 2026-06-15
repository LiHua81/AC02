#!/usr/bin/env python3
"""AC02 serial debug monitor — reads COM5 at 115200 baud."""
import serial
import sys
import time

PORT = "COM5"
BAUD = 115200

def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except serial.SerialException as e:
        print(f"Cannot open {PORT}: {e}")
        sys.exit(1)

    print(f"Listening on {PORT} @ {BAUD}  (Ctrl+C to quit)")
    print("-" * 60)

    buf = b""
    try:
        while True:
            chunk = ser.read(ser.in_waiting or 1)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").rstrip("\r")
                print(text)
    except KeyboardInterrupt:
        print("\n--- stopped ---")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
