# Cyberpunk SmallTV Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Custom ESP8266 firmware for the SmallTV Ultra with two switchable cyberpunk clock faces, Open-Meteo weather, and a web admin page.

**Architecture:** PlatformIO project. All pure logic (settings JSON, weather/geocode parsing, formatting, conversions) lives in `logic.{h,cpp}` and is unit-tested natively. Hardware modules (display, net, weather fetch, faces) are thin wrappers over it. Faces redraw only changed regions via small sprites.

**Tech Stack:** PlatformIO, Arduino ESP8266 core, TFT_eSPI, ArduinoJson v7, WiFiManager (tzapu), LittleFS, Unity (native tests).

**Spec:** `docs/superpowers/specs/2026-11-09-cyberpunk-firmware-design.md`

## Global Constraints

- Board `esp12e`, flash layout `eagle.flash.4m1m.ld` (1 MB FS), `board_build.filesystem = littlefs`.
- Display: ST7789 240×240, SCLK 14, MOSI 13, DC 0, RST 2, CS -1, SPI mode 3, 40 MHz, `TFT_INVERSION_ON`, `TFT_RGB_ORDER=TFT_BGR`. Backlight GPIO5 PWM, active-low.
- Defaults: city `San Francisco`, lat `37.7749`, lon `-122.4194`, unit `F`, face `0`, brightness `80`.
- Weather refresh 15 min; retry 60 s on failure; stale marker if last good data > 1 h.
- Wi-Fi connect timeout 20 s, then AP `CYBERCLOCK-SETUP` (open; portal closes after 5 min with no setup client, then the device reboots to retry Wi-Fi).
- 24-hour time; `--:--` before first NTP sync.
- Open-Meteo over plain HTTP only (`api.open-meteo.com`, `geocoding-api.open-meteo.com`).
- `secrets.ini` gitignored; `secrets.ini.example` committed.
- Folder is not a git repo: skip commit steps unless the user initializes git.

## Review Focus

1. Corrupt / partial `/config.json` (power loss mid-write) → device boots with defaults for bad fields, never crash-loops. Test in Task 1.
2. Out-of-range settings from POST (`brightness: 500`, `face: 9`, `unit: "K"`) → clamped / defaulted, not stored raw. Test in Task 1.
3. City search with spaces, accents, `&` (e.g. `São Paulo`, `A&B`) → query URL-encoded; zero results returns empty list, not an error page. Test in Task 1.
4. Long city names (`Llanfairpwllgwyngyll`) → truncated on screen, never overflow into other regions. Test in Task 1 (`truncateUpper`).
5. Weather API down / malformed response → last good values stay, `valid` not flipped to garbage. Test in Task 1.

---

### Task 1: Project scaffold + pure logic with native tests

**Files:**
- Create: `platformio.ini`, `.gitignore`, `secrets.ini.example`, `src/logic.h`, `src/logic.cpp`, `test/test_logic/test_logic.cpp`, `test/fixtures.h` (captured Open-Meteo forecast + geocode JSON as string literals)

**Interfaces:**
- Produces (`src/logic.h`, no Arduino deps — only `<cstdint>`, `<cstddef>`, ArduinoJson):
  - `struct Settings { char city[48]; float lat; float lon; char unit; uint8_t face; uint8_t brightness; };`
  - `Settings defaultSettings();`
  - `bool parseSettings(const char* json, Settings& out);` — starts from defaults, overlays valid fields; clamps brightness 0–100, face 0–1, unit ∈ {C,F} else default; returns false on unparsable JSON (out = defaults).
  - `size_t serializeSettings(const Settings& s, char* buf, size_t len);`
  - `struct WeatherData { float tempC; float hiC; float loC; int humidity; int code; long utcOffset; bool valid; };`
  - `bool parseWeather(const char* json, WeatherData& out);` — leaves `out` untouched on failure.
  - `struct GeoResult { char name[48]; char admin1[32]; char country[4]; float lat; float lon; };`
  - `int parseGeocode(const char* json, GeoResult* out, int max);` — returns count (0 when `results` missing).
  - `int toDisplayTemp(float c, char unit);` — rounds half away from zero.
  - `const char* wmoLabel(int code);` — uppercase short label.
  - `void formatTime(int h, int m, char* out);` (out ≥ 6) / `void formatDate(int wday, int mday, int mon, int year, char* out);` (out ≥ 16, e.g. `MON 09 NOV 2026`; `mon` 0-based, `wday` 0=SUN)
  - `void urlEncode(const char* in, char* out, size_t len);`
  - `void truncateUpper(const char* in, char* out, size_t maxChars);`

- [ ] **Step 1:** Install PlatformIO: `python3 -m pip install --user platformio` (or `pipx install platformio`). Verify `pio --version`.
- [ ] **Step 2:** Write `platformio.ini` with envs `smalltv` (esp12e, lib_deps `bodmer/TFT_eSPI`, `bblanchon/ArduinoJson@^7`, `tzapu/WiFiManager`, TFT_eSPI config via `-D USER_SETUP_LOADED=1 -D ST7789_DRIVER=1 -D TFT_WIDTH=240 -D TFT_HEIGHT=240 -D TFT_MOSI=13 -D TFT_SCLK=14 -D TFT_CS=-1 -D TFT_DC=0 -D TFT_RST=2 -D TFT_SPI_MODE=SPI_MODE3 -D SPI_FREQUENCY=40000000 -D TFT_INVERSION_ON -D TFT_RGB_ORDER=TFT_BGR -D LOAD_GLCD -D LOAD_FONT2 -D LOAD_FONT4 -D LOAD_FONT7 -D SMOOTH_FONT`, `extra_configs = secrets.ini` optional) and `native` (`platform = native`, `build_src_filter = +<logic.cpp>`, `lib_deps = bblanchon/ArduinoJson@^7`, `test_build_src = yes`).
- [ ] **Step 3: Write failing tests** in `test/test_logic/test_logic.cpp`:
  - `test_defaults`: city `"San Francisco"`, unit `'F'`, face 0, brightness 80, lat ≈ 37.7749.
  - `test_parse_invalid_json`: `parseSettings("{bad", s)` → false, s equals defaults.
  - `test_parse_partial`: `{"unit":"C"}` → true, unit `'C'`, city still default.
  - `test_parse_clamps`: `{"brightness":500,"face":9,"unit":"K"}` → brightness 100, face 0, unit `'F'`.
  - `test_settings_roundtrip`: serialize then parse a non-default Settings → equal.
  - `test_temp`: `toDisplayTemp(18.7,'C')==19`, `toDisplayTemp(18.7,'F')==66`, `toDisplayTemp(-0.6,'C')==-1`.
  - `test_wmo`: 0→`"CLEAR"`, 2→`"PARTLY CLOUDY"`, 3→`"OVERCAST"`, 45→`"FOG"`, 61→`"RAIN"`, 71→`"SNOW"`, 95→`"STORM"`, 999→`"UNKNOWN"`.
  - `test_format`: `formatTime(9,5)`→`"09:05"`; `formatDate(1,9,10,2026)`→`"MON 09 NOV 2026"`.
  - `test_parse_weather`: fixture → tempC 18.7, humidity 75, code 2, hiC 21.3, utcOffset -28800, valid true.
  - `test_parse_weather_bad`: preset `out` with tempC 5; `parseWeather("{}", out)` → false, tempC still 5.
  - `test_geocode`: SF fixture → count ≥ 1, `name=="San Francisco"`, country `"US"`; `parseGeocode("{}",…)` → 0.
  - `test_url_encode`: `"São Paulo"`→`"S%C3%A3o%20Paulo"`, `"A&B"`→`"A%26B"`.
  - `test_truncate`: `truncateUpper("Llanfairpwllgwyngyll", out, 10)`→`"LLANFAIRPW"`.
- [ ] **Step 4:** Run `pio test -e native` → FAIL (undefined symbols).
- [ ] **Step 5:** Implement `src/logic.cpp`. WMO groups: 0 CLEAR; 1 MOSTLY CLEAR; 2 PARTLY CLOUDY; 3 OVERCAST; 45,48 FOG; 51–57 DRIZZLE; 61–67, 80–82 RAIN; 71–77, 85–86 SNOW; 95–99 STORM; else UNKNOWN. F = C × 9/5 + 32.
- [ ] **Step 6:** Run `pio test -e native` → all PASS.

### Task 2: Display driver, backlight, status screens

**Files:** Create `src/display.h`, `src/display.cpp`, temporary `src/main.cpp` (calls `displayBegin()` + `showSplash()`).

**Interfaces:**
- Produces: `extern TFT_eSPI tft;` `void displayBegin();` `void setBrightness(uint8_t pct);` (analogWrite GPIO5, inverted: duty = 1023 − pct×1023/100) `void showSplash();` `void showMessage(const char* line1, const char* line2);` (centered neon text, used for AP mode / IP screen).
- Palette constants in `display.h`: `C_BG_A 0x0001`-ish near-black, `C_CYAN`, `C_MAGENTA`, `C_YELLOW`, `C_AMBER`, `C_GREEN`, `C_DIMGREEN` as RGB565.

- [ ] **Step 1:** Implement as above; `tft.init(); tft.setRotation(0); tft.fillScreen(BG)`.
- [ ] **Step 2:** `pio run -e smalltv` → SUCCESS. Record RAM/flash usage.

### Task 3: Config persistence

**Files:** Create `src/config.h`, `src/config.cpp`.

**Interfaces:**
- Consumes: `Settings`, `parseSettings`, `serializeSettings` (Task 1).
- Produces: `extern Settings settings;` `void configLoad();` (mount LittleFS, `LittleFS.format()` + remount on failure, read `/config.json`, parse; defaults if missing) `bool configSave();` (write `/config.tmp`, then rename to `/config.json` to survive power loss).

- [ ] **Step 1:** Implement. **Step 2:** `pio run -e smalltv` → SUCCESS.

### Task 4: Weather + time

**Files:** Create `src/weather.h`, `src/weather.cpp`.

**Interfaces:**
- Consumes: `settings`, `parseWeather`, `WeatherData`.
- Produces: `extern WeatherData weather;` `extern uint32_t weatherFetchedAt;` (millis of last good fetch, 0 = never) `void weatherRequestRefresh();` `void weatherLoop();` (schedules fetch: 15 min after success, 60 s after failure; on success calls `configTime(weather.utcOffset, 0, "pool.ntp.org", "time.google.com")` when offset changed) `bool weatherStale();` (> 1 h since last good) `bool timeSynced();` (`time(nullptr) > 1700000000`).
- Fetch via `WiFiClient` + `HTTPClient`, stream into `JsonDocument` with a filter for only the needed fields (keeps heap small).

- [ ] **Step 1:** Implement. **Step 2:** `pio run -e smalltv` → SUCCESS.

### Task 5: Network, admin API, OTA

**Files:** Create `src/net.h`, `src/net.cpp`, `src/admin_page.h`.

**Interfaces:**
- Consumes: `settings`, `configSave`, `setBrightness`, `showMessage`, `weatherRequestRefresh`, `weather`, `urlEncode`, `parseGeocode`, `parseSettings`.
- Produces: `void netBegin();` (WiFi.mode STA; `WiFi.begin()` with SDK creds, else `WIFI_SSID`/`WIFI_PASS` build flags if defined; wait 20 s; else WiFiManager `setConfigPortalTimeout(0)`, `setAPCallback` → `showMessage("CYBERCLOCK-SETUP","192.168.4.1")`, `autoConnect("CYBERCLOCK-SETUP")`; then show IP 3 s, start server + OTA) `void netLoop();` `extern volatile bool faceDirty;` (set when face/unit/city change so main re-inits face).
- Routes: `GET /` → `admin_page.h` PROGMEM HTML; `GET /api/config` → serialized settings; `POST /api/config` (JSON body) → `parseSettings` over current settings merged, save, apply brightness, set `faceDirty`, `weatherRequestRefresh()` if lat/lon changed; `GET /api/geocode?q=` → device fetches `http://geocoding-api.open-meteo.com/v1/search?name=<urlEncode(q)>&count=5`, returns `[{name,admin1,country,lat,lon}]` (`[]` on no results/failure); `GET /api/status` → `{ip,rssi,uptime,heap,weather:{tempC,code,age}}`; `/update` → `ESP8266HTTPUpdateServer`.
- For POST merge: serialize current settings to a doc, overlay request fields, then `parseSettings` the merged JSON (reuses clamping).
- Admin page: dark bg, cyan/magenta, monospace; city search with result list, °C/°F toggle, face A/B toggle, brightness slider (debounced POST), status panel, link to `/update` with note about reverting to stock.

- [ ] **Step 1:** Implement. **Step 2:** `pio run -e smalltv` → SUCCESS.

### Task 6: Faces

**Files:** Create `src/faces/face.h`, `src/faces/netrunner.cpp`, `src/faces/terminal.cpp`, `src/faces/common.cpp` (shared glitch + state helpers).

**Interfaces:**
- Produces in `face.h`:
  - `struct FaceState { char time[6]; char date[16]; char city[11]; int temp; int hi; int lo; int humidity; char unit; const char* label; bool weatherValid; bool stale; int second; };`
  - `void buildFaceState(FaceState& s);` (from `time()`, `weather`, `settings`; `--:--` if `!timeSynced()`)
  - `class Face { public: virtual void begin() = 0; virtual void update(const FaceState& s, const FaceState& prev) = 0; virtual void tick(uint32_t ms) = 0; virtual ~Face() {} };`
  - `Face* faceNetrunner(); Face* faceTerminal();` (static instances)
- Time digits: Font 7 (7-segment) in a `TFT_eSprite` with `setColorDepth(8)`, ~216×56; Netrunner draws magenta copy at +2px x then cyan on top. Glitch every random 4–8 s for 200 ms: push sprite in 4 horizontal slices with random x offsets ±6 px, then a clean push.
- Netrunner animation: grid horizontal lines scroll toward viewer (~20 fps), redrawn only in y 186–240 band. Terminal animation: seconds bar width = second/59 × 152, plus blinking cursor after `> CHRONO.SYNC OK`.
- Layout coordinates follow the approved mockups (spec "Face A/B").
- Stale weather: append `*` after temperature, label shows `NO SIGNAL` when `!weatherValid`.

- [ ] **Step 1:** Implement. **Step 2:** `pio run -e smalltv` → SUCCESS.

### Task 7: Main wiring

**Files:** Replace `src/main.cpp`.

**Interfaces:** Consumes everything above.

- [ ] **Step 1:** `setup()`: `displayBegin(); configLoad(); setBrightness(settings.brightness); showSplash(); netBegin(); weatherRequestRefresh(); current face begin()`. `loop()`: `netLoop(); weatherLoop();` if `faceDirty` → pick face by `settings.face`, `begin()`, force full update; every 250 ms build state and `update()` when changed; `tick(millis())` at ~20 fps; `yield()`.
- [ ] **Step 2:** `pio test -e native` → PASS; `pio run -e smalltv` → SUCCESS; report RAM % (must be < 75%) and firmware size (must be < 1 MB).
- [ ] **Step 3:** Request code review of the whole project before flashing (boot path must not crash before OTA is up).

### Task 8: Flash and verify on device (requires user OK)

- [ ] **Step 1:** Ask user to approve flashing. Then `curl -F "firmware=@.pio/build/smalltv/firmware.bin" http://192.168.0.21/update` — first fetch `/update` HTML to confirm the stock form's field name and action URL and use those.
- [ ] **Step 2:** Wait ~30 s; find device IP (same DHCP IP likely `192.168.0.21`; else ask user to read it from screen). If the screen shows `CYBERCLOCK-SETUP`, ask user to join it and pick Wi-Fi.
- [ ] **Step 3:** `curl http://<ip>/api/status` → JSON with `heap` and weather age; `curl http://<ip>/api/geocode?q=Tokyo` → non-empty list; `POST /api/config {"unit":"C"}` then `GET /api/config` → unit `C`; POST `{"face":1}`; restore `{"unit":"F","face":0}`.
- [ ] **Step 4:** User confirms both faces visually.
