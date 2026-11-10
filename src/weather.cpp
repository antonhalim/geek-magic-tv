#include "weather.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <time.h>

WeatherData weather = {0, 0, 0, 0, 0, 0, false};
uint32_t weatherFetchedAt = 0;

static const uint32_t OK_INTERVAL = 15UL * 60UL * 1000UL;
static const uint32_t FAIL_INTERVAL = 60UL * 1000UL;
static const uint32_t STALE_AFTER = 60UL * 60UL * 1000UL;

static bool fetchPending = true;
static uint32_t nextFetchAt = 0;
static long appliedOffset = 0;
static bool offsetApplied = false;

static bool fetchWeather() {
  char url[320];  // ~265 chars with the daily outlook fields
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,weather_code"
           "&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max,precipitation_sum"
           "&timezone=auto&forecast_days=1",
           settings.lat, settings.lon);
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  bool ok = false;
  if (http.GET() == HTTP_CODE_OK) {
    String body = http.getString();
    WeatherData w;
    if (parseWeather(body.c_str(), w)) {
      weather = w;
      ok = true;
    }
  }
  http.end();
  return ok;
}

void weatherRequestRefresh() { fetchPending = true; }

void weatherLoop() {
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if (!fetchPending && (int32_t)(now - nextFetchAt) < 0) return;
  fetchPending = false;
  if (fetchWeather()) {
    weatherFetchedAt = now ? now : 1;
    nextFetchAt = now + OK_INTERVAL;
    if (!offsetApplied || appliedOffset != weather.utcOffset) {
      configTime(weather.utcOffset, 0, "pool.ntp.org", "time.google.com");
      appliedOffset = weather.utcOffset;
      offsetApplied = true;
    }
  } else {
    nextFetchAt = now + FAIL_INTERVAL;
  }
}

bool weatherStale() {
  return weatherFetchedAt == 0 || (uint32_t)(millis() - weatherFetchedAt) > STALE_AFTER;
}

bool timeSynced() { return time(nullptr) > 1700000000; }
