#include "logic.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstring>

static void copyStr(char* dst, size_t cap, const char* src) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

Settings defaultSettings() {
  Settings s;
  memset(&s, 0, sizeof(s));
  copyStr(s.city, sizeof(s.city), "San Francisco");
  s.lat = 37.7749f;
  s.lon = -122.4194f;
  s.unit = 'F';
  s.face = 0;
  s.brightness = 80;
  s.nightOn = true;
  s.nightStart = 23;
  s.nightEnd = 7;
  s.nightBrightness = 15;
  return s;
}

bool parseSettings(const char* json, Settings& out) {
  return parseSettingsOver(json, defaultSettings(), out);
}

bool parseSettingsOver(const char* json, const Settings& base, Settings& out) {
  out = base;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;

  const char* city = doc["city"] | static_cast<const char*>(nullptr);
  if (city && city[0]) copyStr(out.city, sizeof(out.city), city);
  if (doc["lat"].is<float>()) {
    float v = doc["lat"];
    if (v >= -90.0f && v <= 90.0f) out.lat = v;
  }
  if (doc["lon"].is<float>()) {
    float v = doc["lon"];
    if (v >= -180.0f && v <= 180.0f) out.lon = v;
  }

  const char* unit = doc["unit"] | static_cast<const char*>(nullptr);
  if (unit && (unit[0] == 'C' || unit[0] == 'F') && unit[1] == '\0') out.unit = unit[0];

  if (doc["face"].is<int>()) {
    int f = doc["face"];
    if (f >= 0 && f <= 3) out.face = static_cast<uint8_t>(f);
  }
  if (doc["brightness"].is<int>()) {
    int b = doc["brightness"];
    out.brightness = static_cast<uint8_t>(b < 0 ? 0 : (b > 100 ? 100 : b));
  }
  if (doc["nightOn"].is<bool>()) out.nightOn = doc["nightOn"];
  if (doc["nightStart"].is<int>()) {
    int h = doc["nightStart"];
    if (h >= 0 && h <= 23) out.nightStart = static_cast<uint8_t>(h);
  }
  if (doc["nightEnd"].is<int>()) {
    int h = doc["nightEnd"];
    if (h >= 0 && h <= 23) out.nightEnd = static_cast<uint8_t>(h);
  }
  if (doc["nightBrightness"].is<int>()) {
    int b = doc["nightBrightness"];
    out.nightBrightness = static_cast<uint8_t>(b < 0 ? 0 : (b > 100 ? 100 : b));
  }
  return true;
}

size_t serializeSettings(const Settings& s, char* buf, size_t len) {
  JsonDocument doc;
  doc["city"] = s.city;
  doc["lat"] = s.lat;
  doc["lon"] = s.lon;
  char unit[2] = {s.unit, '\0'};
  doc["unit"] = unit;
  doc["face"] = s.face;
  doc["brightness"] = s.brightness;
  doc["nightOn"] = s.nightOn;
  doc["nightStart"] = s.nightStart;
  doc["nightEnd"] = s.nightEnd;
  doc["nightBrightness"] = s.nightBrightness;
  if (measureJson(doc) >= len) return 0;
  return serializeJson(doc, buf, len);
}

bool parseWeather(const char* json, WeatherData& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonObjectConst cur = doc["current"];
  if (cur.isNull() || !cur["temperature_2m"].is<float>()) return false;

  WeatherData w;
  w.tempC = cur["temperature_2m"];
  w.humidity = cur["relative_humidity_2m"] | 0;
  w.code = cur["weather_code"] | 0;
  w.hiC = doc["daily"]["temperature_2m_max"][0] | w.tempC;
  w.loC = doc["daily"]["temperature_2m_min"][0] | w.tempC;
  w.utcOffset = doc["utc_offset_seconds"] | 0L;
  w.dayCode = doc["daily"]["weather_code"][0] | w.code;
  w.precipProb = doc["daily"]["precipitation_probability_max"][0] | -1;
  w.precipMm = doc["daily"]["precipitation_sum"][0] | 0.0f;
  w.valid = true;
  out = w;
  return true;
}

int parseGeocode(const char* json, GeoResult* out, int max) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return 0;
  JsonArrayConst results = doc["results"];
  int n = 0;
  for (JsonObjectConst r : results) {
    if (n >= max) break;
    GeoResult& g = out[n];
    memset(&g, 0, sizeof(g));
    copyStr(g.name, sizeof(g.name), r["name"] | "");
    copyStr(g.admin1, sizeof(g.admin1), r["admin1"] | "");
    copyStr(g.country, sizeof(g.country), r["country_code"] | "");
    g.lat = r["latitude"] | 0.0f;
    g.lon = r["longitude"] | 0.0f;
    n++;
  }
  return n;
}

int toDisplayTemp(float c, char unit) {
  float v = (unit == 'F') ? c * 9.0f / 5.0f + 32.0f : c;
  return static_cast<int>(std::round(v));
}

const char* wmoLabel(int code) {
  switch (code) {
    case 0: return "CLEAR";
    case 1: return "MOSTLY CLEAR";
    case 2: return "PARTLY CLOUDY";
    case 3: return "OVERCAST";
    case 45: case 48: return "FOG";
    case 51: case 52: case 53: case 54: case 55: case 56: case 57: return "DRIZZLE";
    case 61: case 62: case 63: case 64: case 65: case 66: case 67:
    case 80: case 81: case 82: return "RAIN";
    case 71: case 72: case 73: case 74: case 75: case 76: case 77:
    case 85: case 86: return "SNOW";
    case 95: case 96: case 97: case 98: case 99: return "STORM";
    default: return "UNKNOWN";
  }
}

void formatTime(int h, int m, char* out) {
  snprintf(out, 6, "%02d:%02d", h, m);
}

void formatDate(int wday, int mday, int mon, int year, char* out) {
  static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  snprintf(out, 16, "%s %02d %s %04d", DAYS[wday % 7], mday, MONTHS[mon % 12], year);
}

void urlEncode(const char* in, char* out, size_t len) {
  static const char HEXCHARS[] = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in; in++) {
    unsigned char c = static_cast<unsigned char>(*in);
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    if (plain) {
      if (o + 1 >= len) break;
      out[o++] = static_cast<char>(c);
    } else {
      if (o + 3 >= len) break;
      out[o++] = '%';
      out[o++] = HEXCHARS[c >> 4];
      out[o++] = HEXCHARS[c & 15];
    }
  }
  out[o] = '\0';
}

void truncateUpper(const char* in, char* out, size_t maxChars) {
  size_t i = 0;
  for (; in[i] && i < maxChars; i++) {
    char c = in[i];
    out[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  }
  out[i] = '\0';
}

void fitLabel(const char* in, char* out, size_t len, int maxW, int (*width)(const char*)) {
  if (!len) return;
  snprintf(out, len, "%s", in);
  if (width(out) <= maxW) return;
  char* comma = strchr(out, ',');
  if (comma) *comma = '\0';
  if (width(out) <= maxW) return;
  size_t n = strlen(out);
  while (n > 0) {
    out[--n] = '\0';
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s...", out);
    if (width(tmp) <= maxW) {
      snprintf(out, len, "%s", tmp);
      return;
    }
  }
}

uint8_t effectiveBrightness(const Settings& s, int hour, bool synced) {
  if (!synced || !s.nightOn || s.nightStart == s.nightEnd) return s.brightness;
  bool night = (s.nightStart < s.nightEnd)
                   ? (hour >= s.nightStart && hour < s.nightEnd)   // e.g. 01 -> 05
                   : (hour >= s.nightStart || hour < s.nightEnd);  // wraps midnight
  return night ? s.nightBrightness : s.brightness;
}

const char* dayOutlook(int dayCode, int precipProb, float precipMm) {
  if (dayCode >= 95) return "STORM";
  if ((dayCode >= 71 && dayCode <= 77) || dayCode == 85 || dayCode == 86) return "SNOW";
  bool wetCode = (dayCode >= 51 && dayCode <= 67) || (dayCode >= 80 && dayCode <= 82);
  if (wetCode || precipProb >= 50 || precipMm >= 1.0f) return "RAIN";
  return "DRY";
}

void formatPrecip(int precipProb, float precipMm, char unit, char* out, size_t len) {
  if (precipProb < 0) {
    snprintf(out, len, "-- TODAY");
  } else if (unit == 'F') {
    snprintf(out, len, "%d%% TODAY // %.2fin", precipProb, precipMm / 25.4f);
  } else {
    snprintf(out, len, "%d%% TODAY // %.1fmm", precipProb, precipMm);
  }
}
