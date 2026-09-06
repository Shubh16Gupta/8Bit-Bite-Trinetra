# TRINETRA — Temple Crowd Management System · Project Context / Handoff

> **Purpose of this file:** complete, self-contained context for a teammate (and/or another LLM) picking up this project on a different machine. It covers the architecture, tech stack, every component, the APIs and data contracts, how to run everything, current status, and known gotchas.

**Tagline:** *Smarter Temples. Safer Darshan.* — AI-powered real-time crowd monitoring, occupancy/gate control, indoor positioning (SOS bands), and an admin dashboard for temples.

---

## 1. High-level architecture

Four independent parts that talk over HTTP + WebSocket:

```
  ┌─────────────────────┐        enter/exit + outside count (HTTP POST)
  │  PYTHON CV ENGINE    │───────────────────────────────────────────┐
  │  (mandir-crowd-      │        MJPEG annotated video (HTTP GET)    │
  │   monitor, :8090)    │◀────────────────────────── browser <img>  │
  └─────────────────────┘                                            ▼
                                                        ┌────────────────────────┐
  ┌─────────────────────┐    position + SOS (HTTP POST) │   NODE BACKEND         │
  │  ESP32 SOS BAND      │──────────────────────────────▶│  Express + socket.io   │
  │  (band/ firmware)    │                               │  + MongoDB  (:4000)    │
  └─────────────────────┘                               └────────────────────────┘
                                                              │  REST + socket.io
                                                              ▼
                                                   ┌────────────────────────┐
                                                   │  REACT DASHBOARD       │
                                                   │  Vite + Tailwind       │
                                                   │  (:5173)               │
                                                   └────────────────────────┘
```

- **Python CV engine** = the "sensor". Reads phone/CCTV camera streams, runs YOLO person detection + tracking, does entry/exit line-counting (darshan room) and live people-count (outside corridor), serves annotated MJPEG video, and POSTs events/counts to the backend.
- **Node backend** = the "brain". Owns authoritative occupancy, gate open/close logic (hysteresis), per-zone state, ESP32 band positions, and admin auth. Pushes live updates to the dashboard via socket.io.
- **React dashboard** = the admin UI. Login-gated. Shows live occupancy/gate, zone feeds with click-to-analytics, and (mostly mock) crowd-management widgets.
- **ESP32 SOS band** = wearable for elderly/vulnerable visitors. BLE RSSI trilateration → estimated (x,y) position POSTed to backend every ~2s; a physical SOS button (5s hold) latches an alert (`is_active=true`) + buzzer + faster reporting.

---

## 2. Repositories & directory layout

Everything lives under `~/Desktop/Trinetra/` on the dev machine. **Important: there are duplicate copies — read this carefully.**

```
Trinetra/
├── mandir-crowd-monitor/     # Python CV engine + a Node backend dev copy
│   ├── main.py               # entry: runs 3 zone pipelines + MJPEG feed server
│   ├── demo_video.py         # single-file demo of the pipeline
│   ├── requirements.txt
│   ├── venv/                 # Python virtualenv
│   ├── yolov8n.pt            # YOLO nano weights (auto-downloaded)
│   ├── videos/               # vtest.avi, Entrance.mp4, Exit.mp4 (test clips)
│   ├── src/                  # CV modules (see §4)
│   ├── tests/                # pytest suite (23 tests)
│   └── backend/              # Node backend — DEV COPY, synced into shared-repo before push
│
├── trinetra-frontend/        # React dashboard — ACTIVE, EDITED, **NOT pushed** (own git)
│   ├── src/                  # pages, components, contexts, hooks, services
│   ├── public/               # hero-temple.png, trinetra-logo.png/.jpeg, trinetra-icon.png
│   ├── vite.config.js, tailwind.config.js, package.json
│
├── shared-repo/              # clone of the GitHub monorepo (git pushes happen here)
│   ├── src/                  # the frontend AS ON GITHUB (older, teammate's — NOT my edits)
│   ├── backend/              # the backend AS ON GITHUB (pushed, up to date)
│   └── band/                 # ESP32 firmware (teammate pushed here)
│
├── Entrance.mp4, Exit.mp4    # original test videos (copies also in videos/)
└── PROJECT_CONTEXT.md        # this file
```

### GitHub
- **Repo:** `https://github.com/Shubh16Gupta/8Bit-Bite-Trinetra` (branch `main`).
- It is a **monorepo**: React frontend at root + `backend/` (Node) + `band/` (ESP32).
- Collaborator pushing: GitHub user `adarshdev200` (email `tadarsh2006@gmail.com`).

### ⚠️ Duplication / source-of-truth warning
- **Backend** truth = `mandir-crowd-monitor/backend/`. It is **rsync'd into `shared-repo/backend/`** before each push. `shared-repo/backend` == GitHub == up to date (latest commit `98e2a11`).
- **Frontend** is edited in `trinetra-frontend/` and is **NOT pushed**. The frontend on GitHub (`shared-repo/src`) is the teammate's older version **without** the hero redesign, logo, or live-data wiring described below.
- **Band firmware** truth on GitHub = `shared-repo/band/`. A local dev copy of the firmware also exists in `~/Downloads/` (the files that were edited: `config.h`, `network.cpp/.h`, `sos.cpp/.h`, `ble_*.cpp/.h`, `trilateration.*`, `.ino`).

---

## 3. Tech stack

| Layer | Tech |
|---|---|
| CV engine | Python 3.11, OpenCV (`opencv-python`), Ultralytics YOLOv8-nano, ByteTrack tracker, `lap`, Flask + flask-cors (MJPEG server), stdlib `urllib`/`threading` |
| Backend | Node.js 24, Express 4, Mongoose 8 + MongoDB 7, socket.io 4, jsonwebtoken 9, cors, dotenv |
| Frontend | React 19, Vite 8 (rolldown), Tailwind 3, react-router-dom 7, socket.io-client, axios, recharts, leaflet/react-leaflet, framer-motion, lucide-react, Radix UI |
| Hardware | ESP32-S3 (N16R8 = 16MB flash / 8MB OPI PSRAM), Arduino framework, ArduinoJson, ESP32 BLE Arduino, FreeRTOS tasks |

**Ports:** MongoDB `27017` · Node backend `4000` · Python MJPEG feeds `8090` · Vite dev `5173`.

---

## 4. Python CV engine (`mandir-crowd-monitor/`)

### Concept
Three **zones**, each a camera in its own thread:
- `darshan` — **entrance** camera → line-crossing → posts `enter` events.
- `darshan_exit` — **exit** camera → line-crossing → posts `exit` events.
- `outside` — corridor camera → **live people count** (grace-period smoothed) → posts count.

Darshan occupancy = `entries − exits` (computed & held authoritatively by the backend). Outside = current visible count.

### `src/` modules
| File | Responsibility |
|---|---|
| `video_stream.py` | `VideoStream` — open camera/URL/file, read frames, retry flaky network streams (15 retries). |
| `detector.py` | `Detector` — loads YOLOv8n; `detect()` (plain) and `track()` (ByteTrack, `persist=True`). Filters to `person` (COCO class 0). Per-instance so each camera has its own tracker. |
| `line_counter.py` | `LineCounter` — horizontal virtual line; counts a crossing once when a tracked id's box-center flips sides (down/up). |
| `counter.py` | `UniqueCounter` (total distinct) and `OccupancyCounter` (current count with a frames-based grace period to ride out occlusion). |
| `occupancy.py` | `Occupancy` — local occupancy with safety rules (never < 0; over-capacity allowed & flagged). Mostly superseded by backend but used by `demo_video.py`. |
| `zone_pipeline.py` | `ZonePipeline(threading.Thread)` — per-camera loop: capture → track → (line-count or occupancy-count) → annotate frame → keep latest JPEG → POST events/counts via `SignalClient`. `crossing_delta(role, direction, forward)` maps a crossing to +1/−1. |
| `web_stream.py` | Flask app: `GET /feed/<zone>` (MJPEG), `GET /zones` (JSON), `GET /health`, `GET /` (a viewer page showing all feeds). |
| `signal_client.py` | `SignalClient` — fire-and-forget HTTP POST (daemon threads, errors swallowed) to backend: `send_event`, `send_zone_count`, `reset`. |

### `main.py` config (edit for real cameras)
```python
BACKEND_URL = "http://localhost:4000"
FEED_PORT   = 8090
DEMO_SOURCE = "videos/vtest.avi"
ZONES = {
  "darshan":      {source: "videos/Entrance.mp4", role:"entrance", forward:"down", line_position:0.5, confidence:0.4},
  "darshan_exit": {source: "videos/Exit.mp4",     role:"exit",     forward:"down", line_position:0.5, confidence:0.4},
  "outside":      {source: DEMO_SOURCE,           role:"count"},
}
```
- For **live phones**, set each `source` to the phone's stream URL (IP Webcam: `http://<ip>:8080/video`; iOS IP Camera: MJPEG `http://<ip>:8081/video`).
- `forward` = which crossing direction counts as the room's main flow; calibrate on-site (walk in once → occupancy should go **up**; if not, flip `"down"`↔`"up"`).
- `confidence` per-zone (darshan uses **0.4** — close-range doorways; default 0.5).

### Tests
`pytest` — 23 tests: detector filtering, line-crossing, occupancy grace period, occupancy safety, direction mapping.

---

## 5. Node backend (`mandir-crowd-monitor/backend/` == `shared-repo/backend/` == GitHub)

### Files
| File | Responsibility |
|---|---|
| `server.js` | Express app + socket.io. All routes, broadcasts. |
| `src/state.js` | Authoritative darshan occupancy in MongoDB. `applyEvent(enter/exit)` with never-negative rule; `resetState`; `toStatus`. Runs gate hysteresis on each change. |
| `src/gate.js` | `nextGateState(occupancy, current, low, high)` — **two-threshold hysteresis**: CLOSE at ≥ HIGH (capacity), OPEN at ≤ LOW, hold in between (no flapping). OPEN=green, CLOSED=red. |
| `src/models.js` | Mongoose schemas: `State` (single doc `_id:"current"`) and `Event` (enter/exit log). |
| `src/positions.js` | In-memory latest ESP32 position per `mac_address`; `online` staleness (10s); exposes `sos` (mirrors `is_active`). |
| `src/zones.js` | In-memory outside-corridor count. |
| `src/auth.js` | JWT admin auth. Hardcoded users; `login`, `verifyToken`, `requireAuth`, `userFromToken`. |

### Config (`.env`, gitignored — see `.env.example`)
```
PORT=4000
MONGO_URI=mongodb://127.0.0.1:27017/mandir
CAPACITY=30
GATE_HIGH=30
GATE_LOW=10
JWT_SECRET=change-me-in-production
```

### REST API
| Method + path | Body | Purpose |
|---|---|---|
| `GET /api/health` | — | `{ok:true}` |
| `POST /api/auth/login` | `{email,password}` | → `{token, user}` (401 if bad) |
| `GET /api/auth/me` | Bearer token | → `{user}` |
| `GET /api/status` | — | darshan status (below) |
| `POST /api/events` | `{type:"enter"|"exit", camera?}` | apply crossing → occupancy+gate; emits `status`+`zones` |
| `POST /api/reset` | — | zero occupancy; emits |
| `POST /api/position` | `{mac_address, coordinates:{x,y}, is_active}` | ESP32 band position (is_active = SOS); emits `positions` |
| `GET /api/positions` | — | `[{mac_address,x,y,isActive,sos,online,lastSeen}]` |
| `POST /api/zones/outside` | `{count}` | outside live count; emits `zones` |
| `GET /api/zones` | — | `{darshan:{occupancy,capacity,gate,overCapacity}, outside:{count,updatedAt}}` |

**`status` shape:** `{occupancy, capacity, gate, overCapacity, totalEntries, totalExits, thresholds:{low,high}, updatedAt}`

### socket.io events (server → client)
- `status` — darshan status (on connect, and on every event/reset).
- `zones` — `{darshan, outside}` (on connect, and on every event/reset/outside-count).
- `positions` — array of band positions (on every `POST /api/position`).

Data endpoints are **open (no auth)** so the Python engine and ESP32 can POST freely; only the dashboard UI is auth-gated (client-side `ProtectedRoute`).

### Tests
`npm test` (node --test) — 4 gate-hysteresis tests.

---

## 6. React dashboard (`trinetra-frontend/`) — **local only, NOT on GitHub**

### Status: this is a large scaffold; ~38/208 files have real content, rest are placeholders. What's wired to real data vs mock:

**Real (live from backend):**
- **Auth** — `context/AuthContext.jsx` calls `POST /api/auth/login` (JWT), with an offline fallback to hardcoded users. Login: `admin@trinetra.com / admin123` (also `operator@…/operator123`, `security@…/security123`).
- **Socket** — `context/SocketContext.jsx` connects to `VITE_SOCKET_URL`.
- **Dashboard live data** — `hooks/useRealtimeData.js` consumes `status` + `positions`, injects real **occupancy**, **gate**, **SOS** into the dashboard (headline "Visitors Inside" = real occupancy; a band in SOS becomes a top-priority alert). Added a "Darshan Room — Live" panel (occupancy bar, gate badge, bands online, SOS).
- **Live Monitoring** (`pages/LiveMonitoring.jsx`) — the zone grid: each zone shows its **live annotated MJPEG feed** (`<img src={VITE_FEED_URL}/feed/<zone>>`), click → **analytics modal** with a live Recharts trend + current/peak. Uses `hooks/useZones.js` (consumes `zones` socket + `GET /api/zones`, keeps rolling history).

**Mock (for demo richness):** most other widgets — analytics charts, camera grid, other zones, security teams, incidents, emergency. Generated in `useRealtimeData.js` / `data/mock*.js`.

### Config (`.env`)
```
VITE_API_URL=http://localhost:4000/api
VITE_SOCKET_URL=http://localhost:4000
VITE_FEED_URL=http://localhost:8090
```
`vite.config.js` proxies `/api` and `/socket.io` to `:4000`.

### Branding / hero (recent work)
- **Home hero** (`pages/Home.jsx`) redesigned: temple background (`public/hero-temple.png`), "Smarter Temples. / Safer Darshan.", eyebrow, two CTAs, feature row, scroll cue. Nav + hero only; the rest of Home is older content.
- **Logo** = `public/trinetra-logo.png` (full lockup, black bg knocked out to transparent) + `public/trinetra-icon.png` (icon only). Placed in: Home nav (full lockup), sign-in/up nav + emblems, dashboard sidebar/topbar/mobile-nav (icon), favicon. Source art: `public/trinetra-logo.jpeg`.

### Routes
`/` (Home, public), `/signin`, `/signup`; protected: `/dashboard`, `/monitoring`, `/crowd`, `/temple-map`, `/alerts`, `/emergency`, `/security`, `/incidents`, `/analytics`, `/settings`.

---

## 7. ESP32 SOS band (`shared-repo/band/`, dev copy in `~/Downloads/`)

BLE RSSI trilateration + SOS. **Firmware written & logic-verified; NOT yet flashed/tested on hardware.**

- **Tabs:** `ble_trilateration_network.ino`, `config.h`, `ble_scan.cpp/.h`, `trilateration.cpp/.h` (native-compiled & math-tested), `network.cpp/.h`, `sos.cpp/.h`.
- **Flow:** scan 3 anchor phones (matched by BLE name) → RSSI→distance (per-anchor `TX_POWER`, calibrated `-70`) → least-squares trilateration → `(x,y)` → POST to backend.
- **RTOS:** networking runs on its own FreeRTOS task (16KB stack for TLS) so a slow/failed POST never stalls scanning; SOS button+buzzer on another task (core 0). Sensing stays in `loop()`.
- **SOS:** hold button (`SOS_BUTTON_PIN=4`) 5s → **latches** until power-cycle → `is_active=true` in the POST (== the SOS flag the backend/dashboard read as `sos`), POST rate speeds up (2000ms→500ms), active buzzer (`BUZZER_PIN=5`) beeps 200/300ms. During SOS it resends the **last-known** position even if the live fix drops.
- **`config.h` knobs:** `USE_HTTPS` (true=https/Render, false=http/local laptop), `SERVER_URL` (`…/api/position`), WiFi creds, anchor names/positions/TX_POWER, pins, intervals. `mac_address` = the chip's real `WiFi.macAddress()` (unique per board).
- **Board settings (ESP32-S3 N16R8):** Flash 16MB, PSRAM **OPI**, USB CDC On Boot **Enabled**. Avoid GPIO 26–37 (flash/PSRAM), 0/45/46 (strapping), 19/20 (USB), 43/44 (UART). Libs: ArduinoJson + ESP32 BLE Arduino.

---

## 8. How to run the full stack (4 processes)

```bash
# 1. MongoDB
brew services start mongodb-community

# 2. Backend (:4000)
cd ~/Desktop/Trinetra/mandir-crowd-monitor/backend && npm install && npm start

# 3. Python CV engine + MJPEG feeds (:8090)
cd ~/Desktop/Trinetra/mandir-crowd-monitor
source venv/bin/activate
pip install -r requirements.txt      # first time
python main.py

# 4. Frontend (:5173)
cd ~/Desktop/Trinetra/trinetra-frontend && npm install && npm run dev
```
Open `http://localhost:5173/` → **Enter Command Center / Admin Login** → `admin@trinetra.com` / `admin123` → sidebar → **Live Monitoring**.

Quick CV-only check: run just steps 2–3, open `http://localhost:8090/` (all annotated feeds).

Free a busy port: `lsof -ti tcp:<PORT> | xargs kill`.

---

## 9. Current status

| Component | Status |
|---|---|
| CV detection/tracking/line-counting | ✅ Works; tested on `vtest.avi`, `Entrance.mp4`, `Exit.mp4`. Detection strong on close subjects; conf 0.4 tuned. |
| MJPEG zone feeds | ✅ Works; verified rendering in browser. |
| Backend occupancy + gate hysteresis | ✅ Works; tested (30→CLOSE, drain→10→OPEN). |
| Backend positions + `sos` | ✅ Works; tested. |
| Backend zones (outside count) | ✅ Works; tested. |
| Backend admin JWT auth | ✅ Works; tested (login/401/me). **Pushed to GitHub (`98e2a11`).** |
| Frontend login → dashboard live data | ✅ Works; verified in browser. **Not pushed.** |
| Frontend Live Monitoring (feeds + analytics) | ✅ Works; verified. **Not pushed.** |
| Frontend hero redesign + TRINETRA logo | ✅ Done; verified. **Not pushed.** |
| ESP32 firmware | ⚠️ Written, math-verified natively; **NOT flashed/tested on hardware.** Pushed to GitHub `band/`. |
| Fall detection (elderly feature) | ❌ Planned, not built. |
| Temple Map (Leaflet) plotting band positions | ❌ Placeholder; not wired. |

**Pushed to GitHub:** backend (auth/zones/sos) + band firmware.
**NOT pushed:** all frontend changes (hero, logo, live-data wiring) — they exist only in local `trinetra-frontend/`. Python CV engine is also local (not in GitHub).

---

## 10. Known issues / gotchas (learned the hard way)

- **macOS Local Network permission (Sequoia/26+):** Python/terminal apps are blocked from reaching phone cameras on the LAN until granted in *System Settings → Privacy & Security → Local Network* (enable the terminal app, e.g. VS Code). Symptom: browser reaches the phone but Python gets "No route to host".
- **Managed/campus WiFi client isolation:** blocks device-to-device. Use a **phone hotspot** (or home router, not guest mode). Confirm the Mac and phones share a subnet and can `ping` each other.
- **IP Webcam URL is `http` not `https`;** iOS IP Camera gives MJPEG on a port like 8081. Test the URL in a browser first.
- **Vite 8 / rolldown native binding:** if `npm run dev` fails with *"Cannot find native binding"*, do a clean reinstall: `rm -rf node_modules package-lock.json && npm install`.
- **3 YOLO models on CPU** = low FPS; acceptable for demo. Reduce phone resolution to ~640×480.
- **Looped test videos** cause darshan occupancy to drift (the clip's people are re-counted each loop). With real separate entrance/exit cameras it's a true running count.
- **MJPEG stream can't be opened as a page** (never fires load) — always embed via `<img>`.
- **Backend source-of-truth:** edit `mandir-crowd-monitor/backend`, then it's rsync'd into `shared-repo/backend` before pushing. Don't edit `shared-repo/backend` directly or the two diverge.

---

## 11. Suggested next steps

1. **Push the frontend** to GitHub (currently local-only) — decide whether `trinetra-frontend` replaces `shared-repo/src`.
2. **Flash & test the ESP32 band** on real hardware; verify positions land in `/api/positions` and SOS shows on the dashboard.
3. **Wire the Temple Map** (Leaflet) to plot live band positions with SOS markers (data already available via `positions` socket).
4. **Fall detection** (bounding-box aspect-ratio + grace period) → new backend alert → dashboard.
5. **Deploy** for the demo: backend on Render + MongoDB Atlas (ESP32 already supports HTTPS via `setInsecure()`); or keep everything local on one hotspot.
6. **Consolidate the repos** — reconcile the duplicate frontend/backend copies into the single GitHub monorepo.

---

## 12. Credentials & constants quick-reference

- **Admin login:** `admin@trinetra.com` / `admin123`
- **Capacity / gate:** capacity 30, close at 30, reopen at 10
- **Person class:** COCO id 0; darshan confidence 0.4, default 0.5; tracker grace 15 frames
- **ESP32 SOS pins:** button GPIO 4, buzzer GPIO 5; 5s hold; POST 2000ms→500ms in SOS
- **Ports:** Mongo 27017 · backend 4000 · feeds 8090 · Vite 5173
- **GitHub:** `Shubh16Gupta/8Bit-Bite-Trinetra` (main), latest backend commit `98e2a11`
