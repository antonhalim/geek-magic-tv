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
static const int SWEEP_PX_PER_S = 108;
static const int HEAD_LEN = 18;          // bright "phosphor" tip of the trace
#define C_HEAD 0xE7FE                    // #E6FFF0
static const int STRIP_Y = 172, STRIP_H = 22, TAG_W = 58;
static const int LABEL_Y = 202, VALUE_Y = 214;
static const int HEART_X = 214, HEART_Y = 218;

// One heartbeat, t = ms since the beat started. QRS width stays fixed while the
// beat length varies, like a real trace. Ectopic beats: no P wave, wide QRS, inverted T.
static float beatShape(int t, int amp, bool ectopic) {
  const float PI_F = 3.14159f;
  if (ectopic) {
    if (t >= 200 && t < 260) return -amp * (t - 200) / 60.0f;
    if (t >= 260 && t < 340) return -amp + 1.6f * amp * (t - 260) / 80.0f;
    if (t >= 340 && t < 400) return 0.6f * amp * (1 - (t - 340) / 60.0f);
    if (t >= 470 && t < 720) return 0.3f * amp * sinf(PI_F * (t - 470) / 250);
    return 0;
  }
  if (t >= 80 && t < 180) return -4 * sinf(PI_F * (t - 80) / 100);       // P
  if (t >= 230 && t < 250) return 3.0f * (t - 230) / 20;                  // Q dip
  if (t >= 250 && t < 280) return 3 - (amp + 3) * (t - 250) / 30.0f;      // R up
  if (t >= 280 && t < 310) return -amp + 1.75f * amp * (t - 280) / 30.0f; // down past base
  if (t >= 310 && t < 340) return 0.75f * amp * (1 - (t - 310) / 30.0f);  // S back
  if (t >= 440 && t < 640) return -0.27f * amp * sinf(PI_F * (t - 440) / 200);  // T
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
    timeStr[0] = '\0';
    lastX = -1;
    for (int i = 0; i < ECG_W; i++) colY[i] = -1;
    bpm = 72;
    shownBpm = -1;
    nextBeat(millis());
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
    drawMillis(msNow(ms));
    sweep(ms);
    uint32_t sinceBeat = ms - beatStart;
    drawHeart(sinceBeat >= 250 && sinceBeat < 420);  // flash with the R peak
    drawBpm();
  }

 private:
  uint16_t bg = 0;
  char timeStr[9] = "";
  int lastX = -1, lastY = ECG_BASE;
  int16_t colY[ECG_W];  // last drawn y per column, -1 = blank
  bool heartOn = false;
  // current beat
  uint32_t beatStart = 0;
  int beatLen = 833, amp = 26;
  bool ectopic = false, pauseNext = false;
  int bpm = 72, shownBpm = -1;

  // Pick the next beat: heart rate drifts 62-92, height varies, ~1 in 14 is ectopic
  // and is followed by a longer compensatory pause.
  void nextBeat(uint32_t at) {
    beatStart = at;
    bpm += (int)random(-3, 4);
    if (bpm < 62) bpm = 62;
    if (bpm > 92) bpm = 92;
    beatLen = 60000 / bpm;
    amp = 22 + (int)random(0, 9);
    ectopic = !pauseNext && random(14) == 0;
    if (pauseNext) beatLen = beatLen * 3 / 2;
    pauseNext = ectopic;
    if (ectopic) beatLen = beatLen * 3 / 4;  // comes early
  }

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
  // Each column gets the trace value at the moment the head passed it.
  void sweep(uint32_t ms) {
    int headX = ECG_X0 + (int)((ms * (uint32_t)SWEEP_PX_PER_S / 1000) % ECG_W);
    if (lastX < 0) lastX = headX;
    int steps = (headX - lastX + ECG_W) % ECG_W;
    for (int i = 1; i <= steps; i++) {
      int x = ECG_X0 + (lastX - ECG_X0 + i) % ECG_W;
      uint32_t tc = ms - (uint32_t)((steps - i) * 1000 / SWEEP_PX_PER_S);
      while ((int32_t)(tc - beatStart) >= beatLen) nextBeat(beatStart + beatLen);
      float wander = 3 * sinf(6.2832f * (tc % 7000) / 7000.0f);
      int noise = random(3) == 0 ? (int)random(-1, 2) : 0;
      int y = ECG_BASE + (int)(beatShape((int)(tc - beatStart), amp, ectopic) + wander) + noise;
      if (y < ECG_Y0 + 1) y = ECG_Y0 + 1;
      if (y > ECG_Y1 - 2) y = ECG_Y1 - 2;

      int ahead = ECG_X0 + (x - ECG_X0 + ECG_GAP) % ECG_W;
      clearColumn(ahead);
      colY[ahead - ECG_X0] = -1;
      if (x == ECG_X0) tft.drawPixel(x, y, C_HEAD);  // wrapped: start a fresh line
      else tft.drawLine(x - 1, lastY, x, y, C_HEAD);
      colY[x - ECG_X0] = (int16_t)y;
      lastY = y;

      // the tip fades: repaint the segment HEAD_LEN behind in normal green
      int tail = (x - ECG_X0 - HEAD_LEN + ECG_W) % ECG_W;
      int prevTail = (tail - 1 + ECG_W) % ECG_W;
      if (tail != 0 && colY[tail] >= 0 && colY[prevTail] >= 0)
        tft.drawLine(ECG_X0 + prevTail, colY[prevTail], ECG_X0 + tail, colY[tail], C_GREEN);
    }
    lastX = headX;
  }

  void drawBpm() {
    if (bpm == shownBpm) return;
    shownBpm = bpm;
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", bpm);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_PULSE, bg);
    tft.setTextPadding(tft.textWidth("188", 2));
    tft.drawString(buf, 190, VALUE_Y, 2);
    tft.setTextPadding(0);
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
