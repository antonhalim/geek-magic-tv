# Cyberpunk firmware for GeekMagic SmallTV Ultra — design

Date: 2026-11-09
Status: approved in chat, pending spec review

## Goal

Replace the stock GeekMagic firmware (Ultra-V9.0.43) on the SmallTV Ultra at
`192.168.0.21` with custom firmware that shows a cyberpunk clock face with live
time, date and weather. Two faces, switchable. Settings editable from a web
admin page. Reverting to stock must stay possible without a USB cable.

## Non-goals

- Home Assistant / ESPHome integration, custom data feeds, system HUD stats.
- Photo album / GIF playback (stock features that are dropped).
- Restyling the stock web settings page.

## Hardware (confirmed)

- MCU: ESP8266 (ESP-12), 4 MB flash, ~80 KB RAM.
- Display: ST7789 240×240, SPI, no CS pin.
  - SCLK GPIO14, MOSI GPIO13, DC GPIO0, RST GPIO2.
  - SPI mode 3, 40 MHz, colors inverted, BGR order.
- Backlight: GPIO5, PWM, active-low (inverted).
- Source: devices.esphome.io/devices/geekmagic-ultra, HA community thread 618029.

## Stack

- PlatformIO, Arduino ESP8266 core, board `esp12e`, 4 MB flash, default `eagle.flash.4m1m.ld` layout (1 MB FS).
- TFT_eSPI (ST7789 driver, configured via build flags, no `User_Setup.h` edits).
- ArduinoJson v7, WiFiManager (tzapu), ESP8266WebServer,
  ESP8266HTTPUpdateServer, LittleFS.

ESPHome rejected: needs a forked ST7789 driver, flickers without a frame
buffer, and custom animation in YAML lambdas is awkward.

## Behavior

### Boot and Wi-Fi

1. Mount LittleFS; format if mount fails (stock FS contents are discarded).
2. Load `/config.json`; fall back to defaults on missing/invalid file.
3. Show a boot splash ("BOOTING // CYBERCLOCK").
4. Connect Wi-Fi: SDK-stored credentials first, then optional compile-time
   credentials from a gitignored `secrets.ini`.
5. If not connected within 20 s, WiFiManager opens AP `CYBERCLOCK-SETUP`
   (no password) with a captive portal. The screen shows the AP name and
   `192.168.4.1`. The portal closes after 5 min with no setup client and the
   device reboots to retry Wi-Fi.
6. Once connected: show IP on screen for 3 s, start web server, NTP, weather.

### Time

- `configTime(offset, 0, "pool.ntp.org", "time.google.com")`.
- Offset = Open-Meteo `utc_offset_seconds` (`timezone=auto`), re-applied after
  every weather fetch, so DST and city timezone follow automatically.
- 24-hour format. Before first NTP sync the time shows `--:--`.

### Weather

- `http://api.open-meteo.com/v1/forecast?latitude=..&longitude=..
  &current=temperature_2m,relative_humidity_2m,weather_code
  &daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1`
- Every 15 min; on failure retry after 60 s. Last good data stays on screen;
  a stale marker shows if data is older than 1 h.
- Values fetched in °C; converted for display when unit is °F.
- WMO weather code → short uppercase label (CLEAR, CLOUDY, FOG, RAIN, …).

### Settings (`/config.json`)

| Key | Default | Notes |
|---|---|---|
| `city` | `San Francisco` | display name |
| `lat`, `lon` | `37.7749`, `-122.4194` | |
| `unit` | `F` | `C` or `F` |
| `face` | `0` | `0` = Netrunner, `1` = Terminal |
| `brightness` | `80` | 0–100 % |

### Admin web page (`http://<ip>/`)

- Single page, served from PROGMEM, styled to match (dark, neon, monospace).
- City search box → device calls
  `http://geocoding-api.open-meteo.com/v1/search?name=..&count=5` and returns
  matches; user picks one → saves name/lat/lon and triggers a weather refresh.
- Unit toggle, face toggle, brightness slider. Changes apply immediately.
- Endpoints: `GET /` page, `GET /api/config`, `POST /api/config`,
  `GET /api/geocode?q=`, `GET /api/status` (ip, rssi, uptime, heap, last weather).
- `/update`: ESP8266HTTPUpdateServer firmware upload. Used for future updates
  and for reverting to stock (upload GeekMagic's official Ultra `.bin`).
- No auth (LAN-only device; same as stock).

## Rendering

RAM forbids a full 240×240×16-bit frame buffer (115 KB). Approach:

- Static background (grid, frames, labels) drawn once per face switch.
- Dynamic regions rendered into small `TFT_eSprite`s and pushed:
  - time digits (~216×70, 8-bit color, ~15 KB),
  - date line, weather block (redrawn only when values change),
  - animation strip (scanline / seconds bar / grid pulse).
- Animation tick ~20 fps for animated regions only.
- Glitch: every 4–8 s for ~200 ms the time sprite is pushed in horizontally
  offset slices with a chromatic (magenta/cyan) ghost.
- Faces implement one interface: `begin()` (draw static), `update(state)`
  (redraw changed regions), `tick(ms)` (animations).

### Face A — Netrunner
Near-black bg, cyan time with magenta ghost offset, yellow temperature,
magenta perspective grid at the bottom, header `SYS//ONLINE` + city code.

### Face B — Terminal
Green-black bg with dim grid, amber corner brackets, amber time, seconds as a
green progress bar, `> CHRONO.SYNC OK` / `> ATMOS.<CITY>` terminal labels.

## Code layout

```
platformio.ini
secrets.ini.example        (secrets.ini gitignored)
src/
  main.cpp                 setup/loop wiring
  config.{h,cpp}           Settings struct, JSON load/save
  display.{h,cpp}          TFT init, backlight PWM, splash/status screens
  net.{h,cpp}              Wi-Fi, WiFiManager, web server, OTA
  admin_page.h             PROGMEM HTML
  weather.{h,cpp}          Open-Meteo fetch + parse
  logic.{h,cpp}            pure helpers: C→F, WMO labels, time format, parsing
  faces/face.h             interface
  faces/netrunner.cpp
  faces/terminal.cpp
test/test_logic/           native unit tests for logic + config parsing
```

## Testing

- `pio test -e native`: logic.cpp and JSON parsing (settings defaults,
  invalid JSON, unit conversion and rounding, WMO labels, time formatting,
  Open-Meteo response parsing from a captured sample).
- `pio run -e smalltv`: firmware builds; report flash/RAM usage.
- On device: `curl` `/api/status`, `/api/config`, POST config changes,
  geocode search. Visual check of both faces by the user.

## Flashing and rollback

1. First flash: upload `.pio/build/smalltv/firmware.bin` via stock
   `http://192.168.0.21/update` (Firmware field). Requires user approval.
2. If Wi-Fi fails: join `CYBERCLOCK-SETUP` from a phone, pick network.
3. Rollback: upload GeekMagic's official Ultra firmware via `/update`.
4. Last resort (bricked boot): USB-serial adapter, GPIO0 to GND at power-up.

## Risks

- Stock `/update` may reject a non-GeekMagic image (unknown checks beyond the
  ESP magic byte). Mitigation: abort and report; no partial state on failure.
- Our firmware dies before Wi-Fi/OTA come up → needs serial. Mitigation: keep
  boot path minimal, run native tests, review before flashing.
