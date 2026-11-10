#include <string.h>
#include "face.h"
#include "../display.h"

#define C_LAVENDER 0xE73F  // #E6E6FF

// Layout (240x240)
static const int TX = 20, TY = 50;  // time sprite
static const int GLITCH_OFF = 6;
static const int HORIZON = 190, VP_X = 120;
static const int GRID_V = 7;                  // verticals each side of centre
static const int GRID_SPACING = 36;           // at bottom edge
static const int GRID_H = 5;                  // moving horizontal lines
static const int GRID_STEPS = 20;             // animation steps per line spacing

class Netrunner : public Face {
 public:
  void begin() override {
    bg = spriteColor(C_BG_A);
    tft.fillScreen(bg);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_CYAN, bg);
    tft.drawString("SYS//ONLINE", 12, 20, 2);
    tft.drawFastHLine(12, 30, 217, C_CYAN);
    tft.drawRect(12, 140, 216, 38, C_MAGENTA);
    tft.drawFastHLine(0, HORIZON, 240, C_MAGENTA);
    drawVerticals();
    timeStr[0] = '\0';
    step = 0;
    for (int i = 0; i < GRID_H; i++) lineY[i] = -1;
    glitchReset(glitch, millis());
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.city, prev.city) != 0) {
      tft.fillRect(96, 12, 133, 17, bg);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(C_MAGENTA, bg);
      tft.drawString(s.city, 228, 20, 2);
    }
    if (strcmp(s.time, prev.time) != 0) {
      memcpy(timeStr, s.time, sizeof(timeStr));
      renderTime(2, 0);
      if (!glitch.active) pushClean(TX, TY, GLITCH_OFF, bg);
    }
    if (strcmp(s.date, prev.date) != 0 || s.second != prev.second) {
      char buf[24];
      if (s.time[0] == '-')
        snprintf(buf, sizeof(buf), "%s  :--", s.date);
      else
        snprintf(buf, sizeof(buf), "%s  :%02d", s.date, s.second);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_LAVENDER, bg);
      tft.setTextPadding(200);
      tft.drawString(buf, 120, 126, 2);
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
    animateGrid();
  }

 private:
  uint16_t bg = 0;
  char timeStr[6] = "";
  Glitch glitch = {0, 0, false};
  int step = 0;
  int lineY[GRID_H];

  void renderTime(int magentaDx, int cyanDx) {
    TFT_eSprite& spr = timeSprite();
    spr.fillSprite(bg);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(C_MAGENTA);
    spr.drawString(timeStr, TIME_W / 2 + magentaDx, TIME_H / 2, 7);
    spr.setTextColor(C_CYAN);
    spr.drawString(timeStr, TIME_W / 2 + cyanDx, TIME_H / 2, 7);
  }

  void drawWeather(const FaceState& s) {
    tft.fillRect(13, 141, 214, 36, bg);
    drawTemp(20, 146, s, C_YELLOW);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_CYAN);
    tft.drawString(s.label, 220, 143, 2);
    char buf[20];
    if (s.weatherValid)
      snprintf(buf, sizeof(buf), "H %d  L %d", s.hi, s.lo);
    else
      snprintf(buf, sizeof(buf), "H --  L --");
    tft.setTextColor(C_MAGENTA);
    tft.drawString(buf, 220, 160, 2);
  }

  void drawVerticals() {
    for (int i = -GRID_V; i <= GRID_V; i++)
      tft.drawLine(VP_X, HORIZON, VP_X + i * GRID_SPACING, 239, C_MAGENTA);
  }

  // Horizontal lines move from the horizon toward the viewer, spaced
  // quadratically for perspective. Only the y 191-239 band is touched.
  void animateGrid() {
    step = (step + 1) % GRID_STEPS;
    int next[GRID_H];
    const int den = GRID_H * GRID_STEPS;
    for (int k = 0; k < GRID_H; k++) {
      int num = k * GRID_STEPS + step;
      int y = HORIZON + 49 * num * num / (den * den);
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
