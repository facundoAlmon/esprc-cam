# CLAUDE.md — esprc-cam

## Project Overview

ESP32-CAM firmware for FPV video streaming integrated with **esprc-brain**. Targets the AI-Thinker ESP32-CAM module (OV2640 sensor, 4 MB flash, 4 MB PSRAM).

- **Firmware** (`Firmware/`) — C++ on pure ESP-IDF 6.0, compiled with `idf.py`
- **WebApp** (`Firmware/webapp/`) — HTML/CSS/JS, bundled by Gulp into a single `index.html`

## Stack & Managed Dependencies

| Dependency | Version | Source |
|------------|---------|--------|
| ESP-IDF | v6.0.1 | `/mnt/EVO_EXT4/DIY/esp-idf/.espressif/v6.0.1/esp-idf/` |
| espressif/esp32-camera | ^2.0.0 | component manager |
| espressif/mdns | ^1.0.0 | component manager |
| bblanchon/arduinojson | ^7.4.2 | component manager |

## Firmware Commands

```bash
# Activate IDF (run in every new terminal)
export IDF_PATH=/mnt/EVO_EXT4/DIY/esp-idf/.espressif/v6.0.1/esp-idf
export IDF_PYTHON_ENV_PATH=/home/falmon/.espressif/python_env/idf6.0_py3.12_env
export ESP_IDF_VERSION=6.0.1
export PATH="$IDF_PATH/tools:/home/falmon/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:$IDF_PYTHON_ENV_PATH/bin:$PATH"

cd Firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## WebApp Commands

```bash
cd Firmware/webapp
npm install
npm run build    # → copies index.html to Firmware/main/
```

## Architecture

### Firmware Modules

| File | Responsibility |
|------|---------------|
| `main.c` | Entry point, calls `app_task_start()` |
| `src/main.cpp` | WiFi init (AP/STA), NVS prefs, camera init (5-retry), webserver start |
| `src/camera_driver.cpp` | OV2640 init (PWDN cycle + PSRAM), pause/resume guard, `camera_apply_settings()` |
| `src/led_status.cpp` | Onboard LED (GPIO 33) state machine — BOOT/RETRY/READY/STREAMING patterns |
| `src/streamer.cpp` | WebSocket binary JPEG streaming task (FreeRTOS, pinned to core 1) |
| `src/webserver.cpp` | HTTP server: MJPEG, WS, config, OTA, scan-brain endpoints |
| `src/nvs_prefs.cpp` | NVS wrapper (identical to brain) |
| `src/dns_server.c` | Captive portal DNS for AP mode |

### Streaming

Two parallel stream mechanisms:
- **`GET /mjpeg`** — MJPEG multipart HTTP stream (port 81, raw TCP task pinned core 0, priority 5). httpd at port 80 `/mjpeg` just 302-redirects there. Used by brain webapp `<img>` element.
- **`WS /ws`** — WebSocket binary JPEG. Used by brain's `connectCamWebSocket()`. Frames sent from dedicated FreeRTOS task via `httpd_ws_send_frame_async`.

Both expose the same OV2640 hardware JPEG frames. `CAMERA_GRAB_WHEN_EMPTY + fb_count=1` (single DRAM buffer). `CAMERA_GRAB_LATEST` is NOT used — see constraints below.

### FreeRTOS Task Map

| Task | Core | Priority | Notes |
|------|------|----------|-------|
| `mjpeg_srv` | 0 | 5 | raw TCP MJPEG loop |
| `httpd` | any | 5 | API + WS handler |
| `streamer` | 1 | 6 | WS frame sender |
| `main_task` | any | 5 | sleeps after init |
| `led_status` | any | 1 | GPIO 33 blink driver, 50 ms tick |

### Camera Reliability Mechanisms

**Initialization order** — camera init runs BEFORE NVS and WiFi:
1. NVS allocs from DMA-capable DRAM; if done first, less free DRAM causes `frame_copy_cnt=2` instead of 3, shrinking the PSRAM buffer → no SOI on every frame.
2. WiFi ISRs (level 3) preempt the VSYNC ISR (level 1) during init → DMA misses SOI bytes.

Fix: init camera with hardcoded defaults (QVGA/12), drain 5 warmup frames, then load NVS + reinit camera with saved framesize/quality.

**PWDN power-cycle** — `camera_init()` toggles GPIO 32 HIGH (10 ms) → LOW (300 ms) before `esp_camera_init()`. The AI-Thinker module sometimes holds the OV2640 in an undefined state after a soft reset; the cycle puts it in a known state before SCCB probing.

**5-retry init loop** — `main_task` retries `camera_init()` up to 5 times with 1 s delay between attempts. On total failure calls `esp_restart()` (not an infinite loop) to reset all hardware state cleanly.

**DMA stuck recovery** — both `mjpeg_server` and `streamer` count consecutive NULL `fb_get()` returns. After 3 failures (~6 s) they call `camera_pause()` + `camera_reinit()` + `camera_resume()`. The webserver's `post_config_handler` does the same when framesize/quality change.

**`camera_pause()` / `camera_resume()`** — volatile bool that streaming tasks poll before calling `fb_get()`. After setting `camera_pause = true`, caller waits 200 ms (enough for any in-flight `fb_get()` in the normal/non-stuck case to return) before `camera_reinit()`.

### Camera Frame Buffer Constraints

- `fb_count=1`, `CAMERA_GRAB_WHEN_EMPTY` — intentional. `fb_count=2` triggers a pipelining bug in esp32-camera v2.x on ESP32 (VSYNC semaphore becomes inconsistent). Do NOT change.
- Single buffer means `mjpeg_task` and `streamer` compete for the same buffer. Fix: streamer checks `mjpeg_server_has_client()` and backs off (50 ms sleep) while MJPEG has an active client.
- Raw TCP sockets (mjpeg_server) need `SO_SNDTIMEO` set explicitly — httpd's `send_wait_timeout` config does NOT apply to them. Without it, a stalled client blocks `send()` indefinitely → FPS drops to 0.
- `CONFIG_LWIP_MAX_SOCKETS=16` — httpd (1+7) + mjpeg_srv (1+1) = ~10 at peak; 16 gives headroom to avoid `ENFILE` under burst connections.

### Camera Config Keys

JSON keys match brain webapp's camera form IDs (`camQuality`, `camBright`, etc.):

| JSON key | Sensor call |
|----------|------------|
| `framesize` | `set_framesize` |
| `camQuality` | `set_quality` |
| `camBright` | `set_brightness` |
| `camContrast` | `set_contrast` |
| `camSaturation` | `set_saturation` |
| `camAELevel` | `set_ae_level` |
| `camExpo` | `set_aec_value` |
| `camAGCGain` | `set_agc_gain` |
| `camGainCeiling` | `set_gainceiling` |
| `camAwb` … `camColorBar` | boolean sensor flags |

### NVS Namespace

`"esprc-cam"` — signed sensor values stored offset +128 (so -2 → 126, 0 → 128, +2 → 130).

### mDNS

Camera registers:
- Hostname: `esprc-cam` → accessible as `esprc-cam.local`
- Service: `_esprc-cam._tcp` port 80 (queried by brain for autodiscovery)
- Service: `_http._tcp` port 80

Brain queries `mdns_query_a("esprc-cam", 3000, &addr)` every 30 seconds.

### WiFi

AP mode default SSID: `ESPRC-CAM` (no password). Captive portal DNS active in AP mode.
STA fallback: returns to AP mode after 10 failed connection attempts.

### Partition Table (4 MB flash)

- nvs: 0x9000 (24 KB)
- ota_0: 0x20000 (1.9375 MB)
- ota_1: 0x210000 (1.9375 MB)

### Camera Pins (AI-Thinker ESP32-CAM)

PWDN=32, XCLK=0, SIOD=26, SIOC=27, D7=35, D6=34, D5=39, D4=36, D3=21, D2=19, D1=18, D0=5, VSYNC=25, HREF=23, PCLK=22

### LED Status (GPIO 33)

Onboard red LED (active LOW). Driven by `led_status` task (priority 1, 50 ms tick).

| State | Pattern | Meaning |
|-------|---------|---------|
| `LED_BOOT` | 1 Hz slow blink (500 ms on/off) | ESP booting, camera not yet initialized |
| `LED_RETRY` | 5 Hz fast blink (100 ms on/off) | Camera init retry or DMA reinit in progress |
| `LED_READY` | Solid ON | Camera OK, no active stream client |
| `LED_STREAMING` | 2 Hz medium blink (250 ms on/off) | Client connected, streaming frames |

State transitions: `main.cpp` drives BOOT→RETRY→READY; `mjpeg_server` and `streamer` drive READY↔STREAMING and STREAMING→RETRY→STREAMING on DMA recovery; `webserver` drives RETRY→READY/STREAMING on config-triggered reinit.

## REST API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Embedded webapp |
| GET | `/mjpeg` | MJPEG stream |
| WS | `/ws` | WebSocket JPEG stream |
| GET/POST | `/api/config` | Camera + sensor config |
| GET/POST | `/cam-config` | Alias for brain compat |
| GET/POST | `/api/wifi` | WiFi settings |
| GET | `/api/stats` | `{fps, bps}` |
| GET | `/api/scan-brain` | mDNS lookup for brain, returns WiFi creds |
| POST | `/manage` | `{restartESP, clearPreferences}` |
| GET | `/api/ota/info` | Firmware info |
| POST | `/api/ota` | OTA upload |

