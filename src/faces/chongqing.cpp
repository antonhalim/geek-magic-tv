#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "cjk_glyphs.h"
#include "../display.h"
#include "../weather.h"

// ---- palette (RGB565) ----
#define Q_BG 0x0021        // #07040D night
#define Q_SKY 0x0823       // #0B0618 haze band behind the towers
#define Q_FAR 0x1044       // #110A20 distant towers
#define Q_FARWIN 0x1886    // #1A1230
#define Q_MID 0x1886       // #1C1132 mid towers
#define Q_MIDWIN 0x28E9    // #2A1C48
#define Q_TOWER 0x20A8     // #221640 central tower
#define Q_TOWERWIN 0x312B  // #33245A
#define Q_FLOOR 0x290A     // #2E2050 floor lines
#define Q_MAST 0x394A      // #3A2A55 antennas, cornices
#define Q_RED 0xF949       // #FF2A4A
#define Q_REDDIM 0x58A4    // #5A1520 dead neon tube
#define Q_CYAN 0x073F      // #00E5FF
#define Q_CYANLT 0x7FBF    // #7FF4FF
#define Q_MAGENTA 0xFA7B   // #FF4FD8
#define Q_GOLD 0xFE8F      // #FFD27A lit windows
#define Q_AMBER 0xFD88     // #FFB347 Hongyadong lights
#define Q_STATION 0x1061   // #140C08 station interior
#define Q_PILLAR 0x28C2    // #2A1A10
#define Q_BEAM 0x52AD      // #55556A track beam
#define Q_BEAMTOP 0x8C54   // #8A8AA2
#define Q_SUPPORT 0x422B   // #45455A
#define Q_HILL 0x0842      // #0D0816
#define Q_RIVER 0x0001     // #05030B
#define Q_ROOF 0x38E2      // #3A1E10 Hongyadong eaves
#define Q_WALL 0x28A1      // #2A1408
#define Q_CABLE 0x5A4F     // #5A4A7A
#define Q_FOG 0xCDDF       // #C8B8FF
// train
#define T_BODY 0xE75E      // #E6E8F0
#define T_GLASS 0x1928     // #1C2440 window band
#define T_LIT 0xFF99       // #FFF2C8 cabin light
#define T_DOOR 0x9D17      // #9AA0B8
#define T_STRIPE 0xE9C7    // #E83A3A
#define T_TEAL 0x065C      // #00C8E0
#define T_ROOF 0xBDF9      // #B8BCCC
#define T_SKIRT 0x2967     // #2C2C3C
#define T_SKIRTTOP 0x4A4B  // #4A4A5E
#define T_GAP 0x39CA       // #3A3A50
#define T_SHIELD 0x10C5    // #141A2C
#define T_HEAD 0xFFBA      // #FFF6D0
#define T_SPEED 0x4C73     // dim cyan speed lines
#define T_BEAM 0x39A5      // dim warm headlight beam

// ---- layout ----
static const int BAND_Y = 132, BAND_H = 36;  // train band, redrawn while a train passes
static const int STRIP_H = 12;               // painted in strips through one small sprite
static const int TRACK_Y = 162;
static const int TRAIN_LEN = 81;
static const int TRAIN_SPEED = 64;  // px/s; a pass takes ~5.5 s, starting each minute
static const int TRAIN_RUN_MS = (240 + TRAIN_LEN + 40) * 1000 / TRAIN_SPEED;
static const int HUD_Y = 214;
static const int CAB_W = 20, CAB_H = 18;  // cable-car redraw box

// Everything the scene draws goes through this, so the same code paints the
// whole screen once and repaints just the train band into a sprite.
struct Canvas {
  TFT_eSPI& g;
  int dx, dy, y0, y1;  // screen point (x, y) maps to (x + dx, y + dy); rows outside [y0, y1) skipped
  bool hits(int y, int h) const { return y < y1 && y + h > y0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (hits(y, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void hline(int x, int y, int w, uint16_t c) { rect(x, y, w, 1, c); }
  void vline(int x, int y, int h, uint16_t c) { rect(x, y, 1, h, c); }
  void line(int x0, int y0_, int x1, int y1_, uint16_t c) {
    if (hits(min(y0_, y1_), abs(y1_ - y0_) + 1)) g.drawLine(x0 + dx, y0_ + dy, x1 + dx, y1_ + dy, c);
  }
  void dot(int x, int y, int r, uint16_t c) {
    if (hits(y - r, 2 * r + 1)) g.fillCircle(x + dx, y + dy, r, c);
  }
  void tri(int x0, int y0_, int x1, int y1_, int x2, int y2_, uint16_t c) {
    int lo = min(y0_, min(y1_, y2_)), hi = max(y0_, max(y1_, y2_));
    if (hits(lo, hi - lo + 1)) g.fillTriangle(x0 + dx, y0_ + dy, x1 + dx, y1_ + dy, x2 + dx, y2_ + dy, c);
  }
  void glyph(int idx, int x, int y, uint16_t c) {
    if (hits(y, 16)) g.drawBitmap(x + dx, y + dy, CJK16[idx], 16, 16, c);
  }
};

// Stable pseudo-random "is this window lit" per cell.
static uint8_t litKind(int x, int y) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  h ^= h >> 13;
  return h % 13;  // 0 = gold, 1 = cyan, else dark
}

// Window grid: base colour plus a window per cell, some lit.
static void windows(Canvas& cv, int x, int y, int w, int h, uint16_t base, uint16_t win, int cw, int ch,
                    int ww, int wh, bool floorLines) {
  cv.rect(x, y, w, h, base);
  for (int cy = y; cy + wh + 1 <= y + h; cy += ch) {
    if (!cv.hits(cy, ch)) continue;
    for (int cx = x; cx + ww + 1 <= x + w; cx += cw) {
      uint8_t k = litKind(cx, cy);
      cv.rect(cx + 1, cy + 1, ww, wh, k == 0 ? Q_GOLD : (k == 1 ? Q_CYANLT : win));
    }
    if (floorLines) cv.hline(x, cy + ch - 1, w, Q_FLOOR);
  }
}

static int hillTop(int x) {  // gentle hillside the towers stand on
  float t;
  if (x < 80) { t = x / 80.0f; return (int)((1 - t) * (1 - t) * 196 + 2 * (1 - t) * t * 182 + t * t * 188); }
  if (x < 160) { t = (x - 80) / 80.0f; return (int)((1 - t) * (1 - t) * 188 + 2 * (1 - t) * t * 194 + t * t * 184); }
  t = (x - 160) / 80.0f;
  return (int)((1 - t) * (1 - t) * 184 + 2 * (1 - t) * t * 172 + t * t * 178);
}

static void drawScene(Canvas& cv) {
  cv.rect(0, 0, 240, 240, Q_BG);
  cv.rect(0, 56, 240, 44, Q_SKY);
  cv.line(140, 62, 240, 98, Q_CABLE);  // Yangtze cable car line

  // distant towers
  windows(cv, 0, 150, 60, 50, Q_FAR, Q_FARWIN, 4, 5, 2, 1, false);
  windows(cv, 14, 92, 12, 58, Q_FAR, Q_FARWIN, 4, 5, 2, 1, false);
  cv.tri(14, 92, 20, 88, 26, 92, Q_FAR);
  windows(cv, 40, 80, 8, 70, Q_FAR, Q_FARWIN, 4, 5, 2, 1, false);
  cv.tri(40, 80, 44, 76, 48, 80, Q_FAR);
  windows(cv, 146, 74, 18, 126, Q_FAR, Q_FARWIN, 4, 5, 2, 1, false);
  windows(cv, 164, 120, 12, 80, Q_FAR, Q_FARWIN, 4, 5, 2, 1, false);
  windows(cv, 200, 92, 20, 108, Q_FAR, Q_FARWIN, 4, 5, 2, 1, false);
  cv.tri(200, 92, 210, 86, 220, 92, Q_FAR);
  cv.vline(20, 76, 12, Q_MAST);
  cv.vline(158, 58, 16, Q_MAST);
  cv.vline(210, 74, 12, Q_MAST);

  for (int x = 0; x < 240; x += 4) cv.rect(x, hillTop(x), 4, 214 - hillTop(x), Q_HILL);

  // left apartment block: cornice, balconies with AC units
  windows(cv, 4, 112, 38, 78, Q_MID, Q_MIDWIN, 6, 6, 3, 2, false);
  cv.rect(2, 108, 42, 4, Q_MIDWIN);
  for (int y = 128; y <= 176; y += 16) cv.hline(4, y, 38, Q_MAST);
  cv.rect(2, 124, 5, 3, Q_MIDWIN); cv.rect(2, 140, 5, 3, Q_MIDWIN); cv.rect(2, 156, 5, 3, Q_MIDWIN);
  cv.rect(39, 132, 5, 3, Q_MIDWIN); cv.rect(39, 148, 5, 3, Q_MIDWIN);
  cv.rect(30, 100, 8, 8, Q_MIDWIN);
  cv.vline(34, 96, 4, Q_MAST);

  // hotpot tower with stepped crown and a vertical sign
  windows(cv, 46, 84, 32, 112, Q_TOWER, Q_TOWERWIN, 5, 7, 3, 3, true);
  windows(cv, 52, 78, 20, 6, Q_TOWER, Q_TOWERWIN, 5, 7, 3, 3, false);
  cv.rect(56, 70, 12, 8, Q_TOWER);
  cv.rect(58, 66, 8, 4, Q_FLOOR);
  cv.vline(62, 52, 14, Q_MAST);
  cv.rect(47, 88, 20, 52, Q_SKY);
  cv.rect(47, 88, 20, 1, Q_MAGENTA); cv.rect(47, 139, 20, 1, Q_MAGENTA);
  cv.rect(47, 88, 1, 52, Q_MAGENTA); cv.rect(66, 88, 1, 52, Q_MAGENTA);
  cv.glyph(CJK_HUO, 49, 92, Q_MAGENTA);
  cv.glyph(CJK_GUO, 49, 110, Q_MAGENTA);

  // central tower: three-step crown, antenna cluster, neon edges, station cut through it
  windows(cv, 88, 70, 52, 130, Q_TOWER, Q_TOWERWIN, 5, 7, 3, 3, true);
  windows(cv, 94, 62, 40, 8, Q_TOWER, Q_TOWERWIN, 5, 7, 3, 3, false);
  windows(cv, 100, 54, 28, 8, Q_TOWER, Q_TOWERWIN, 5, 7, 3, 3, false);
  cv.rect(104, 51, 20, 3, Q_MIDWIN);  // low roof plant: the time and date sit just above
  cv.hline(94, 70, 6, Q_MAGENTA); cv.hline(128, 70, 6, Q_MAGENTA);
  cv.vline(94, 62, 8, Q_MAGENTA); cv.vline(133, 62, 8, Q_MAGENTA);
  cv.hline(94, 62, 6, Q_MAGENTA); cv.hline(128, 62, 6, Q_MAGENTA);
  cv.vline(100, 54, 8, Q_MAGENTA); cv.vline(127, 54, 8, Q_MAGENTA);
  cv.hline(100, 54, 28, Q_MAGENTA);
  cv.rect(88, 70, 2, 130, Q_CYAN);
  cv.rect(138, 70, 2, 130, Q_MAGENTA);
  cv.rect(88, 128, 52, 34, Q_STATION);  // Liziba station
  cv.rect(88, 128, 52, 3, Q_AMBER);
  cv.rect(96, 131, 3, 31, Q_PILLAR);
  cv.rect(128, 131, 3, 31, Q_PILLAR);
  cv.hline(88, 127, 52, Q_MAGENTA);
  cv.hline(88, 162, 52, Q_MAGENTA);

  // right tower with setbacks and the station sign
  windows(cv, 182, 118, 54, 78, Q_MID, Q_MIDWIN, 6, 6, 3, 2, false);
  windows(cv, 188, 112, 48, 6, Q_MID, Q_MIDWIN, 6, 6, 3, 2, false);
  windows(cv, 196, 104, 30, 8, Q_MID, Q_MIDWIN, 6, 6, 3, 2, false);
  cv.hline(182, 130, 54, Q_MAST); cv.hline(182, 146, 54, Q_MAST); cv.hline(182, 178, 54, Q_MAST);
  cv.rect(208, 94, 14, 10, Q_TOWER);
  cv.rect(211, 90, 8, 4, Q_FLOOR);
  cv.rect(216, 114, 20, 58, Q_SKY);
  cv.rect(216, 114, 20, 1, Q_RED); cv.rect(216, 171, 20, 1, Q_RED);
  cv.rect(216, 114, 1, 58, Q_RED); cv.rect(235, 114, 1, 58, Q_RED);
  cv.glyph(CJK_ZHONG, 218, 117, Q_RED);
  cv.glyph(CJK_QING, 218, 135, Q_RED);
  cv.glyph(CJK_ZHAN, 218, 153, Q_REDDIM);

  // holographic billboard frame (the glyph is drawn by the face, it flickers)
  cv.rect(146, 94, 34, 35, Q_SKY);
  for (int x = 146; x < 180; x += 5) { cv.hline(x, 94, 3, Q_CYAN); cv.hline(x, 128, 3, Q_CYAN); }
  for (int y = 94; y < 129; y += 5) { cv.vline(146, y, 3, Q_CYAN); cv.vline(179, y, 3, Q_CYAN); }

  // monorail beam on T-shaped supports
  cv.rect(0, TRACK_Y, 240, 6, Q_BEAM);
  cv.hline(0, TRACK_Y, 240, Q_BEAMTOP);
  cv.rect(22, 168, 18, 3, Q_SUPPORT); cv.rect(29, 171, 4, 29, Q_SUPPORT);
  cv.rect(194, 168, 18, 3, Q_SUPPORT); cv.rect(201, 171, 4, 29, Q_SUPPORT);

  // Hongyadong: stacked stilt-house eaves with warm windows
  for (int i = 0; i < 2; i++) {
    int y = 178 + i * 10, inset = i * 2;
    cv.rect(7 + inset, y, 58 - 2 * inset, 4, Q_ROOF);
    cv.tri(2 + inset, y + 4, 7 + inset, y, 7 + inset, y + 4, Q_ROOF);
    cv.tri(70 - inset, y + 4, 65 - inset, y, 65 - inset, y + 4, Q_ROOF);
    cv.hline(2 + inset, y, 68 - 2 * inset, Q_AMBER);
    cv.rect(6 + inset, y + 4, 60 - 2 * inset, i ? 10 : 6, Q_WALL);
    for (int x = 10 + inset; x < 62 - inset; x += 8) cv.rect(x, y + 6, 4, i ? 3 : 2, Q_AMBER);
  }

  // bridge arch
  int px = 146, py = 204;
  for (int i = 1; i <= 16; i++) {
    float t = i / 16.0f;
    int x = (int)((1 - t) * (1 - t) * 146 + 2 * (1 - t) * t * 193 + t * t * 240);
    int y = (int)((1 - t) * (1 - t) * 204 + 2 * (1 - t) * t * 174 + t * t * 200);
    cv.line(px, py, x, y, Q_MAGENTA);
    px = x; py = y;
  }
  cv.hline(140, 204, 100, Q_MAGENTA);
  cv.vline(170, 190, 14, Q_REDDIM); cv.vline(193, 189, 15, Q_REDDIM); cv.vline(216, 191, 13, Q_REDDIM);

  cv.rect(0, 205, 240, HUD_Y - 205, Q_RIVER);
}

static void drawTrain(Canvas& cv, int n) {  // n = x of the nose tip
  int x0 = n - TRAIN_LEN;
  for (int i = 0; i < 3; i++) cv.line(x0 - 34 + i * 6, 141 + i * 5, x0 - 3, 141 + i * 5, T_SPEED);
  cv.line(n + 2, 150, n + 38, 141, T_BEAM);
  cv.line(n + 2, 152, n + 38, 152, T_BEAM);
  cv.line(n + 2, 154, n + 38, 162, T_BEAM);

  cv.rect(x0, 139, 26, 20, T_BODY);
  cv.rect(x0 + 27, 139, 26, 20, T_BODY);
  for (int y = 139; y < 159; y++) {  // sloped nose
    int w = y < 150 ? 16 + (y - 139) : 27;
    cv.hline(n - 27, y, w, T_BODY);
  }
  cv.tri(n - 12, 141, n - 3, 150, n - 10, 150, T_SHIELD);
  cv.rect(x0 + 3, 142, 20, 5, T_GLASS);
  cv.rect(x0 + 30, 142, 20, 5, T_GLASS);
  cv.rect(n - 24, 142, 10, 5, T_GLASS);
  cv.rect(x0 + 4, 143, 4, 2, T_LIT); cv.rect(x0 + 12, 143, 5, 2, T_LIT);
  cv.rect(x0 + 31, 143, 5, 2, T_LIT); cv.rect(x0 + 40, 143, 6, 2, T_LIT);
  cv.rect(n - 23, 143, 5, 2, T_LIT);
  for (int dx : {8, 18, 35, 45, 62}) cv.vline(x0 + dx, 141, 15, T_DOOR);
  cv.rect(x0, 150, TRAIN_LEN, 2, T_STRIPE);
  cv.hline(x0, 152, TRAIN_LEN, T_TEAL);
  cv.rect(x0 + 6, 136, 8, 3, T_ROOF);
  cv.rect(x0 + 33, 136, 10, 3, T_ROOF);
  cv.rect(n - 23, 136, 7, 3, T_ROOF);
  cv.vline(x0 + 26, 140, 19, T_GAP);
  cv.vline(x0 + 53, 140, 19, T_GAP);
  cv.rect(x0, 156, TRAIN_LEN, 8, T_SKIRT);
  cv.hline(x0, 156, TRAIN_LEN, T_SKIRTTOP);
  cv.rect(n - 5, 153, 4, 2, T_HEAD);
  cv.rect(x0, 153, 2, 2, Q_RED);
}

class Chongqing : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the train band needs that RAM; other faces re-create it
    band.setColorDepth(16);
    bandOk = band.createSprite(240, STRIP_H) != nullptr;  // 5.8 KB, fits fragmented heap
    cab.setColorDepth(16);
    cabOk = cab.createSprite(CAB_W, CAB_H) != nullptr;

    Canvas cv{tft, 0, 0, 0, 240};
    drawScene(cv);
    drawHud();
    timeStr[0] = '\0';
    trainShown = false;
    shownNext = -1;
    holoOn = false;
    drawHolo(true);
    zhanOn = false;  // the scene drew it dim
    for (bool& b : mastOn) b = false;
    lastCabX = -1;
  }

  void end() override {
    band.deleteSprite();
    cab.deleteSprite();
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(Q_CYANLT, Q_BG);
      tft.setTextPadding(tft.textWidth("88:88:88", 4));
      tft.drawString(timeStr, 10, 6, 4);
      tft.setTextPadding(0);
    }
    if (strcmp(s.city, prev.city) != 0) {  // your city, not Chongqing: that lives on the signs
      char city[48];
      fitLabel(s.city, city, sizeof(city), 92, textWidth2);
      tft.fillRect(10, 34, 94, 16, Q_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(Q_MAGENTA, Q_BG);
      tft.drawString(city, 10, 34, 2);
    }
    if (strcmp(s.date, prev.date) != 0) {
      char d[12];
      snprintf(d, sizeof(d), "%.10s", s.date);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(Q_MAGENTA, Q_BG);
      tft.setTextPadding(tft.textWidth("WWW 88 WWW", 1));
      tft.drawString(d, 104, 41, 1);
      tft.setTextPadding(0);
    }
    if (weatherChanged(s, prev) || s.code != prev.code) drawWeather(s);
  }

  void tick(uint32_t ms) override {
    drawMillis();
    int msIntoMinute = msIntoMin(ms);
    runTrain(msIntoMinute);
    drawNext(msIntoMinute);
    moveCableCar(ms);
    blinkMasts(ms);
    flickerNeon(ms);
    if (ms - lastShimmer >= 250) {
      lastShimmer = ms;
      shimmer();
    }
  }

 private:
  TFT_eSprite band = TFT_eSprite(&tft);
  TFT_eSprite cab = TFT_eSprite(&tft);
  bool bandOk = false, cabOk = false;
  char timeStr[9] = "";
  bool trainShown = false;
  int shownNext = -1;
  bool holoOn = false, zhanOn = false;
  bool mastOn[3] = {};
  uint32_t lastShimmer = 0, nextFlicker = 0;
  int lastCabX = -1;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  void drawMillis() {
    char buf[6] = ".---";
    if (timeSynced()) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      snprintf(buf, sizeof(buf), ".%03d", (int)(tv.tv_usec / 1000));
    }
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Q_MAGENTA, Q_BG);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, 14 + tft.textWidth("88:88:88", 4), 12, 2);
    tft.setTextPadding(0);
  }

  void drawHud() {
    Canvas cv{tft, 0, 0, 0, 240};
    // ATMOS chip
    tft.drawRect(178, 6, 54, 44, Q_CYAN);
    tft.fillRect(178, 6, 54, 10, Q_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Q_BG);
    tft.drawString("ATMOS", 205, 11, 1);

    // bottom HUD: line tag, station, countdown
    tft.fillRect(0, HUD_Y, 240, 240 - HUD_Y, Q_BG);
    tft.drawRect(6, HUD_Y, 228, 18, Q_CYAN);
    tft.fillRect(6, HUD_Y, 38, 18, Q_CYAN);
    tft.setTextColor(Q_BG);
    tft.drawString("LINE 2", 25, HUD_Y + 9, 1);
    cv.glyph(CJK_LI, 48, HUD_Y + 1, Q_CYANLT);
    cv.glyph(CJK_ZI, 64, HUD_Y + 1, Q_CYANLT);
    cv.glyph(CJK_BA, 80, HUD_Y + 1, Q_CYANLT);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(Q_CYANLT, Q_BG);
    tft.drawString("LIZIBA", 100, HUD_Y + 9, 1);
  }

  void drawWeather(const FaceState& s) {
    tft.fillRect(179, 16, 52, 33, Q_BG);
    tft.setTextDatum(TL_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(Q_FOG);
      tft.drawString("NO SIG", 184, 26, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    Canvas cv{tft, 0, 0, 0, 240};
    cv.glyph(CJK_SUNNY + w, 182, 18, Q_FOG);  // weather glyphs are in CnWeather order
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", s.temp);
    int x = 202 + tft.drawString(buf, 202, 19, 2);
    tft.drawCircle(x + 2, 21, 1, TFT_WHITE);
    char u[2] = {s.unit, '\0'};
    tft.setTextColor(TFT_WHITE);
    tft.drawString(u, x + 5, 19, 2);
    tft.setTextColor(Q_FOG);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(cnWeatherLabel(w), 205, 38, 1);
  }

  // Train passes once a minute, starting at :00.
  void runTrain(int msIntoMinute) {
    bool running = msIntoMinute < TRAIN_RUN_MS;
    if (!bandOk || (!running && !trainShown)) return;
    int nose = -10 + msIntoMinute * TRAIN_SPEED / 1000;
    for (int sy = BAND_Y; sy < BAND_Y + BAND_H; sy += STRIP_H) {
      Canvas cv{band, 0, -sy, sy, sy + STRIP_H};
      drawScene(cv);
      if (running) drawTrain(cv, nose);
      band.pushSprite(0, sy);
    }
    trainShown = running;
    if (!running) zhanOn = false;  // repaint restored the dim tube
  }

  void drawNext(int msIntoMinute) {
    int next = msIntoMinute < TRAIN_RUN_MS ? 0 : 60 - msIntoMinute / 1000;
    if (next == shownNext) return;
    shownNext = next;
    char buf[16];
    if (next == 0) strcpy(buf, "ARRIVING");
    else snprintf(buf, sizeof(buf), "NEXT 00:%02d", next);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(Q_GOLD, Q_BG);
    tft.setTextPadding(tft.textWidth("NEXT 00:88", 1));
    tft.drawString(buf, 230, HUD_Y + 9, 1);
    tft.setTextPadding(0);
  }

  // Gondola glides along the cable and back every 40 s.
  void moveCableCar(uint32_t ms) {
    if (!cabOk) return;
    float t = (ms % 40000) / 20000.0f;
    if (t > 1) t = 2 - t;
    int cx = 150 + (int)(t * 76);
    if (cx == lastCabX) return;  // moves ~4 px/s: only repaint on a new position
    lastCabX = cx;
    int cy = 62 + (cx - 140) * 36 / 100;  // on the cable
    int bx = cx - CAB_W / 2, by = cy - 4;
    Canvas cv{cab, -bx, -by, by, by + CAB_H};
    drawScene(cv);  // clipped to this small box
    TFT_eSprite& g = cab;
    int gx = CAB_W / 2 - 6, gy = 4;
    g.drawFastVLine(CAB_W / 2, 1, 3, Q_CABLE);
    g.fillRoundRect(gx, gy, 13, 10, 2, 0xD947);
    g.fillRect(gx + 2, gy + 2, 9, 4, 0xFF14);
    g.fillRect(gx, gy + 8, 13, 2, 0x88A4);
    g.pushSprite(bx, by);
  }

  // Aircraft-warning lights on the masts blink out of step.
  void blinkMasts(uint32_t ms) {
    static const int16_t P[3][2] = {{20, 75}, {158, 57}, {62, 51}};
    for (int i = 0; i < 3; i++) {
      bool on = ((ms + i * 370) % 1600) < 500;
      if (on == mastOn[i]) continue;
      mastOn[i] = on;
      uint16_t bgc = (P[i][1] >= 56 && P[i][1] < 100) ? Q_SKY : Q_BG;
      tft.fillCircle(P[i][0], P[i][1], 1, on ? Q_RED : bgc);
    }
  }

  // The 站 tube is mostly dead and sputters; the hologram glitches now and then.
  void flickerNeon(uint32_t ms) {
    if ((int32_t)(ms - nextFlicker) < 0) return;
    bool z = random(5) == 0;
    drawZhan(z);
    bool h = random(9) != 0;
    drawHolo(h);
    nextFlicker = ms + (z || !h ? 90 : 300 + random(900));
  }

  void drawZhan(bool on) {
    if (on == zhanOn) return;
    zhanOn = on;
    tft.fillRect(218, 153, 16, 16, Q_SKY);
    tft.drawBitmap(218, 153, CJK16[CJK_ZHAN], 16, 16, on ? Q_RED : Q_REDDIM);
  }

  // 渝 at double size with every other row lit: a scanline hologram.
  void drawHolo(bool on) {
    if (on == holoOn) return;
    holoOn = on;
    tft.fillRect(147, 95, 32, 32, Q_SKY);
    if (!on) return;
    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 16; x++) {
        uint8_t b = pgm_read_byte(&CJK16[CJK_YU][y * 2 + x / 8]);
        if (b & (0x80 >> (x % 8))) tft.fillRect(147 + 2 * x, 95 + 2 * y, 2, 1, Q_CYANLT);
      }
    }
  }

  // Neon reflections on the river wobble.
  void shimmer() {
    tft.fillRect(0, 206, 240, HUD_Y - 207, Q_RIVER);
    int j = (int)random(-2, 3);
    tft.fillRect(87 + j, 207, 2, 5, Q_CYAN);
    tft.fillRect(138 - j, 207, 2, 6, Q_MAGENTA);
    tft.fillRect(225, 207, 2, 4 + random(3), Q_RED);
    tft.fillRect(18 + j, 207, 40, 1, Q_AMBER);
    tft.fillRect(24 - j, 210, 28 + random(6), 1, Q_AMBER);
    tft.fillRect(150 - j, 207, 80, 1, Q_MAGENTA);
    tft.fillRect(164 + j, 210, 50 + random(8), 1, Q_MAGENTA);
  }
};

static Chongqing chongqing;
Face* faceChongqing() { return &chongqing; }
