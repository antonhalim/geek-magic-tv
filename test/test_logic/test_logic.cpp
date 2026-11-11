#include <unity.h>
#include <cstring>
#include "logic.h"
#include "../fixtures.h"

void setUp() {}
void tearDown() {}

void test_defaults() {
  Settings s = defaultSettings();
  TEST_ASSERT_EQUAL_STRING("San Francisco", s.city);
  TEST_ASSERT_EQUAL_CHAR('F', s.unit);
  TEST_ASSERT_EQUAL_UINT8(0, s.face);
  TEST_ASSERT_EQUAL_UINT8(80, s.brightness);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 37.7749f, s.lat);
}

void test_parse_invalid_json() {
  Settings s = defaultSettings();
  s.brightness = 10;
  TEST_ASSERT_FALSE(parseSettings("{bad", s));
  Settings d = defaultSettings();
  TEST_ASSERT_EQUAL_MEMORY(&d, &s, sizeof(Settings));
}

void test_parse_partial() {
  Settings s;
  TEST_ASSERT_TRUE(parseSettings("{\"unit\":\"C\"}", s));
  TEST_ASSERT_EQUAL_CHAR('C', s.unit);
  TEST_ASSERT_EQUAL_STRING("San Francisco", s.city);
}

void test_parse_clamps() {
  Settings s;
  TEST_ASSERT_TRUE(parseSettings("{\"brightness\":500,\"face\":9,\"unit\":\"K\"}", s));
  TEST_ASSERT_EQUAL_UINT8(100, s.brightness);
  TEST_ASSERT_EQUAL_UINT8(0, s.face);
  TEST_ASSERT_EQUAL_CHAR('F', s.unit);
}

void test_settings_roundtrip() {
  Settings a = defaultSettings();
  strcpy(a.city, "Tokyo");
  a.lat = 35.6762f;
  a.lon = 139.6503f;
  a.unit = 'C';
  a.face = 1;
  a.brightness = 33;
  char buf[256];
  TEST_ASSERT_TRUE(serializeSettings(a, buf, sizeof(buf)) > 0);
  Settings b;
  TEST_ASSERT_TRUE(parseSettings(buf, b));
  TEST_ASSERT_EQUAL_STRING("Tokyo", b.city);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, a.lat, b.lat);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, a.lon, b.lon);
  TEST_ASSERT_EQUAL_CHAR('C', b.unit);
  TEST_ASSERT_EQUAL_UINT8(1, b.face);
  TEST_ASSERT_EQUAL_UINT8(33, b.brightness);
}

void test_temp() {
  TEST_ASSERT_EQUAL_INT(19, toDisplayTemp(18.7f, 'C'));
  TEST_ASSERT_EQUAL_INT(66, toDisplayTemp(18.7f, 'F'));
  TEST_ASSERT_EQUAL_INT(-1, toDisplayTemp(-0.6f, 'C'));
}

void test_wmo() {
  TEST_ASSERT_EQUAL_STRING("CLEAR", wmoLabel(0));
  TEST_ASSERT_EQUAL_STRING("PARTLY CLOUDY", wmoLabel(2));
  TEST_ASSERT_EQUAL_STRING("OVERCAST", wmoLabel(3));
  TEST_ASSERT_EQUAL_STRING("FOG", wmoLabel(45));
  TEST_ASSERT_EQUAL_STRING("RAIN", wmoLabel(61));
  TEST_ASSERT_EQUAL_STRING("SNOW", wmoLabel(71));
  TEST_ASSERT_EQUAL_STRING("STORM", wmoLabel(95));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", wmoLabel(999));
}

void test_format() {
  char out[16];
  formatTime(9, 5, out);
  TEST_ASSERT_EQUAL_STRING("09:05", out);
  formatDate(1, 9, 10, 2026, out);
  TEST_ASSERT_EQUAL_STRING("MON 09 NOV 2026", out);
}

void test_parse_weather() {
  WeatherData w = {};
  TEST_ASSERT_TRUE(parseWeather(FORECAST_JSON, w));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.7f, w.tempC);
  TEST_ASSERT_EQUAL_INT(75, w.humidity);
  TEST_ASSERT_EQUAL_INT(2, w.code);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.3f, w.hiC);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.8f, w.loC);
  TEST_ASSERT_EQUAL_INT(-28800, w.utcOffset);
  TEST_ASSERT_TRUE(w.valid);
}


void test_parse_weather_day() {
  WeatherData w = {};
  TEST_ASSERT_TRUE(parseWeather(FORECAST_DAY_JSON, w));
  TEST_ASSERT_EQUAL_INT(61, w.dayCode);
  TEST_ASSERT_EQUAL_INT(58, w.precipProb);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.3f, w.precipMm);
  TEST_ASSERT_TRUE(parseWeather(FORECAST_JSON, w));  // older response without daily extras
  TEST_ASSERT_EQUAL_INT(2, w.dayCode);               // falls back to current code
  TEST_ASSERT_EQUAL_INT(-1, w.precipProb);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, w.precipMm);
}

void test_face_vitals() {
  Settings s;
  TEST_ASSERT_TRUE(parseSettings("{\"face\":2}", s));
  TEST_ASSERT_EQUAL(2, s.face);
  TEST_ASSERT_TRUE(parseSettings("{\"face\":3}", s));
  TEST_ASSERT_EQUAL(3, s.face);  // corpo
  TEST_ASSERT_TRUE(parseSettings("{\"face\":4}", s));
  TEST_ASSERT_EQUAL(4, s.face);  // radar
  TEST_ASSERT_TRUE(parseSettings("{\"face\":5}", s));
  TEST_ASSERT_EQUAL(5, s.face);  // chongqing
  TEST_ASSERT_TRUE(parseSettings("{\"face\":6}", s));
  TEST_ASSERT_EQUAL(6, s.face);  // hong kong
  TEST_ASSERT_TRUE(parseSettings("{\"face\":7}", s));
  TEST_ASSERT_EQUAL(0, s.face);
}

void test_day_outlook() {
  TEST_ASSERT_EQUAL_STRING("STORM", dayOutlook(95, 10, 0));
  TEST_ASSERT_EQUAL_STRING("SNOW", dayOutlook(71, 40, 2));
  TEST_ASSERT_EQUAL_STRING("RAIN", dayOutlook(61, 58, 7.3f));
  TEST_ASSERT_EQUAL_STRING("RAIN", dayOutlook(51, 30, 0.2f));  // drizzle
  TEST_ASSERT_EQUAL_STRING("RAIN", dayOutlook(3, 70, 0));      // likely rain
  TEST_ASSERT_EQUAL_STRING("RAIN", dayOutlook(3, 20, 1.5f));   // measurable rain
  TEST_ASSERT_EQUAL_STRING("DRY", dayOutlook(2, 10, 0));
  TEST_ASSERT_EQUAL_STRING("DRY", dayOutlook(0, -1, 0));
}

void test_format_precip() {
  char out[32];
  formatPrecip(58, 7.3f, 'C', out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("58% TODAY // 7.3mm", out);
  formatPrecip(58, 7.3f, 'F', out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("58% TODAY // 0.29in", out);
  formatPrecip(-1, 0, 'F', out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("-- TODAY", out);
}


void test_wind() {
  WeatherData w = {};
  TEST_ASSERT_TRUE(parseWeather(FORECAST_DAY_JSON, w));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.7f, w.windKmh);
  TEST_ASSERT_EQUAL_INT(146, w.windDir);
  TEST_ASSERT_TRUE(parseWeather(FORECAST_JSON, w));  // no wind fields
  TEST_ASSERT_EQUAL_INT(-1, w.windDir);
  TEST_ASSERT_EQUAL_STRING("N", windCompass(0));
  TEST_ASSERT_EQUAL_STRING("N", windCompass(359));
  TEST_ASSERT_EQUAL_STRING("SE", windCompass(146));
  TEST_ASSERT_EQUAL_STRING("SW", windCompass(225));
  TEST_ASSERT_EQUAL_STRING("W", windCompass(270));
  char out[24];
  formatWind(13.7f, 146, 'F', out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("WIND SE 9mph", out);
  formatWind(13.7f, 146, 'C', out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("WIND SE 14km/h", out);
  formatWind(0, -1, 'F', out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("WIND --", out);
}


void test_military_time() {
  TEST_ASSERT_EQUAL('Z', militaryZone(0));
  TEST_ASSERT_EQUAL('U', militaryZone(-8 * 3600));
  TEST_ASSERT_EQUAL('A', militaryZone(3600));
  TEST_ASSERT_EQUAL('I', militaryZone(9 * 3600));
  TEST_ASSERT_EQUAL('K', militaryZone(10 * 3600));  // J is skipped
  TEST_ASSERT_EQUAL('M', militaryZone(12 * 3600));
  TEST_ASSERT_EQUAL('N', militaryZone(-3600));
  TEST_ASSERT_EQUAL('Y', militaryZone(-12 * 3600));
  TEST_ASSERT_EQUAL('J', militaryZone(19800));      // +5:30: not a whole hour, J = local
  char out[24];
  formatUtcOffset(-8 * 3600, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("UTC-8", out);
  formatUtcOffset(19800, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("UTC+5:30", out);
  formatUtcOffset(0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("UTC", out);
  formatDtg(10, 14, 18, 'U', 10, 2026, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("DTG 101418U NOV 26", out);
}


void test_cn_weather() {
  TEST_ASSERT_EQUAL(CNW_SUNNY, cnWeather(0));
  TEST_ASSERT_EQUAL(CNW_SUNNY, cnWeather(1));
  TEST_ASSERT_EQUAL(CNW_CLOUD, cnWeather(2));
  TEST_ASSERT_EQUAL(CNW_OVERCAST, cnWeather(3));
  TEST_ASSERT_EQUAL(CNW_FOG, cnWeather(45));
  TEST_ASSERT_EQUAL(CNW_RAIN, cnWeather(53));
  TEST_ASSERT_EQUAL(CNW_RAIN, cnWeather(81));
  TEST_ASSERT_EQUAL(CNW_SNOW, cnWeather(73));
  TEST_ASSERT_EQUAL(CNW_SNOW, cnWeather(86));
  TEST_ASSERT_EQUAL(CNW_THUNDER, cnWeather(95));
  TEST_ASSERT_EQUAL_STRING("FOG", cnWeatherLabel(CNW_FOG));
  TEST_ASSERT_EQUAL_STRING("STORM", cnWeatherLabel(CNW_THUNDER));
}

void test_parse_weather_bad() {
  WeatherData w = {};
  w.tempC = 5;
  TEST_ASSERT_FALSE(parseWeather("{}", w));
  TEST_ASSERT_EQUAL_FLOAT(5, w.tempC);
}

void test_geocode() {
  GeoResult r[5];
  int n = parseGeocode(GEOCODE_JSON, r, 5);
  TEST_ASSERT_TRUE(n >= 1);
  TEST_ASSERT_EQUAL_STRING("San Francisco", r[0].name);
  TEST_ASSERT_EQUAL_STRING("US", r[0].country);
  TEST_ASSERT_EQUAL_STRING("California", r[0].admin1);
  TEST_ASSERT_EQUAL_INT(0, parseGeocode("{}", r, 5));
}

void test_url_encode() {
  char out[64];
  urlEncode("S\xC3\xA3o Paulo", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("S%C3%A3o%20Paulo", out);
  urlEncode("A&B", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("A%26B", out);
}

void test_truncate() {
  char out[32];
  truncateUpper("Llanfairpwllgwyngyll", out, 10);
  TEST_ASSERT_EQUAL_STRING("LLANFAIRPW", out);
}


void test_night_defaults() {
  Settings s = defaultSettings();
  TEST_ASSERT_TRUE(s.nightOn);
  TEST_ASSERT_EQUAL(23, s.nightStart);
  TEST_ASSERT_EQUAL(7, s.nightEnd);
  TEST_ASSERT_EQUAL(15, s.nightBrightness);
}

void test_night_parse() {
  Settings s;
  TEST_ASSERT_TRUE(parseSettings("{\"nightOn\":false,\"nightStart\":21,\"nightEnd\":6,\"nightBrightness\":0}", s));
  TEST_ASSERT_FALSE(s.nightOn);
  TEST_ASSERT_EQUAL(21, s.nightStart);
  TEST_ASSERT_EQUAL(6, s.nightEnd);
  TEST_ASSERT_EQUAL(0, s.nightBrightness);
  Settings base = defaultSettings();
  TEST_ASSERT_TRUE(parseSettingsOver("{\"nightStart\":24,\"nightEnd\":-1,\"nightBrightness\":300}", base, s));
  TEST_ASSERT_EQUAL(23, s.nightStart);  // out of range keeps base
  TEST_ASSERT_EQUAL(7, s.nightEnd);
  TEST_ASSERT_EQUAL(100, s.nightBrightness);  // clamped
  char buf[256];
  TEST_ASSERT_TRUE(serializeSettings(defaultSettings(), buf, sizeof(buf)) > 0);
  TEST_ASSERT_TRUE(parseSettings(buf, s));
  TEST_ASSERT_EQUAL(15, s.nightBrightness);
}

void test_effective_brightness() {
  Settings s = defaultSettings();  // 80 day, 15 night, 23 -> 7
  TEST_ASSERT_EQUAL(15, effectiveBrightness(s, 23, true));
  TEST_ASSERT_EQUAL(15, effectiveBrightness(s, 3, true));
  TEST_ASSERT_EQUAL(80, effectiveBrightness(s, 7, true));   // end hour is day
  TEST_ASSERT_EQUAL(80, effectiveBrightness(s, 22, true));
  TEST_ASSERT_EQUAL(80, effectiveBrightness(s, 3, false));  // unsynced -> day
  s.nightStart = 1; s.nightEnd = 5;                          // same-day window
  TEST_ASSERT_EQUAL(15, effectiveBrightness(s, 1, true));
  TEST_ASSERT_EQUAL(80, effectiveBrightness(s, 5, true));
  TEST_ASSERT_EQUAL(80, effectiveBrightness(s, 0, true));
  s.nightStart = 7; s.nightEnd = 7;                          // empty window
  TEST_ASSERT_EQUAL(80, effectiveBrightness(s, 7, true));
  s.nightStart = 23; s.nightEnd = 7; s.nightOn = false;
  TEST_ASSERT_EQUAL(80, effectiveBrightness(s, 3, true));
}

static int w8(const char* s) { return (int)strlen(s) * 8; }

void test_fit_label() {
  char out[32];
  fitLabel("VANCOUVER, WA", out, sizeof(out), 104, w8);  // 13 chars = 104 px
  TEST_ASSERT_EQUAL_STRING("VANCOUVER, WA", out);
  fitLabel("VANCOUVER, WA", out, sizeof(out), 100, w8);  // drops region
  TEST_ASSERT_EQUAL_STRING("VANCOUVER", out);
  fitLabel("LLANFAIRPWLLGWYNGYLL, GB", out, sizeof(out), 80, w8);  // 10 chars max
  TEST_ASSERT_EQUAL_STRING("LLANFAI...", out);
  fitLabel("PARIS", out, sizeof(out), 80, w8);
  TEST_ASSERT_EQUAL_STRING("PARIS", out);
}

void test_parse_latlon_range() {
  Settings s;
  TEST_ASSERT_TRUE(parseSettings("{\"lat\":999,\"lon\":-500}", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 37.7749f, s.lat);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -122.4194f, s.lon);
  TEST_ASSERT_TRUE(parseSettings("{\"lat\":-33.86,\"lon\":151.2}", s));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -33.86f, s.lat);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 151.2f, s.lon);
}

void test_parse_over_keeps_base() {
  Settings base = defaultSettings();
  base.face = 1;
  base.unit = 'C';
  Settings o;
  TEST_ASSERT_TRUE(parseSettingsOver("{\"face\":9,\"unit\":\"K\"}", base, o));
  TEST_ASSERT_EQUAL_UINT8(1, o.face);
  TEST_ASSERT_EQUAL_CHAR('C', o.unit);
  TEST_ASSERT_TRUE(parseSettingsOver("{\"brightness\":500}", base, o));
  TEST_ASSERT_EQUAL_UINT8(100, o.brightness);
  o = defaultSettings();
  TEST_ASSERT_FALSE(parseSettingsOver("{bad", base, o));
  TEST_ASSERT_EQUAL_MEMORY(&base, &o, sizeof(Settings));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_parse_invalid_json);
  RUN_TEST(test_parse_partial);
  RUN_TEST(test_parse_clamps);
  RUN_TEST(test_parse_latlon_range);
  RUN_TEST(test_parse_over_keeps_base);
  RUN_TEST(test_settings_roundtrip);
  RUN_TEST(test_temp);
  RUN_TEST(test_wmo);
  RUN_TEST(test_format);
  RUN_TEST(test_parse_weather);
  RUN_TEST(test_parse_weather_bad);
  RUN_TEST(test_wind);
  RUN_TEST(test_military_time);
  RUN_TEST(test_cn_weather);
  RUN_TEST(test_parse_weather_day);
  RUN_TEST(test_face_vitals);
  RUN_TEST(test_day_outlook);
  RUN_TEST(test_format_precip);
  RUN_TEST(test_geocode);
  RUN_TEST(test_url_encode);
  RUN_TEST(test_truncate);
  RUN_TEST(test_fit_label);
  RUN_TEST(test_night_defaults);
  RUN_TEST(test_night_parse);
  RUN_TEST(test_effective_brightness);
  return UNITY_END();
}
