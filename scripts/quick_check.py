#!/usr/bin/env python3
"""Quick serial check for AC02 — reads from COM5"""
import serial
import time
import sys

for attempt in range(5):
    try:
        s = serial.Serial('COM5', 115200, timeout=4)
        print(f"[OK] Connected on attempt {attempt+1}")
        time.sleep(0.3)
        data = b''
        start = time.time()
        while time.time() - start < 3.5:
            if s.in_waiting:
                data += s.read(s.in_waiting)
                break
            time.sleep(0.1)
        
        if data:
            text = data.decode('utf-8', errors='replace')
            print(f"[DATA] {len(data)} bytes:")
            for line in text.split('\n'):
                line = line.strip()
                if line:
                    print(f"  {line}")
        else:
            print("[WARN] No data received in 3.5s")
            print("       Check: MCU running? USART2 TX→RX? Baud rate?")
        s.close()
        sys.exit(0)
    except serial.SerialException as e:
        print(f"[WAIT] Attempt {attempt+1}: {e}")
        time.sleep(1.5)

print("[FAIL] COM5 locked. Close CLion serial monitor or other terminal first.")
sys.exit(1)
