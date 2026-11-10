#include <ESP8266WiFi.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include "face.h"
#include "../display.h"
#include "../weather.h"

#define C_LAVENDER 0xE73F  // #E6E6FF
#define C_DIMCYAN 0x0472   // #008C96

// Layout (240x240)
static const int TX = (240 - TIME_W) / 2, TY = 46;  // time sprite (HH:MM:SS)
static const int GLITCH_OFF = 6;
static const int TICKER_X = 12, TICKER_Y = 34, TICKER_W = 216;  // font 1, 8 px tall
static const int MS_X = 228, MS_Y = 107;   // right edge of the seconds
static const int DATE_Y = 124;
static const int BOX_Y = 136, BOX_H = 36;  // weather box
static const int HORIZON = 202, VP_X = 120;
static const int SUN_R = 28;
static const int GRID_V = 7;               // verticals each side of centre
static const int GRID_SPACING = 36;        // at bottom edge
static const int GRID_H = 5;               // moving horizontal lines
static const int GRID_STEPS = 20;          // animation steps per line spacing

static uint16_t lerp565(uint16_t a, uint16_t b, int t, int n) {
  int r = ((a >> 11) * (n - t) + (b >> 11) * t) / n;
  int g = (((a >> 5) & 63) * (n - t) + ((b >> 5) & 63) * t) / n;
  int bl = ((a & 31) * (n - t) + (b & 31) * t) / n;
  return (r << 11) | (g << 5) | bl;
}

class Netrunner : public Face {
 public:
  void begin() override {
    bg = spriteColor(C_BG_A);
    tft.fillScreen(bg);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_CYAN, bg);
    tft.drawString("SYS//ONLINE", 12, 20, 2);
    tft.drawFastHLine(12, 30, 217, C_CYAN);
    tft.drawRect(12, BOX_Y, 216, BOX_H, C_MAGENTA);
    drawSun();
    tft.drawFastHLine(0, HORIZON, 240, C_MAGENTA);
    drawVerticals();
    timeStr[0] = '\0';
    step = 0;
    for (int i = 0; i < GRID_H; i++) lineY[i] = -1;
    buildTicker();
    tickerX = TICKER_W;
    glitchReset(glitch, millis());
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 128, textWidth2);
      tft.fillRect(96, 12, 133, 17, bg);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(C_MAGENTA, bg);
      tft.drawString(city, 228, 20, 2);
    }
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      renderTime(2, 0);
      if (!glitch.active) pushClean(TX, TY, GLITCH_OFF, bg);
    }
    if (strcmp(s.date, prev.date) != 0) {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_LAVENDER, bg);
      tft.setTextPadding(200);
      tft.drawString(s.date, 120, DATE_Y, 2);
      tft.setTextPadding(0);
    }
    if (weatherChanged(s, prev)) drawWeather(s);
  }

  void tick(uint32_t ms) override {
    int g = glitchStep(glitch, ms);
    if (g == 1) {
      // chromatic flash: wider magenta/cyan split on random frames
      if (random(2)) renderTime(5, -3); else renderTime(2, 0);
      pushSliced(TX, TY, 4, GLITCH_OFF, bg);
    } else if (g == -1) {
      renderTime(2, 0);
      pushClean(TX, TY, GLITCH_OFF, bg);
    }
    drawMillis();
    drawTicker();
    animateGrid();
  }

 private:
  uint16_t bg = 0;
  char timeStr[9] = "";
  Glitch glitch = {0, 0, false};
  int step = 0;
  int lineY[GRID_H];
  char ticker[112] = "";
  int tickerX = 0, tickerW = 0;

  // HH:MM:SS in font 7. 7-segment glyphs are fixed width, so it never shifts.
  void renderTime(int magentaDx, int cyanDx) {
    TFT_eSprite& spr = timeSprite();
    spr.fillSprite(bg);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(C_MAGENTA);
    spr.drawString(timeStr, TIME_W / 2 + magentaDx, TIME_H / 2, 7);
    spr.setTextColor(C_CYAN);
    spr.drawString(timeStr, TIME_W / 2 + cyanDx, TIME_H / 2, 7);
  }

  void drawMillis() {
    char buf[6];
    if (timeSynced()) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      snprintf(buf, sizeof(buf), ".%03d", (int)(tv.tv_usec / 1000));
    } else {
      strcpy(buf, ".---");
    }
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_MAGENTA, bg);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, MS_X, MS_Y, 2);
    tft.setTextPadding(0);
  }

  void buildTicker() {
    static const char* const lines[] = {
        "NEURAL LINK STABLE", "ICE BREACH 0%", "DAEMON SWEEP CLEAN",
        "NIGHT CITY NET OK", "BLACKWALL INTACT", "PACKET STORM NOMINAL"};
    snprintf(ticker, sizeof(ticker), "0x%04X // %s // RSSI %ddBm // HEAP %uK // %s // ",
             (unsigned)random(0x10000), lines[random(6)], (int)WiFi.RSSI(),
             (unsigned)(ESP.getFreeHeap() / 1024), lines[random(6)]);
    tickerW = tft.textWidth(ticker, 1);
  }

  // Scrolls right-to-left, clipped to the band between header and time.
  void drawTicker() {
    tickerX -= 2;
    if (tickerX < -tickerW) {
      buildTicker();
      tickerX = TICKER_W;
    }
    tft.setViewport(TICKER_X, TICKER_Y, TICKER_W, 8);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_DIMCYAN, bg);
    tft.drawString(ticker, tickerX, 0, 1);
    tft.fillRect(tickerX + tickerW, 0, 4, 8, bg);  // erase the 2 px trail
    tft.resetViewport();
  }

  // Half sun sitting on the horizon: yellow-to-magenta with widening gaps near the bottom.
  void drawSun() {
    for (int k = 1; k <= SUN_R; k++) {  // k = rows above the horizon
      if (k <= 18 && ((k % 6) == 1 || (k <= 10 && (k % 6) == 2))) continue;
      int hw = (int)sqrtf((float)(SUN_R * SUN_R - k * k));
      uint16_t c = lerp565(C_MAGENTA, C_YELLOW, k, SUN_R);
      tft.drawFastHLine(VP_X - hw, HORIZON - k, 2 * hw + 1, c);
    }
  }

  void drawWeather(const FaceState& s) {
    tft.fillRect(13, BOX_Y + 1, 214, BOX_H - 2, bg);
    drawTemp(20, BOX_Y + 6, s, C_YELLOW);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_CYAN);
    tft.drawString(s.label, 220, BOX_Y + 3, 2);
    char buf[20];
    if (s.weatherValid)
      snprintf(buf, sizeof(buf), "H %d  L %d", s.hi, s.lo);
    else
      snprintf(buf, sizeof(buf), "H --  L --");
    tft.setTextColor(C_MAGENTA);
    tft.drawString(buf, 220, BOX_Y + 19, 2);
  }

  void drawVerticals() {
    for (int i = -GRID_V; i <= GRID_V; i++)
      tft.drawLine(VP_X, HORIZON, VP_X + i * GRID_SPACING, 239, C_MAGENTA);
  }

  // Horizontal lines move from the horizon toward the viewer, spaced
  // quadratically for perspective. Only the band below the horizon is touched.
  void animateGrid() {
    step = (step + 1) % GRID_STEPS;
    int next[GRID_H];
    const int den = GRID_H * GRID_STEPS;
    for (int k = 0; k < GRID_H; k++) {
      int num = k * GRID_STEPS + step;
      int y = HORIZON + (239 - HORIZON) * num * num / (den * den);
      next[k] = (y > HORIZON + 1) ? y : -1;
    }
    for (int k = 0; k < GRID_H; k++) {
      if (lineY[k] < 0) continue;
      bool kept = false;
      for (int j = 0; j < GRID_H; j++) kept |= (next[j] == lineY[k]);
      if (!kept) tft.drawFastHLine(0, lineY[k], 240, bg);
    }
    drawVerticals();
    for (int k = 0; k < GRID_H; k++) {
      if (next[k] >= 0) tft.drawFastHLine(0, next[k], 240, C_MAGENTA);
      lineY[k] = next[k];
    }
  }
};

static Netrunner netrunner;
Face* faceNetrunner() { return &netrunner; }
