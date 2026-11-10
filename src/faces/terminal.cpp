#include <string.h>
#include <time.h>
#include "face.h"
#include "../display.h"
#include "../weather.h"

// Layout (240x240)
static const int TX = (240 - TIME_W) / 2, TY = 62;  // time sprite
static const int GLITCH_OFF = 4;
static const int BAR_X = 44, BAR_Y = 118, BAR_W = 152, BAR_H = 4;
static const char* const SYNC_TEXT = "> CHRONO.SYNC OK";

class Terminal : public Face {
 public:
  void begin() override {
    bg = spriteColor(C_BG_B);
    grid = spriteColor(C_DIMGREEN);  // same colour inside and outside the time sprite
    tft.fillScreen(bg);
    for (int g = 60; g < 240; g += 60) {
      tft.drawFastVLine(g, 0, 240, grid);
      tft.drawFastHLine(0, g, 240, grid);
    }
    // amber 2px corner brackets, 20px arms, 8px inset
    tft.fillRect(8, 8, 20, 2, C_AMBER);
    tft.fillRect(8, 8, 2, 20, C_AMBER);
    tft.fillRect(212, 8, 20, 2, C_AMBER);
    tft.fillRect(230, 8, 2, 20, C_AMBER);
    tft.fillRect(8, 230, 20, 2, C_AMBER);
    tft.fillRect(8, 212, 2, 20, C_AMBER);
    tft.fillRect(212, 230, 20, 2, C_AMBER);
    tft.fillRect(230, 212, 2, 20, C_AMBER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GREEN);
    cursorX = 20 + tft.drawString(SYNC_TEXT, 20, 32, 2) + 3;
    cursorOn = false;

    tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, C_DIMGREEN);
    timeStr[0] = '\0';
    glitchReset(glitch, millis());
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.time, prev.time) != 0) {
      memcpy(timeStr, s.time, sizeof(timeStr));
      renderTime();
      if (!glitch.active) pushClean(TX, TY, GLITCH_OFF, bg);
    }
    if (s.second != prev.second) {
      int fw = s.second * BAR_W / 59;
      tft.fillRect(BAR_X, BAR_Y, fw, BAR_H, C_GREEN);
      tft.fillRect(BAR_X + fw, BAR_Y, BAR_W - fw, BAR_H, C_DIMGREEN);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate(s);
    if (strcmp(s.city, prev.city) != 0) {
      clearRect(20, 172, 200, 16);
      char city[48], buf[64];
      fitLabel(s.city, city, sizeof(city), 200 - tft.textWidth("> ATMOS.", 2), textWidth2);
      snprintf(buf, sizeof(buf), "> ATMOS.%s", city);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_AMBER);
      tft.drawString(buf, 20, 172, 2);
    }
    if (weatherChanged(s, prev)) drawWeather(s);
  }

  void tick(uint32_t ms) override {
    int g = glitchStep(glitch, ms);
    if (g == 1)
      pushSliced(TX, TY, 2, GLITCH_OFF, bg);
    else if (g == -1)
      pushClean(TX, TY, GLITCH_OFF, bg);

    bool on = (ms / 500) & 1;
    if (on != cursorOn) {
      cursorOn = on;
      tft.fillRect(cursorX, 33, 8, 13, on ? C_GREEN : bg);
    }
  }

 private:
  uint16_t bg = 0;
  uint16_t grid = 0;
  char timeStr[6] = "";
  Glitch glitch = {0, 0, false};
  int cursorX = 0;
  bool cursorOn = false;

  // Clear to background and restore any grid lines crossing the rect.
  void clearRect(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, bg);
    for (int g = 60; g < 240; g += 60) {
      if (g >= x && g < x + w) tft.drawFastVLine(g, y, h, grid);
      if (g >= y && g < y + h) tft.drawFastHLine(x, g, w, grid);
    }
  }

  void renderTime() {
    TFT_eSprite& spr = timeSprite();
    spr.fillSprite(bg);
    for (int g = 60; g < 240; g += 60) spr.drawFastVLine(g - TX, 0, TIME_H, grid);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(C_AMBER);
    spr.drawString(timeStr, TIME_W / 2, TIME_H / 2, 7);
  }

  void drawDate(const FaceState& s) {
    char buf[48];
    if (timeSynced()) {
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      snprintf(buf, sizeof(buf), "%04d.%02d.%02d // %.3s", t.tm_year + 1900, t.tm_mon + 1,
               t.tm_mday, s.date);
    } else {
      snprintf(buf, sizeof(buf), "----.--.-- // ---");
    }
    clearRect(20, 134, 200, 16);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_GREEN);
    tft.drawString(buf, 120, 142, 2);
  }

  void drawWeather(const FaceState& s) {
    clearRect(20, 192, 201, 34);
    int end = drawTemp(20, 196, s, C_GREEN);

    char row1[32];
    if (s.weatherValid)
      snprintf(row1, sizeof(row1), "%s  %d%%RH", s.label, s.humidity);
    else
      snprintf(row1, sizeof(row1), "%s", s.label);
    // Long labels fall back to the small GLCD font so they never overlap the temperature
    int font = (tft.textWidth(row1, 2) <= 220 - end - 6) ? 2 : 1;
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_GREEN);
    tft.drawString(row1, 220, font == 2 ? 194 : 198, font);

    char row2[20];
    if (s.weatherValid)
      snprintf(row2, sizeof(row2), "H %d  L %d", s.hi, s.lo);
    else
      snprintf(row2, sizeof(row2), "H --  L --");
    tft.setTextColor(C_AMBER);
    tft.drawString(row2, 220, 211, 2);
  }
};

static Terminal terminal;
Face* faceTerminal() { return &terminal; }
