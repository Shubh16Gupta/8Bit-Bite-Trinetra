# Barricade LED System (Raspberry Pi Pico)

A physical barricade signal driven by the backend's gate logic:
- **RED LED = close the gate** (gate `CLOSED`)
- **GREEN LED = open the gate** (gate `OPEN`)

LEDs use **active-low (inverted) wiring** and the Pico boots **CLOSED** (red on).
(We used active high)
## Architecture

```
CV crossings ─▶ Backend (/api/status, gate hysteresis) ─▶ agent.py ─▶ Pico ─▶ LEDs
                                                            (polls)    (USB)   red / green
```

- The Pico is a dumb serial LED controller (MicroPython).
- `agent.py` runs on a host machine, polls the backend gate every ~1s, and sends
  `OPEN` / `CLOSE` to the Pico over USB serial.
- **Only `BACKEND_URL` changes** between localhost and Render — Pico firmware and
  agent are identical.

| Environment | BACKEND_URL | Where the agent + Pico run |
|---|---|---|
| **Localhost** | `http://localhost:4000` | on the Mac (same machine as the backend) |
| **Render** | `https://<your-app>.onrender.com` | on another laptop (backend is in the cloud) |

## Wiring (active-low / inverted)

For each LED: **anode (long leg) → 3V3**, **cathode → resistor (~330Ω) → GPIO**.
Driving the GPIO **LOW turns the LED ON**.

| LED | GPIO | meaning |
|---|---|---|
| RED | GP15 | close |
| GREEN | GP14 | open |

(Change the pins at the top of `pico/main.py` if you wire differently.)

## Flash the Pico
1. Install MicroPython on the Pico (hold BOOTSEL, drag the `.uf2`, per the official guide).
2. Copy `pico/main.py` onto the Pico as `main.py` (via Thonny or `mpremote`), so it
   runs on boot.

## Run the agent

```bash
cd barricade
pip install -r requirements.txt

# find the Pico's serial port:
#   macOS/Linux: ls /dev/tty.usbmodem*   (Linux may be /dev/ttyACM0)
#   Windows: Device Manager -> Ports (COMx)

# --- localhost ---
BACKEND_URL=http://localhost:4000 SERIAL_PORT=/dev/tty.usbmodemXXXX python agent.py

# --- render ---
BACKEND_URL=https://<your-app>.onrender.com SERIAL_PORT=/dev/tty.usbmodemXXXX python agent.py
```

You should see `gate -> CLOSED` / `gate -> OPEN` printed as the backend's gate
flips, and the red/green LEDs follow.

## Test without the CV pipeline
Drive the gate directly on the backend and watch the LEDs:

```bash
# fill the room -> gate CLOSES -> RED
for i in $(seq 1 30); do curl -s -XPOST localhost:4000/api/events -H 'Content-Type: application/json' -d '{"type":"enter"}' >/dev/null; done

# empty it -> gate OPENS -> GREEN
for i in $(seq 1 30); do curl -s -XPOST localhost:4000/api/events -H 'Content-Type: application/json' -d '{"type":"exit"}' >/dev/null; done
```

## Notes
- The agent only sends a command when the gate **changes**, so the serial link stays quiet.
- If the backend is unreachable (e.g. Render cold start) the agent keeps retrying;
  the Pico holds its last state.
- Env knobs: `BACKEND_URL`, `SERIAL_PORT`, `BAUD` (default 115200), `POLL_SEC` (default 1.0).
