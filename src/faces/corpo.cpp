#include <ESP8266WiFi.h>
#include <string.h>
#include <sys/time.h>
#include "face.h"
#include "../display.h"
#include "../weather.h"

#define C_RED 0xE907     // #E8213C
#define C_REDDIM 0x4882  // dim red for pulsing
#define C_WHITE 0xF79E   // #F2F2F2
#define C_GREY 0x8C52    // #8A8A92
#define C_RULE 0x39C8    // #3A3A42
#define C_BARS 0xDEDB    // #D8D8DE
#define C_BG_C 0x0841    // #0B0B0D

// Layout (240x240)
static const int STRIPE_H = 8;
static const int TX = (240 - TIME_W) / 2, TY = 34;  // time sprite (HH:MM:SS)
static const int GLITCH_OFF = 4;
static const int MS_Y = 94;
static const int RULE_Y = 104, RULE_X = 12, RULE_W = 216;
static const int DATE_Y = 116;
static const int PANEL_X = 12, PANEL_Y = 128, PANEL_W = 112, PANEL_H = 62;
static const int BAR_X = 136, BAR_Y = 130, BAR_W = 92, BAR_H = 44;
static const int HL_Y = 206;
static const int FOOT_Y = 216;
static const int WARN_X = 220, WARN_Y = 13;

static int textWidth1(const char* s) { return tft.textWidth(s, 1); }

class Corpo : public Face {
 public:
  void begin() override {
    bg = spriteColor(C_BG_C);
    tft.fillScreen(bg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GREY);
    tft.drawString("SECTOR 07 // CLEARANCE 3", 12, 16, 1);
    tft.drawFastHLine(RULE_X, RULE_Y, RULE_W, C_RULE);

    char id[16];
    snprintf(id, sizeof(id), "ID-%04u-K", (unsigned)(ESP.getChipId() % 10000));
    tft.setTextDatum(MR_DATUM);
    tft.drawString(id, 228, DATE_Y, 2);

    tft.drawRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, C_RED);
    tft.fillRect(PANEL_X, PANEL_Y, PANEL_W, 12, C_RED);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(bg);
    tft.drawString("ENV SCAN", PANEL_X + 6, PANEL_Y + 3, 1);

    tft.drawRect(12, FOOT_Y, 216, 14, C_RULE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GREY);
    tft.drawString("UNAUTHORIZED ACCESS PROHIBITED", 120, FOOT_Y + 7, 1);

    timeStr[0] = '\0';
    ruleW = -1;
    stripeOff = 0;
    warnOn = recOn = false;
    drawWarn(true);
    barMinute = -1;
    glitchReset(glitch, millis());
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      renderTime();
      if (!glitch.active) pushClean(TX, TY, GLITCH_OFF, bg);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate();
    if (strcmp(s.time, prev.time) != 0 || strcmp(s.city, prev.city) != 0) drawBarcode(s);
    if (weatherChanged(s, prev)) drawWeather(s);
  }

  void tick(uint32_t ms) override {
    int g = glitchStep(glitch, ms);
    if (g == 1)
      pushSliced(TX, TY, 2, GLITCH_OFF, bg);
    else if (g == -1)
      pushClean(TX, TY, GLITCH_OFF, bg);

    int msInSec = msNow(ms);
    drawMillis(msInSec);
    drawRule(msInSec);
    if ((ms / 100) != lastStripeStep) {  // stripes scroll at 10 fps
      lastStripeStep = ms / 100;
      stripeOff = (stripeOff + 1) % 16;
      drawStripes();
    }
    drawWarn((ms / 600) & 1);
    drawRec(((ms / 1000) & 1) == 0);
  }

 private:
  uint16_t bg = 0;
  char timeStr[9] = "";
  Glitch glitch = {0, 0, false};
  int ruleW = -1;
  int stripeOff = 0;
  uint32_t lastStripeStep = 0;
  bool warnOn = false, recOn = false;
  int barMinute = -1;

  int msNow(uint32_t ms) {
    if (!timeSynced()) return ms % 1000;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)(tv.tv_usec / 1000);
  }

  // White digits, red colons. Font 7 glyphs: digits 32 px, colon 12 px.
  void renderTime() {
    TFT_eSprite& spr = timeSprite();
    spr.fillSprite(bg);
    int x0 = (TIME_W - spr.textWidth(timeStr, 7)) / 2;
    int y0 = (TIME_H - 48) / 2;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_WHITE);
    spr.drawString(timeStr, x0, y0, 7);
    spr.setTextColor(C_RED);
    spr.drawString(":", x0 + 64, y0, 7);
    spr.drawString(":", x0 + 140, y0, 7);
  }

  void drawMillis(int msInSec) {
    char buf[6];
    if (timeSynced()) snprintf(buf, sizeof(buf), ".%03d", msInSec);
    else strcpy(buf, ".---");
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_GREY, bg);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, 228, MS_Y, 2);
    tft.setTextPadding(0);
  }

  // Thin grey rule that fills red across each minute.
  void drawRule(int msInSec) {
    int w = 0;
    if (timeSynced()) {
      time_t now = time(nullptr);
      w = (int)(((now % 60) * 1000L + msInSec) * RULE_W / 60000L);
    }
    if (w == ruleW) return;
    if (w < ruleW || ruleW < 0) {
      tft.drawFastHLine(RULE_X, RULE_Y, RULE_W, C_RULE);
      ruleW = 0;
    }
    tft.drawFastHLine(RULE_X + ruleW, RULE_Y, w - ruleW, C_RED);
    ruleW = w;
  }

  void drawDate() {
    char buf[20] = "--- --.--.----";
    if (timeSynced()) {
      static const char* const DOW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      snprintf(buf, sizeof(buf), "%s %02d.%02d.%04d", DOW[t.tm_wday % 7], t.tm_mday, t.tm_mon + 1,
               t.tm_year + 1900);
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_WHITE, bg);
    tft.setTextPadding(130);
    tft.drawString(buf, 12, DATE_Y, 2);
    tft.setTextPadding(0);
  }

  // Diagonal hazard stripes, 8 px bands, scrolling sideways.
  void drawStripes() {
    for (int y = 0; y < STRIPE_H; y++) {
      int shift = (stripeOff + y) % 16;
      for (int x = -shift; x < 240; x += 16) {
        int x0 = x < 0 ? 0 : x;
        int x1 = x + 8 > 240 ? 240 : x + 8;
        if (x1 > x0) tft.drawFastHLine(x0, y, x1 - x0, C_RED);
        int b0 = x + 8 < 0 ? 0 : x + 8;
        int b1 = x + 16 > 240 ? 240 : x + 16;
        if (b1 > b0) tft.drawFastHLine(b0, y, b1 - b0, bg);
      }
    }
  }

  void drawWarn(bool on) {
    if (on == warnOn) return;
    warnOn = on;
    uint16_t c = on ? C_RED : C_REDDIM;
    tft.drawTriangle(WARN_X, WARN_Y, WARN_X + 9, WARN_Y + 16, WARN_X - 9, WARN_Y + 16, c);
    tft.drawTriangle(WARN_X, WARN_Y + 1, WARN_X + 8, WARN_Y + 16, WARN_X - 8, WARN_Y + 16, c);
    tft.fillRect(WARN_X, WARN_Y + 6, 1, 5, c);
    tft.fillRect(WARN_X, WARN_Y + 13, 1, 1, c);
  }

  void drawRec(bool on) {
    if (on == recOn) return;
    recOn = on;
    tft.fillCircle(201, HL_Y + 3, 2, on ? C_RED : bg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_RED, bg);
    tft.drawString("REC", 207, HL_Y, 1);
  }

  // Barcode re-shuffles every minute (seeded by the minute), city label underneath.
  void drawBarcode(const FaceState& s) {
    int minute = (s.time[3] - '0') * 10 + (s.time[4] - '0');
    if (s.time[0] == '-') minute = 0;
    tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, bg);
    uint32_t seed = (uint32_t)minute * 2654435761u + 7;
    int x = BAR_X;
    while (x < BAR_X + BAR_W) {
      seed = seed * 1103515245u + 12345u;
      int w = 1 + (seed >> 16) % 5;
      int gap = 1 + (seed >> 24) % 3;
      if (x + w > BAR_X + BAR_W) break;
      tft.fillRect(x, BAR_Y, w, BAR_H, C_BARS);
      x += w + gap;
    }
    if (strcmp(s.city, lastCity) != 0 || barMinute < 0) {
      char fit[48];
      fitLabel(s.city, fit, sizeof(fit), BAR_W, textWidth1);
      for (char* p = fit; *p; p++) {
        if (*p == ',') { memmove(p, p + 1, strlen(p)); *p = '-'; }  // "VANCOUVER, WA" -> "VANCOUVER-WA"
        else if (*p == ' ') *p = '-';
      }
      tft.fillRect(BAR_X, BAR_Y + BAR_H + 4, BAR_W, 8, bg);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_GREY);
      tft.drawString(fit, BAR_X, BAR_Y + BAR_H + 4, 1);
      strncpy(lastCity, s.city, sizeof(lastCity) - 1);
    }
    barMinute = minute;
  }
  char lastCity[48] = "";

  void drawWeather(const FaceState& s) {
    tft.fillRect(PANEL_X + 1, PANEL_Y + 12, PANEL_W - 2, PANEL_H - 13, bg);
    drawTemp(PANEL_X + 6, PANEL_Y + 16, s, C_WHITE);
    char buf[24];
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GREY);
    tft.drawString(s.label, PANEL_X + 6, PANEL_Y + 44, 1);
    if (s.weatherValid) snprintf(buf, sizeof(buf), "%d%%RH", s.humidity);
    else strcpy(buf, "--%RH");
    tft.drawString(buf, PANEL_X + 6, PANEL_Y + 53, 1);

    tft.fillRect(12, HL_Y, 120, 8, bg);
    if (s.weatherValid) snprintf(buf, sizeof(buf), "H %d / L %d", s.hi, s.lo);
    else strcpy(buf, "H -- / L --");
    tft.drawString(buf, 12, HL_Y, 1);
  }
};

static Corpo corpo;
Face* faceCorpo() { return &corpo; }
