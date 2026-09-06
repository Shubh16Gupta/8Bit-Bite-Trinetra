#ifndef CONFIG_H
#define CONFIG_H

// ---------- BLE anchors ----------
// Matched by BLE advertised device name (not MAC -- avoids Android MAC randomization issues)
static const char* ANCHOR_NAME[3] = {"Redmi", "Realme", "Shubh"};

// Measured position of each phone in meters, relative to your chosen origin
static const float ANCHOR_X[3] = {0.0, 0.0, 3.1};
static const float ANCHOR_Y[3] = {0.0, 3.3, 3.3};

// ---------- RSSI/distance calibration ----------
// Per-anchor 1-meter TX_POWER calibration (measured individually per phone -- phones vary)
static const float TX_POWER[3] = {-70.0, -70.0, -70.0};  // [Redmi, Adarsh, Shubh]

static const float PATH_LOSS_EXPONENT = 2.3;  // 2.0 free space, 2.5-4.0 indoor typical

static const int SCAN_TIME_SEC = 1;               // BLE scan window per loop
static const int SMOOTHING_SAMPLES = 5;             // moving-average window per anchor

// ---------- WiFi / backend ----------
static const char* WIFI_SSID = "AndroidAP_Lineage";
static const char* WIFI_PASSWORD = "12345678";

// Set USE_HTTPS to match SERVER_URL's scheme:
//   true  -> https:// (e.g. the deployed Render backend)
//   false -> http://  (e.g. the backend on your laptop: http://<mac-ip>:4000/api/position)
static const bool USE_HTTPS = false;
static const char* SERVER_URL = "http://10.117.84.1:4000/api/position";//"https://your-app-name.onrender.com/api/position";

// The band's identifier is the chip's own WiFi MAC (unique per board), read at
// runtime in connectWiFi() -- no need to hardcode a per-device value.

static const unsigned long SEND_INTERVAL_MS = 2000;       // normal POST interval
static const unsigned long SEND_INTERVAL_SOS_MS = 500;    // faster POST interval while in SOS
static const unsigned long FIX_FRESH_MS = 4000;           // outside SOS, only send a fix newer than this

// ---------- SOS button + buzzer ----------
// Push button wired between the pin and GND (internal pull-up -> pressed reads LOW).
static const int SOS_BUTTON_PIN = 4;
// Active buzzer: buzzes while the pin is HIGH.
static const int BUZZER_PIN = 5;

static const unsigned long SOS_HOLD_MS = 5000;   // hold the button this long to trigger SOS
static const unsigned long BUZZER_ON_MS = 200;   // beep on time
static const unsigned long BUZZER_OFF_MS = 300;  // beep off time

#endif
