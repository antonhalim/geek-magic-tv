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
};

struct WeatherData {
  float tempC;
  float hiC;
  float loC;
  int humidity;
  int code;
  long utcOffset;
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
size_t serializeSettings(const Settings& s, char* buf, size_t len);
bool parseWeather(const char* json, WeatherData& out);
int parseGeocode(const char* json, GeoResult* out, int max);
int toDisplayTemp(float c, char unit);
const char* wmoLabel(int code);
void formatTime(int h, int m, char* out);
void formatDate(int wday, int mday, int mon, int year, char* out);
void urlEncode(const char* in, char* out, size_t len);
void truncateUpper(const char* in, char* out, size_t maxChars);
