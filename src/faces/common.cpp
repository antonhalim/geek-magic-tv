#include <string.h>
#include <time.h>
#include "face.h"
#include "../config.h"
#include "../display.h"
#include "../weather.h"

void buildFaceState(FaceState& s) {
  memset(&s, 0, sizeof(s));
  if (timeSynced()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    formatTime(t.tm_hour, t.tm_min, s.time);
    formatDate(t.tm_wday, t.tm_mday, t.tm_mon, t.tm_year + 1900, s.date);
    s.second = t.tm_sec;
  } else {
    strcpy(s.time, "--:--");
    strcpy(s.date, "--- -- --- ----");
  }
  truncateUpper(settings.city, s.city, 10);
  s.unit = settings.unit;
  s.weatherValid = weather.valid;
  s.stale = weatherStale();
  s.temp = toDisplayTemp(weather.tempC, s.unit);
  s.hi = toDisplayTemp(weather.hiC, s.unit);
  s.lo = toDisplayTemp(weather.loC, s.unit);
  s.humidity = weather.humidity;
  s.label = weather.valid ? wmoLabel(weather.code) : "NO SIGNAL";
}

TFT_eSprite& timeSprite() {
  static TFT_eSprite spr(&tft);
  if (!spr.created()) {
    spr.setColorDepth(8);
    spr.createSprite(TIME_W, TIME_H);
  }
  return spr;
}

uint16_t spriteColor(uint16_t c) { return tft.color8to16(tft.color16to8(c)); }

void glitchReset(Glitch& g, uint32_t now) {
  g.active = false;
  g.nextAt = now + random(4000, 8001);
}

int glitchStep(Glitch& g, uint32_t now) {
  if (g.active) {
    if ((int32_t)(now - g.endAt) >= 0) {
      glitchReset(g, now);
      return -1;
    }
    return 1;
  }
  if ((int32_t)(now - g.nextAt) >= 0) {
    g.active = true;
    g.endAt = now + 200;
    return 1;
  }
  return 0;
}

void pushSliced(int x, int y, int slices, int maxOff, uint16_t bg) {
  TFT_eSprite& spr = timeSprite();
  int h = TIME_H / slices;
  for (int i = 0; i < slices; i++) {
    int sy = i * h;
    int sh = (i == slices - 1) ? TIME_H - sy : h;
    int dx = random(-maxOff, maxOff + 1);
    tft.fillRect(x - maxOff, y + sy, maxOff + dx, sh, bg);
    tft.fillRect(x + TIME_W + dx, y + sy, maxOff - dx, sh, bg);
    spr.pushSprite(x + dx, y + sy, 0, sy, TIME_W, sh);
  }
}

void pushClean(int x, int y, int maxOff, uint16_t bg) {
  timeSprite().pushSprite(x, y);
  tft.fillRect(x - maxOff, y, maxOff, TIME_H, bg);
  tft.fillRect(x + TIME_W, y, maxOff, TIME_H, bg);
}

int drawTemp(int x, int y, const FaceState& s, uint16_t fg) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(fg);
  if (!s.weatherValid) return x + tft.drawString("--", x, y, 4);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", s.temp);
  x += tft.drawString(buf, x, y, 4);
  // Font 4 has no degree glyph: draw a 2px ring
  tft.drawCircle(x + 4, y + 5, 3, fg);
  tft.drawCircle(x + 4, y + 5, 2, fg);
  x += 10;
  char u[2] = {s.unit, '\0'};
  x += tft.drawString(u, x, y, 4);
  if (s.stale) x += tft.drawString("*", x, y, 4);
  return x;
}

static bool strEq(const char* a, const char* b) {
  if (!a || !b) return a == b;
  return strcmp(a, b) == 0;
}

bool weatherChanged(const FaceState& a, const FaceState& b) {
  return a.temp != b.temp || a.hi != b.hi || a.lo != b.lo || a.humidity != b.humidity ||
         a.unit != b.unit || a.weatherValid != b.weatherValid || a.stale != b.stale ||
         !strEq(a.label, b.label);
}
