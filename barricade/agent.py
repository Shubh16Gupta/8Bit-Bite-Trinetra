# Barricade agent: polls the backend gate state and drives the Pico's LEDs
# over USB serial. The SAME script works for localhost and Render — only the
# BACKEND_URL (and the Pico's serial port) change.
#
#   Local :  BACKEND_URL=http://localhost:4000
#   Render:  BACKEND_URL=https://<your-app>.onrender.com
#
# Find the Pico's serial port:
#   macOS/Linux:  ls /dev/tty.usbmodem*   (or /dev/ttyACM0 on Linux)
#   Windows    :  Device Manager -> Ports (COMx)
#
# Run:  pip install pyserial
#       BACKEND_URL=http://localhost:4000 SERIAL_PORT=/dev/tty.usbmodemXXXX python agent.py

import os
import sys
import time
import json
import urllib.request

import serial  # pyserial

BACKEND_URL = os.environ.get("BACKEND_URL", "http://10.117.84.1:4000").rstrip("/")
SERIAL_PORT = os.environ.get("SERIAL_PORT", "/dev/ttyACM0")
BAUD = int(os.environ.get("BAUD", "115200"))
POLL_SEC = float(os.environ.get("POLL_SEC", "1.0"))


def get_gate():
    """Fetch the current gate state ('OPEN' / 'CLOSED') from the backend."""
    with urllib.request.urlopen(f"{BACKEND_URL}/api/status", timeout=8) as r:
        return json.load(r).get("gate")


def open_pico():
    p = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
    time.sleep(2)  # let the Pico's USB CDC settle
    p.write(b"CLOSE\n")  # match the Pico's boot state (closed)
    return p


def main():
    print(f"Barricade agent -> backend={BACKEND_URL}  pico={SERIAL_PORT}")
    pico = open_pico()
    last = None
    while True:
        try:
            gate = get_gate()
            if gate in ("OPEN", "CLOSED") and gate != last:
                cmd = b"OPEN\n" if gate == "OPEN" else b"CLOSE\n"
                pico.write(cmd)
                print(f"gate -> {gate}")
                last = gate
        except serial.SerialException as e:
            print("serial lost, reconnecting:", e)
            try:
                pico = open_pico()
                last = None
            except Exception as e2:
                print("reconnect failed:", e2)
                time.sleep(2)
        except Exception as e:
            print("poll error:", e)  # backend down / cold start -> keep trying
        time.sleep(POLL_SEC)


if __name__ == "__main__":
    main()
