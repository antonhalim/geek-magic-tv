#include <math.h>
#include <string.h>
#include <sys/time.h>
#include "face.h"
#include "../display.h"
#include "../weather.h"

#define C_GRIDV 0x0943   // #0D2A1A ECG grid
#define C_MINT 0x7FF8    // #7CFFC4 labels
#define C_PULSE 0xF949   // #FF2B4A
#define C_PULSEDIM 0x4862
#define C_RAIN 0x5DDF    // #5FB8FF
#define C_SNOW 0xE7FE    // #E6FFF0
#define C_STRIP 0x2B49   // #2A6A4A
#define C_TAGTXT 0x0083  // #021018

// Layout (240x240)
static const int TX = (240 - TIME_W) / 2, TY = 20;  // time sprite (HH:MM:SS)
static const int DATE_Y = 84, MS_X = 228;
static const int ECG_X0 = 12, ECG_W = 216, ECG_Y0 = 96, ECG_Y1 = 166, ECG_BASE = 134;
static const int ECG_GAP = 14;           // blank columns ahead of the sweep head
static const int SWEEP_PX_PER_S = 108;   // two beats across the screen
static const int STRIP_Y = 172, STRIP_H = 22, TAG_W = 58;
static const int LABEL_Y = 202, VALUE_Y = 214;
static const int HEART_X = 214, HEART_Y = 218;

// ECG shape for one beat (one per second), u = px into the beat (0..107).
static int wave(int u) {
  if (u >= 8 && u < 18) return (int)(-4 * sinf(3.1416f * (u - 8) / 10));    // P
  if (u >= 26 && u < 30) return -26 * (u - 26) / 4;                          // R up
  if (u >= 30 && u < 34) return -26 + 46 * (u - 30) / 4;                     // down past base
  if (u >= 34 && u < 37) return 20 - 20 * (u - 34) / 3;                      // S back
  if (u >= 50 && u < 70) return (int)(-7 * sinf(3.1416f * (u - 50) / 20));  // T
  return 0;
}

class Vitals : public Face {
 public:
  void begin() override {
    bg = spriteColor(C_BG_B);
    tft.fillScreen(bg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GREEN);
    tft.drawString("BIOMON v2 // SUBJECT: NIGHT CITY", 12, 6, 1);
    for (int x = ECG_X0; x < ECG_X0 + ECG_W; x++) clearColumn(x);
    tft.drawRect(12, STRIP_Y, 216, STRIP_H, C_STRIP);
    tft.setTextColor(C_MINT);
    tft.drawString("TEMP", 12, LABEL_Y, 1);
    tft.drawString("H / L", 72, LABEL_Y, 1);
    tft.drawString("HUM", 142, LABEL_Y, 1);
    tft.setTextColor(C_PULSE);
    tft.drawString("PULSE", 190, LABEL_Y, 1);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("60", 190, VALUE_Y, 2);  // one beat per second
    timeStr[0] = '\0';
    lastX = -1;
    heartOn = true;
    drawHeart(false);
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      TFT_eSprite& spr = timeSprite();
      spr.fillSprite(bg);
      spr.setTextDatum(MC_DATUM);
      spr.setTextColor(C_GREEN);
      spr.drawString(timeStr, TIME_W / 2, TIME_H / 2, 7);
      spr.pushSprite(TX, TY);
    }
    if (strcmp(s.date, prev.date) != 0) {
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(C_MINT, bg);
      tft.setTextPadding(160);
      tft.drawString(s.date, 12, DATE_Y, 2);
      tft.setTextPadding(0);
    }
    if (weatherChanged(s, prev)) drawWeather(s);
  }

  void tick(uint32_t ms) override {
    int msInSec = msNow(ms);
    drawMillis(msInSec);
    sweep(ms, msInSec);
    drawHeart(msInSec >= 250 && msInSec < 450);  // flash with the R peak
  }

 private:
  uint16_t bg = 0;
  char timeStr[9] = "";
  int lastX = -1, lastY = ECG_BASE;
  bool heartOn = false;

  // Milliseconds into the current second (wall clock once synced).
  int msNow(uint32_t ms) {
    if (!timeSynced()) return ms % 1000;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)(tv.tv_usec / 1000);
  }

  void drawMillis(int msInSec) {
    char buf[6];
    if (timeSynced()) snprintf(buf, sizeof(buf), ".%03d", msInSec);
    else strcpy(buf, ".---");
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_AMBER, bg);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, MS_X, DATE_Y, 2);
    tft.setTextPadding(0);
  }

  // Blank one ECG column and put its grid pixels back.
  void clearColumn(int x) {
    bool vGrid = ((x - ECG_X0) % 20) == 0;
    tft.drawFastVLine(x, ECG_Y0, ECG_Y1 - ECG_Y0, vGrid ? C_GRIDV : bg);
    if (!vGrid)
      for (int y = ECG_Y0 + 8; y < ECG_Y1; y += 20) tft.drawPixel(x, y, C_GRIDV);
  }

  // Advance the trace head to where it should be now, drawing every column on the way.
  void sweep(uint32_t ms, int msInSec) {
    int headX = ECG_X0 + (int)((ms * (uint32_t)SWEEP_PX_PER_S / 1000) % ECG_W);
    if (lastX < 0) lastX = headX;
    int steps = (headX - lastX + ECG_W) % ECG_W;
    int uNow = msInSec * SWEEP_PX_PER_S / 1000;  // px into the current beat
    for (int i = 1; i <= steps; i++) {
      int x = ECG_X0 + (lastX - ECG_X0 + i) % ECG_W;
      int u = ((uNow - (steps - i)) % SWEEP_PX_PER_S + SWEEP_PX_PER_S) % SWEEP_PX_PER_S;
      int y = ECG_BASE + wave(u);
      clearColumn(ECG_X0 + (x - ECG_X0 + ECG_GAP) % ECG_W);
      if (x == ECG_X0) tft.drawPixel(x, y, C_GREEN);  // wrapped: start a fresh line
      else tft.drawLine(x - 1, lastY, x, y, C_GREEN);
      lastY = y;
    }
    lastX = headX;
  }

  void drawHeart(bool on) {
    if (on == heartOn) return;
    heartOn = on;
    uint16_t c = on ? C_PULSE : C_PULSEDIM;
    tft.fillCircle(HEART_X + 3, HEART_Y + 3, 3, c);
    tft.fillCircle(HEART_X + 8, HEART_Y + 3, 3, c);
    tft.fillTriangle(HEART_X, HEART_Y + 4, HEART_X + 11, HEART_Y + 4, HEART_X + 5, HEART_Y + 10, c);
  }

  void drawWeather(const FaceState& s) {
    // today strip: coloured tag + rain chance/total
    uint16_t tagC = C_GREEN;
    if (!strcmp(s.outlook, "RAIN")) tagC = C_RAIN;
    else if (!strcmp(s.outlook, "SNOW")) tagC = C_SNOW;
    else if (!strcmp(s.outlook, "STORM")) tagC = C_AMBER;
    tft.fillRect(12, STRIP_Y, TAG_W, STRIP_H, tagC);
    tft.fillRect(12 + TAG_W, STRIP_Y + 1, 216 - TAG_W - 1, STRIP_H - 2, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TAGTXT);
    tft.drawString(s.outlook, 12 + TAG_W / 2, STRIP_Y + STRIP_H / 2 + 1, 2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(tagC);
    tft.drawString(s.weatherValid ? s.precip : "NO SIGNAL", 12 + TAG_W + 8, STRIP_Y + STRIP_H / 2 + 1, 2);

    // bottom readouts
    tft.fillRect(12, VALUE_Y, 176, 16, bg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GREEN);
    char buf[16];
    if (s.weatherValid) {
      snprintf(buf, sizeof(buf), "%d", s.temp);
      int x = 12 + tft.drawString(buf, 12, VALUE_Y, 2);
      tft.drawCircle(x + 3, VALUE_Y + 3, 2, C_GREEN);  // font 2 has no degree glyph
      char u[3] = {s.unit, s.stale ? '*' : '\0', '\0'};
      tft.drawString(u, x + 7, VALUE_Y, 2);
      snprintf(buf, sizeof(buf), "%d/%d", s.hi, s.lo);
      tft.drawString(buf, 72, VALUE_Y, 2);
      snprintf(buf, sizeof(buf), "%d%%", s.humidity);
      tft.drawString(buf, 142, VALUE_Y, 2);
    } else {
      tft.drawString("--", 12, VALUE_Y, 2);
      tft.drawString("--/--", 72, VALUE_Y, 2);
      tft.drawString("--", 142, VALUE_Y, 2);
    }
  }
};

static Vitals vitals;
Face* faceVitals() { return &vitals; }
