#!/usr/bin/env python
"""Reset the board once and print its serial boot log.

Usage: python tools/bootlog.py [/dev/cu.usbserial-10] [seconds=60]
Needs pyserial. On this Mac use Homebrew's esptool venv Python:
  /opt/homebrew/Cellar/esptool/5.4.0/libexec/bin/python tools/bootlog.py
One reset is safe once the boot counter has cleared (>40 s after 'Ready!').
"""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-10'
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 60
s = serial.Serial(port, 115200, timeout=0.5)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)  # esptool hard reset, not flash mode
t0 = time.time()
while time.time() - t0 < secs:
    d = s.read(4096)
    if d:
        sys.stdout.write(d.decode('utf-8', 'replace')); sys.stdout.flush()
s.close()
