#pragma once
#include <cstddef>
#include <cstdint>

struct Settings {
  char city[48];
  float lat;
  float lon;
  char unit;  // 'C' or 'F'
  uint8_t face;
  uint8_t brightness;
  bool nightOn;
  uint8_t nightStart;  // hour 0-23, night begins
  uint8_t nightEnd;    // hour 0-23, day resumes
  uint8_t nightBrightness;
};

struct WeatherData {
  float tempC;
  float hiC;
  float loC;
  int humidity;
  int code;
  long utcOffset;
  int dayCode;      // today's dominant WMO code
  int precipProb;   // today's max rain chance %, -1 if unknown
  float precipMm;   // today's expected total
  float windKmh;
  int windDir;      // degrees the wind comes FROM, -1 if unknown
  bool valid;
};

struct GeoResult {
  char name[48];
  char admin1[32];
  char country[4];
  float lat;
  float lon;
};

Settings defaultSettings();
bool parseSettings(const char* json, Settings& out);
bool parseSettingsOver(const char* json, const Settings& base, Settings& out);
size_t serializeSettings(const Settings& s, char* buf, size_t len);
bool parseWeather(const char* json, WeatherData& out);
int parseGeocode(const char* json, GeoResult* out, int max);
int toDisplayTemp(float c, char unit);
const char* wmoLabel(int code);
// Today's outlook tag: STORM, SNOW, RAIN or DRY.
const char* dayOutlook(int dayCode, int precipProb, float precipMm);
// e.g. "58% TODAY // 7.3mm" (C) or "58% TODAY // 0.29in" (F).
void formatPrecip(int precipProb, float precipMm, char unit, char* out, size_t len);
// 8-point compass name for a bearing in degrees.
const char* windCompass(int deg);
// "WIND SE 9mph" (F) / "WIND SE 14km/h" (C), "WIND --" if unknown.
void formatWind(float kmh, int deg, char unit, char* out, size_t len);
// Military time zone letter for a UTC offset: Z, A-M (east, no J), N-Y (west); J if not whole hours.
char militaryZone(long offsetSec);
// "UTC-8", "UTC+5:30", "UTC".
void formatUtcOffset(long offsetSec, char* out, size_t len);
// Date-time group, e.g. "DTG 101418U NOV 26" (mon0 is 0-based).
void formatDtg(int mday, int hour, int min, char zone, int mon0, int year, char* out, size_t len);
// Weather category for the Chinese-character faces; order matches the CJK weather glyphs.
enum CnWeather { CNW_SUNNY, CNW_CLOUD, CNW_OVERCAST, CNW_FOG, CNW_RAIN, CNW_SNOW, CNW_THUNDER };
CnWeather cnWeather(int wmoCode);
const char* cnWeatherLabel(CnWeather w);  // CLEAR, CLOUDY, OVERCAST, FOG, RAIN, SNOW, STORM
void formatTime(int h, int m, char* out);
void formatDate(int wday, int mday, int mon, int year, char* out);
void urlEncode(const char* in, char* out, size_t len);
// Brightness for this local hour; day brightness until the clock is synced.
uint8_t effectiveBrightness(const Settings& s, int hour, bool synced);
void truncateUpper(const char* in, char* out, size_t maxChars);
// Fits "NAME, REGION" into maxW px: full text, else NAME, else NAME cut with "...".
void fitLabel(const char* in, char* out, size_t len, int maxW, int (*width)(const char*));
