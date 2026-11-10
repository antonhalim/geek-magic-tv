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
  TEST_ASSERT_TRUE(parseSettingsOver("{\"face\":5,\"unit\":\"K\"}", base, o));
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
  RUN_TEST(test_geocode);
  RUN_TEST(test_url_encode);
  RUN_TEST(test_truncate);
  return UNITY_END();
}
