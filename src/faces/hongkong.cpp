#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "hk_glyphs.h"
#include "../display.h"
#include "../weather.h"

// ---- palette (RGB565) ----
#define H_BG 0x0821        // #0A0508 night
#define H_HAZE_MID 0x2845  // #2A0A28 city glow
#define H_HAZE_LOW 0x4887  // #4A1038
#define H_FAR 0x1063       // #170D1E skyline
#define H_SPIRE 0x20A5     // #20142A
#define H_FACADE 0x0822    // #0A0612 end of the street
#define H_WALL 0x0842      // #0E0816
#define H_LEDGE 0x1064     // #160E20 floor ledges
#define H_WINDARK 0x1885   // #1C1228 unlit window
#define H_AC 0x3948        // #3A2A40 AC units
#define H_TANK 0x18A4      // #1E1424 rooftop tanks
#define H_NET 0x1924       // scaffold netting
#define H_POLE 0xA408      // #A08040 bamboo
#define H_WIRE 0x5A4C      // #5A4A60 overhead wires
#define H_SPAN 0x3948      // span wires
#define H_STREET 0x0821    // #08040C
#define H_RAIL 0x3988      // #3A3040
#define H_GOLD 0xFE8F      // #FFD27A
#define H_WARM 0xFD8E      // #FFB070
#define H_CYANLT 0x7E9F    // #7FD0FF
#define H_PINK 0xFB75      // #FF6FA8
#define H_VIOLET 0xC31F    // #C060FF
#define H_RED 0xF949       // #FF2A4A
#define H_CYAN 0x073F      // #00E5FF
#define H_GREEN 0x3FED     // #39FF6A
#define H_MAGENTA 0xFA7B   // #FF4FD8
#define H_AMBER 0xFD80     // #FFB000
#define H_AMB_DEAD 0x59E0  // dead amber tube
#define H_F_RED 0x1821     // sign backings
#define H_F_GREEN 0x00A1
#define H_F_MAG 0x1021
#define H_F_AMB 0x1060
#define H_F_CYAN 0x0083
#define H_GLOW_RED 0x5083  // neon halos
#define H_GLOW_GREEN 0x1A84
#define H_GLOW_MAG 0x50C8
#define H_GLOW_AMB 0x51C0
#define H_GLOW_CYAN 0x024A
#define H_REF_RED 0x80C5   // wet-street reflections
#define H_REF_GREEN 0x2407
#define H_REF_AMB 0x82C1
#define H_REF_MAG 0x814E
#define H_REF_CYAN 0x03B1
#define H_PINKTXT 0xFA71   // #FF4F8A
#define H_DATE 0xA30F      // #A0607A
#define H_RAIN 0x4B10
#define H_SNOW 0xBDFA
#define H_S_WARM 0x28A0    // shopfronts: interior, lit window
#define H_S_WARMLT 0x8AA3
#define H_S_CYAN 0x0105
#define H_S_CYANLT 0x1BD1
#define H_S_MAG 0x2825
#define H_S_MAGLT 0x88EF
#define H_S_GRN 0x0101
#define H_S_GRNLT 0x1BC6
// taxi
#define X_RED 0xC885       // #C8102E
#define X_GLASS 0x2947     // #2A2A38
#define X_CREAM 0xEF19     // #E8E0C8
#define X_GREEN 0x3CC7     // #3A9A3A New Territories taxi
#define X_BLUE 0x2BDA      // #2A7AD0 Lantau taxi
// tram
#define T_GREEN 0x2BE9     // #2E7D4F
#define T_ROOF 0x1AC7      // #1C5A38
#define T_DOOR 0x1A46      // #1C4A30
#define T_FRONT 0x22E7     // #245F3E
#define T_SKIRT 0x0943     // #0F2A1C
#define T_LIT 0xFF35       // #FFE6A8
#define T_DARK 0x2947      // #2A2A38 unlit window
#define T_SIL 0x3946       // #3A2A30 passengers
#define T_SCREEN 0x9E5B    // #9FC8D8 windscreen
#define T_HEAD 0xFFBA      // #FFF6D0
#define T_POLE 0x8BD2      // #8A7A90
#define T_SPARK 0x7F5F     // #7FE8FF
#define T_BEAM 0x2904      // dim headlight beam
#define T_R_GREEN 0x1124   // tram reflection
#define T_R_RED 0x3843
#define T_R_CREAM 0x3985

// ---- layout ----
static const int BAND_Y = 124, BAND_END = 214;  // tram band, repainted while it passes
static const int STRIP_H = 12;                  // painted in strips through one small sprite
static const int TRAM_LEN = 108;
static const int TRAM_SPEED = 40;  // px/s: a slow ding ding, ~9 s a pass, starting each minute
static const int TRAM_START = -TRAM_LEN;
static const int TRAM_RUN_MS = (240 - TRAM_START) * 1000 / TRAM_SPEED;
static const int HUD_Y = 214;
static const int BOX_W = 16, BOX_H = 18;  // small redraw box for drops, lights, flicker
static const int NO_TRAM = -9999;
static const int DROPS = 14;

// Scene state the drawing reads, so every repaint agrees with what is on screen.
static bool lightOn[5];
static bool jiaOn = true;  // 家 on the 酒家 sign sputters
static int reflJ = 0;      // reflection wobble
static int tramX = NO_TRAM;
static int taxiX = NO_TRAM;  // a taxi drives by between trams, never alongside one
static uint16_t taxiColor = X_RED;
static bool taxiRight = true;  // heading left to right
static const int TAXI_Y = 196, TAXI_LEN = 36, TAXI_SPEED = 110;
static const int TAXI_RUN_MS = (240 + TAXI_LEN + 24) * 1000 / TAXI_SPEED;
static uint8_t precip = 0;  // 0 none, 1 rain, 2 snow
static float dropX[DROPS], dropY[DROPS];

static const int16_t LIGHTS[5][2] = {{56, 71}, {81, 70}, {162, 68}, {119, 52}, {56, 57}};

namespace {  // file-local: chongqing.cpp has its own Canvas

// Everything the scene draws goes through this, so the same code paints the
// whole screen once and repaints small areas through a sprite.
struct Canvas {
  TFT_eSPI& g;
  int dx, dy, y0, y1;  // screen point (x, y) maps to (x + dx, y + dy); rows outside [y0, y1) skipped
  bool hits(int y, int h) const { return y < y1 && y + h > y0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (hits(y, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void hline(int x, int y, int w, uint16_t c) { rect(x, y, w, 1, c); }
  void vline(int x, int y, int h, uint16_t c) { rect(x, y, 1, h, c); }
  void frame(int x, int y, int w, int h, uint16_t c) {
    hline(x, y, w, c); hline(x, y + h - 1, w, c); vline(x, y, h, c); vline(x + w - 1, y, h, c);
  }
  void line(int x0, int y0_, int x1, int y1_, uint16_t c) {
    if (hits(min(y0_, y1_), abs(y1_ - y0_) + 1)) g.drawLine(x0 + dx, y0_ + dy, x1 + dx, y1_ + dy, c);
  }
  void dot(int x, int y, int r, uint16_t c) {
    if (hits(y - r, 2 * r + 1)) g.fillCircle(x + dx, y + dy, r, c);
  }
  void ring(int x, int y, int r, uint16_t c) {
    if (hits(y - r, 2 * r + 1)) g.drawCircle(x + dx, y + dy, r, c);
  }
  void tri(int x0, int y0_, int x1, int y1_, int x2, int y2_, uint16_t c) {
    int lo = min(y0_, min(y1_, y2_)), hi = max(y0_, max(y1_, y2_));
    if (hits(lo, hi - lo + 1)) g.fillTriangle(x0 + dx, y0_ + dy, x1 + dx, y1_ + dy, x2 + dx, y2_ + dy, c);
  }
  void glyph(int idx, int x, int y, uint16_t c) {
    if (hits(y, 16)) g.drawBitmap(x + dx, y + dy, HK16[idx], 16, 16, c);
  }
  void glyph12(int idx, int x, int y, uint16_t c) {
    if (hits(y, 12)) g.drawBitmap(x + dx, y + dy, HK12[idx], 12, 12, c);
  }
  void text(const char* s, int x, int y, uint16_t c) {  // font 1, transparent
    if (!hits(y, 8)) return;
    g.setTextDatum(TL_DATUM);
    g.setTextColor(c);
    g.drawString(s, x + dx, y + dy, 1);
  }
};

}  // namespace

static uint32_t cellHash(int x, int y) {  // stable pseudo-random per cell
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  return h ^ (h >> 13);
}

// Skyline behind the street: x, width, top.
static const int16_t SKY[][3] = {
    {0, 12, 90}, {13, 13, 90}, {27, 14, 85}, {42, 9, 75}, {51, 10, 73}, {63, 13, 90}, {77, 8, 72},
    {86, 11, 88}, {98, 10, 79}, {109, 10, 85}, {119, 9, 90}, {128, 14, 77}, {143, 14, 89}, {157, 11, 70},
    {170, 10, 85}, {180, 12, 88}, {194, 12, 87}, {207, 12, 71}, {219, 10, 71}, {229, 8, 90}, {237, 3, 94}};

static uint16_t lerp565(uint16_t a, uint16_t b, int t, int n) {
  int r = ((a >> 11) * (n - t) + (b >> 11) * t) / n;
  int g = (((a >> 5) & 63) * (n - t) + ((b >> 5) & 63) * t) / n;
  int bl = ((a & 31) * (n - t) + (b & 31) * t) / n;
  return (r << 11) | (g << 5) | bl;
}

static int wallY(int x, float t) {  // floor line t (0 top .. 1 bottom) on the left wall at x
  float a = 52 + 154 * t, b = 98 + 70 * t;
  return (int)(a + (b - a) * x / 84);
}

static void neonBox(Canvas& cv, int x, int y, int w, int h, uint16_t tube, uint16_t glow, uint16_t fill) {
  cv.frame(x - 2, y - 2, w + 4, h + 4, glow);
  cv.frame(x - 1, y - 1, w + 2, h + 2, glow);
  cv.rect(x, y, w, h, fill);
  cv.frame(x, y, w, h, tube);
}

static void drawScene(Canvas& cv) {
  cv.rect(0, 0, 240, 240, H_BG);

  // city glow behind the skyline
  for (int y = max(48, cv.y0); y < min(170, cv.y1); y++) {
    uint16_t c = y < 115 ? lerp565(H_BG, H_HAZE_MID, y - 48, 67) : lerp565(H_HAZE_MID, H_HAZE_LOW, y - 115, 55);
    cv.hline(0, y, 240, c);
  }

  // skyline with scattered lit windows
  static const uint16_t SKYWIN[4] = {H_GOLD, H_GOLD, H_CYANLT, H_PINK};
  for (auto& t : SKY) {
    int x = t[0], w = t[1], top = t[2];
    cv.rect(x, top, w, 172 - top, H_FAR);
    for (int wy = top + 3; wy < 112; wy += 4) {
      if (!cv.hits(wy, 1)) continue;
      for (int wx = x + 2; wx < x + w - 1; wx += 3) {
        uint32_t h = cellHash(wx, wy);
        if (h % 25 < 4) cv.rect(wx, wy, 1, 1, SKYWIN[(h >> 5) & 3]);
      }
    }
  }
  // the spire at the end of the street
  cv.rect(114, 62, 13, 50, H_SPIRE);
  cv.rect(117, 56, 7, 6, H_SPIRE);
  cv.tri(114, 62, 117, 56, 117, 62, H_SPIRE);
  cv.tri(123, 56, 126, 62, 123, 62, H_SPIRE);
  cv.vline(115, 64, 46, H_CYAN);
  cv.vline(125, 64, 46, H_CYAN);
  cv.rect(119, 54, 2, 2, H_AC);

  // building wall closing the street
  cv.rect(84, 98, 72, 70, H_FACADE);
  static const uint16_t FACWIN[4] = {H_GOLD, H_WARM, H_CYANLT, H_PINK};
  for (int wy = 102; wy < 166; wy += 5) {  // sparse, so the signs in front stay the focus
    if (!cv.hits(wy, 2)) continue;
    for (int wx = 87; wx < 154; wx += 4) {
      uint32_t h = cellHash(wx, wy);
      cv.rect(wx, wy, 2, 2, h % 7 == 0 ? FACWIN[(h >> 4) & 3] : H_LEDGE);
    }
  }

  // street walls in perspective, filled row by row
  for (int y = max(52, cv.y0); y < min(206, cv.y1); y++) {
    int w = y < 98 ? (y - 52) * 84 / 46 : (y <= 168 ? 84 : (206 - y) * 84 / 38);
    cv.hline(0, y, w, H_WALL);
    cv.hline(240 - w, y, w, H_WALL);
  }
  static const uint8_t XS[6] = {0, 26, 46, 62, 74, 84};
  static const uint16_t WALLWIN[6] = {H_GOLD, H_GOLD, H_WARM, H_CYANLT, H_VIOLET, H_PINK};
  for (int side = 0; side < 2; side++) {
    auto X = [side](int x) { return side ? 240 - x : x; };
    for (int i = 0; i < 5; i++) {
      int xa = XS[i], xb = XS[i + 1], cw = xb - xa, mid = (xa + xb) / 2;
      // upper floors: a ledge per floor, two windows per bay, AC boxes under dark ones
      for (int j = 0; j < 7; j++) {
        int top = wallY(mid, j / 9.0f), ch = wallY(mid, (j + 1) / 9.0f) - top;
        cv.line(X(xa), wallY(xa, (j + 1) / 9.0f), X(xb), wallY(xb, (j + 1) / 9.0f), H_LEDGE);
        for (int k = 0; k < 2; k++) {
          int ww = max(2, cw * 32 / 100), wh = max(1, ch * 45 / 100);
          int wx = xa + cw * (k ? 56 : 12) / 100, wy = top + ch / 4;
          int x = side ? 240 - wx - ww : wx;
          uint32_t h = cellHash(i * 13 + k * 5 + side * 71, j * 17 + 1) % 20;
          cv.rect(x, wy, ww, wh, h < 9 ? WALLWIN[h % 6] : H_WINDARK);
          if (h >= 9 && h < 12) cv.rect(x, wy + wh + 1, max(2, ww * 7 / 10), max(1, ch / 7), H_AC);
        }
      }
      // street level: lit shopfronts under neon awnings
      static const uint16_t SHOP[4][3] = {{H_S_WARM, H_S_WARMLT, H_AMBER},
                                          {H_S_CYAN, H_S_CYANLT, H_CYAN},
                                          {H_S_MAG, H_S_MAGLT, H_MAGENTA},
                                          {H_S_GRN, H_S_GRNLT, H_GREEN}};
      const uint16_t* c = SHOP[(i + side * 2 + (i >> 1)) & 3];
      int a0 = wallY(xa, 0.8f), b0 = wallY(xb, 0.8f), a1 = wallY(xa, 1), b1 = wallY(xb, 1);
      cv.tri(X(xa), a0, X(xb), b0, X(xb), b1, c[0]);
      cv.tri(X(xa), a0, X(xb), b1, X(xa), a1, c[0]);
      int ga = wallY(xa, 0.85f), gb = wallY(xb, 0.85f), la = wallY(xa, 0.96f), lb = wallY(xb, 0.96f);
      int ia = xa + 1, ib = xb - 1;  // glowing window, inset from the pillars
      cv.tri(X(ia), ga, X(ib), gb, X(ib), lb, c[1]);
      cv.tri(X(ia), ga, X(ib), lb, X(ia), la, c[1]);
      cv.line(X(xa), a0, X(xb), b0, c[2]);
      cv.line(X(xa), a0 + 1, X(xb), b0 + 1, c[2]);
      cv.line(X(xa), a0, X(xa), a1, H_WALL);
    }
  }
  cv.vline(84, 98, 70, H_CYAN);  // neon trim where the street walls end
  cv.vline(155, 98, 70, H_MAGENTA);

  // rooftops: tanks and antennas
  cv.rect(14, 52, 8, 8, H_TANK);
  cv.rect(40, 62, 6, 6, H_TANK);
  cv.rect(214, 54, 9, 7, H_TANK);
  cv.vline(56, 58, 12, H_AC);
  cv.vline(190, 60, 12, H_AC);
  cv.hline(187, 63, 7, H_AC);

  // bamboo scaffolding with green netting
  cv.tri(168, 92, 200, 75, 200, 106, H_NET);
  cv.tri(168, 92, 200, 106, 168, 106, H_NET);
  for (int x = 170; x <= 198; x += 7) {
    int top = 53 + (240 - x) * 46 / 84;
    cv.vline(x, top, 106 - top, H_POLE);
  }
  for (int k = 0; k < 3; k++) cv.line(168, 95 + 5 * k, 200, 78 + 7 * k, H_POLE);
  cv.line(168, 104, 200, 78, H_POLE);

  // aviation lights (they blink)
  for (int i = 0; i < 5; i++)
    if (lightOn[i]) cv.rect(LIGHTS[i][0], LIGHTS[i][1], i == 3 ? 2 : 1, 2, H_RED);

  // signs further down the street, lit, smaller with distance
  static const struct { uint8_t x, y, w, h; uint16_t tube, glow; } FAR_SIGNS[] = {
      {64, 108, 12, 7, H_GREEN, H_GLOW_GREEN},  {66, 120, 7, 14, H_AMBER, H_GLOW_AMB},
      {74, 126, 8, 6, H_MAGENTA, H_GLOW_MAG},   {75, 136, 6, 10, H_CYAN, H_GLOW_CYAN},
      {78, 150, 5, 4, H_RED, H_GLOW_RED},       {163, 112, 8, 14, H_MAGENTA, H_GLOW_MAG},
      {161, 128, 10, 6, H_CYAN, H_GLOW_CYAN},   {158, 138, 8, 5, H_AMBER, H_GLOW_AMB},
      {158, 146, 5, 8, H_GREEN, H_GLOW_GREEN}, {157, 156, 6, 4, H_MAGENTA, H_GLOW_MAG}};
  for (auto& f : FAR_SIGNS) {
    cv.rect(f.x, f.y, f.w, f.h, f.glow);
    cv.frame(f.x, f.y, f.w, f.h, f.tube);
    if (f.h > f.w) cv.vline(f.x + f.w / 2, f.y + 2, f.h - 4, f.tube);  // vertical "text"
    else cv.hline(f.x + 2, f.y + f.h / 2, f.w - 4, f.tube);
  }

  // span wires behind the signs
  cv.line(84, 120, 156, 120, H_SPAN);
  cv.line(0, 96, 84, 120, H_SPAN);
  cv.line(240, 96, 156, 120, H_SPAN);

  // 香港 hanging at the end of the street
  neonBox(cv, 102, 79, 36, 20, H_CYAN, H_GLOW_CYAN, H_F_CYAN);
  cv.glyph(HK_XIANG, 104, 81, H_CYAN);
  cv.glyph(HK_GANG, 120, 81, H_CYAN);

  // pawnshop: the round 押 sign on a bracket
  cv.vline(10, 56, 40, H_WIRE);
  cv.rect(10, 73, 7, 2, H_WIRE);
  cv.dot(34, 74, 19, H_GLOW_RED);
  cv.dot(34, 74, 17, H_RED);
  cv.dot(34, 74, 15, H_F_RED);
  cv.ring(34, 74, 13, H_GLOW_RED);
  cv.glyph(HK_YA, 26, 66, H_RED);

  // 茶餐廳 cafe
  neonBox(cv, 12, 98, 78, 20, H_GREEN, H_GLOW_GREEN, H_F_GREEN);
  cv.glyph(HK_CHA, 19, 100, H_GREEN);
  cv.glyph(HK_CAN, 43, 100, H_GREEN);
  cv.glyph(HK_TING, 67, 100, H_GREEN);

  // 麻雀 24HR mahjong parlour
  neonBox(cv, 205, 54, 26, 56, H_MAGENTA, H_GLOW_MAG, H_F_MAG);
  cv.glyph(HK_MA, 210, 58, H_MAGENTA);
  cv.glyph(HK_QUE, 210, 77, H_MAGENTA);
  cv.text("24HR", 207, 98, H_MAGENTA);

  // 酒家 restaurant
  neonBox(cv, 140, 108, 60, 19, H_AMBER, H_GLOW_AMB, H_F_AMB);
  cv.glyph(HK_JIU, 152, 110, H_GOLD);
  cv.glyph(HK_JIA, 172, 110, jiaOn ? H_GOLD : H_AMB_DEAD);

  // tram wire
  int px = 0, py = 126;
  for (int x = 8; x <= 240; x += 8) {
    float u = (x - 120) / 120.0f;
    int y = 126 + (int)(6 * (1 - u * u));
    cv.line(px, py, x, y, H_WIRE);
    px = x; py = y;
  }

  // street and rails
  for (int y = max(168, cv.y0); y < min(HUD_Y, cv.y1); y++) {
    int w = y < 206 ? (206 - y) * 84 / 38 : 0;
    cv.hline(w, y, 240 - 2 * w, H_STREET);
  }
  cv.line(98, 168, 44, 213, H_RAIL);
  cv.line(104, 168, 62, 213, H_RAIL);
  cv.line(136, 168, 178, 213, H_RAIL);
  cv.line(142, 168, 196, 213, H_RAIL);

  // neon on the wet street
  static const struct { uint8_t x; uint8_t y; uint16_t c; } REF[] = {
      {30, 200, H_REF_RED}, {34, 200, H_REF_RED}, {48, 200, H_REF_GREEN}, {60, 200, H_REF_GREEN},
      {170, 200, H_REF_AMB}, {176, 200, H_REF_AMB}, {218, 200, H_REF_MAG}, {120, 172, H_REF_CYAN}};
  for (auto& r : REF) {
    for (int k = 0; k < 4; k++) {
      int j = (k & 1) ? reflJ : -reflJ;
      cv.rect(r.x + j, r.y + k * 3 + (k == 3), (k & 1) ? 2 : 3, 2, r.c);
    }
  }
}

static void drawTram(Canvas& cv, int x0) {
  int x1 = x0 + TRAM_LEN;
  // headlight beam on the road, pole up to the wire with a spark
  cv.tri(x1, 189, x1 + 44, 184, x1 + 44, 198, T_BEAM);
  cv.tri(x1, 189, x1 + 44, 198, x1, 193, T_BEAM);
  int cx = x0 + 58;
  float u = (cx - 120) / 120.0f;
  int cy = 126 + (int)(6 * (1 - u * u));
  cv.line(x0 + 30, 140, cx, cy, T_POLE);
  cv.line(x0 + 31, 140, cx + 1, cy, T_POLE);
  cv.rect(x0 + 26, 138, 8, 3, T_SKIRT);
  if (random(3)) {
    cv.hline(cx - 2, cy, 5, T_SPARK);
    cv.vline(cx, cy - 2, 5, T_SPARK);
    cv.rect(cx, cy, 1, 1, TFT_WHITE);
  }

  // body: roof with route and destination boards
  cv.rect(x0 + 3, 140, TRAM_LEN - 6, 2, T_ROOF);
  cv.rect(x0 + 1, 142, TRAM_LEN - 1, 53, T_GREEN);
  cv.rect(x0 + 4, 142, 14, 9, TFT_BLACK);
  cv.text("88", x0 + 5, 143, H_AMBER);
  cv.rect(x0 + 40, 142, 58, 9, TFT_BLACK);
  cv.text("HAPPY VLY", x0 + 42, 143, H_AMBER);

  // upper deck: cream band, windows, passengers
  cv.rect(x0 + 1, 151, TRAM_LEN - 1, 15, X_CREAM);
  static const uint8_t UP_LIT = 0b10101101, UP_SIL = 0b10001001;
  for (int i = 0; i < 8; i++) {
    int wx = x0 + 3 + i * 12;
    bool lit = UP_LIT & (1 << i);
    cv.rect(wx, 153, 10, 11, lit ? T_LIT : T_DARK);
    if (lit && (UP_SIL & (1 << i))) {
      cv.dot(wx + 5, 158, 2, T_SIL);
      cv.rect(wx + 2, 160, 7, 4, T_SIL);
    }
  }
  cv.hline(x0 + 1, 165, TRAM_LEN - 1, T_GREEN);

  // advert band: 叮叮 DING DING
  cv.rect(x0 + 1, 166, TRAM_LEN - 1, 13, X_RED);
  cv.hline(x0 + 1, 166, TRAM_LEN - 1, H_PINKTXT);
  cv.hline(x0 + 1, 178, TRAM_LEN - 1, H_PINKTXT);
  cv.glyph12(HK_DING, x0 + 6, 166, H_GOLD);
  cv.glyph12(HK_DING, x0 + 18, 166, H_GOLD);
  cv.text("DING DING", x0 + 34, 169, TFT_WHITE);

  // lower deck: rear entry, windows, front exit
  static const uint8_t LO_LIT = 0b01101, LO_SIL = 0b00101;
  for (int i = 0; i < 5; i++) {
    int wx = x0 + 16 + i * 12;
    bool lit = LO_LIT & (1 << i);
    cv.rect(wx, 181, 10, 9, lit ? T_LIT : T_DARK);
    if (lit && (LO_SIL & (1 << i))) {
      cv.dot(wx + 5, 185, 1, T_SIL);
      cv.rect(wx + 3, 187, 5, 3, T_SIL);
    }
  }
  cv.rect(x0 + 3, 180, 10, 15, T_DOOR);
  cv.rect(x0 + 4, 181, 8, 8, T_DARK);
  cv.vline(x0 + 8, 180, 15, T_SKIRT);
  cv.rect(x0 + 78, 180, 12, 15, T_DOOR);
  cv.rect(x0 + 79, 181, 10, 8, T_LIT);
  cv.vline(x0 + 84, 180, 15, T_SKIRT);

  // rounded front with windscreens, lights
  cv.rect(x1 - 8, 142, 8, 53, T_FRONT);
  cv.rect(x1 - 7, 153, 6, 11, T_SCREEN);
  cv.rect(x1 - 7, 181, 6, 9, T_SCREEN);
  cv.rect(x1 - 2, 191, 2, 2, T_HEAD);
  cv.rect(x0 + 1, 191, 2, 2, H_RED);
  cv.vline(x0, 143, 52, T_ROOF);

  // skirt, bogies, reflection on the wet road
  cv.rect(x0, 195, TRAM_LEN, 4, T_SKIRT);
  cv.rect(x0 + 14, 199, 10, 2, TFT_BLACK);
  cv.rect(x0 + 82, 199, 10, 2, TFT_BLACK);
  cv.rect(x0, 202, TRAM_LEN, 3, T_R_GREEN);
  cv.rect(x0, 205, TRAM_LEN, 2, T_R_RED);
  cv.rect(x0 + 3, 207, TRAM_LEN - 12, 2, T_R_CREAM);
}

static void drawPrecip(Canvas& cv) {
  for (int i = 0; i < DROPS && precip; i++) {
    int x = (int)dropX[i], y = (int)dropY[i];
    if (precip == 1) cv.line(x, y, x - 3, y + 8, H_RAIN);
    else cv.rect(x, y, 2, 2, H_SNOW);
  }
}

static void drawTaxi(Canvas& cv, int x) {  // x = left end of the body
  int y = TAXI_Y;
  auto R = [x](int dx, int w) { return taxiRight ? x + TAXI_LEN - dx - w : x + dx; };  // dx from the nose
  int nose = taxiRight ? x + TAXI_LEN : x, ahead = taxiRight ? 22 : -22;
  cv.tri(nose, y + 9, nose + ahead, y + 6, nose + ahead, y + 15, T_BEAM);
  cv.rect(R(16, 8), y, 8, 3, TFT_WHITE);  // TAXI roof sign
  cv.rect(R(12, 14), y + 3, 14, 2, taxiColor);
  cv.rect(R(6, 26), y + 5, 26, 3, taxiColor);
  cv.rect(R(10, 19), y + 5, 19, 2, X_GLASS);
  cv.rect(x, y + 8, TAXI_LEN, 7, taxiColor);
  cv.hline(x, y + 9, TAXI_LEN, X_CREAM);
  cv.dot(R(6, 1), y + 15, 2, TFT_BLACK);
  cv.dot(R(29, 1), y + 15, 2, TFT_BLACK);
  cv.rect(R(0, 2), y + 10, 2, 2, H_GOLD);
  cv.rect(R(TAXI_LEN - 2, 2), y + 10, 2, 2, H_RED);
}

static void drawAll(Canvas& cv) {
  drawScene(cv);
  if (tramX != NO_TRAM) drawTram(cv, tramX);
  if (taxiX != NO_TRAM) drawTaxi(cv, taxiX);
  drawPrecip(cv);
}

class HongKong : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band.setColorDepth(16);
    bandOk = band.createSprite(240, STRIP_H) != nullptr;  // 5.8 KB, fits fragmented heap
    box.setColorDepth(16);
    boxOk = box.createSprite(BOX_W, BOX_H) != nullptr;

    tramX = taxiX = NO_TRAM;
    precip = 0;
    jiaOn = true;
    for (bool& b : lightOn) b = false;
    Canvas cv{tft, 0, 0, 0, 240};
    drawScene(cv);
    drawHud();
    timeStr[0] = '\0';
    tramShown = taxiShown = false;
    shownNext = -1;
  }

  void end() override {
    band.deleteSprite();
    box.deleteSprite();
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(H_GOLD, H_BG);
      tft.setTextPadding(tft.textWidth("88:88:88", 4));
      tft.drawString(timeStr, 10, 6, 4);
      tft.setTextPadding(0);
    }
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 92, textWidth2);
      tft.fillRect(10, 34, 94, 16, H_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(H_PINKTXT, H_BG);
      tft.drawString(city, 10, 34, 2);
    }
    if (strcmp(s.date, prev.date) != 0) {
      char d[12];
      snprintf(d, sizeof(d), "%.10s", s.date);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(H_DATE, H_BG);
      tft.setTextPadding(tft.textWidth("WWW 88 WWW", 1));
      tft.drawString(d, 104, 41, 1);
      tft.setTextPadding(0);
    }
    if (weatherChanged(s, prev) || s.code != prev.code) {
      drawWeather(s);
      CnWeather w = cnWeather(s.code);
      uint8_t p = !s.weatherValid ? 0 : (w == CNW_RAIN || w == CNW_THUNDER) ? 1 : (w == CNW_SNOW ? 2 : 0);
      if (p != precip) setPrecip(p);
    }
  }

  void tick(uint32_t ms) override {
    drawMillis();
    int msIntoMinute = msIntoMin(ms);
    bool running = msIntoMinute < TRAM_RUN_MS;
    tramX = running ? TRAM_START + msIntoMinute * TRAM_SPEED / 1000 : NO_TRAM;
    updateTaxi(msIntoMinute);

    int oldX[DROPS], oldY[DROPS];
    bool respawned[DROPS] = {};
    if (precip) stepPrecip(oldX, oldY, respawned);

    bool bandPainted = false;
    if (running || tramShown) {
      repaintRows(BAND_Y, BAND_END);
      tramShown = running;
      bandPainted = true;
    }
    if (!bandPainted && (taxiX != NO_TRAM || taxiShown)) repaintRows(TAXI_Y - 2, BAND_END);
    taxiShown = taxiX != NO_TRAM;
    if (precip) {
      for (int i = 0; i < DROPS; i++) {
        if (respawned[i]) {  // clear the old spot, then draw at the new one
          paintBox(oldX[i] - 8, oldY[i] - 1, bandPainted);
          paintBox((int)dropX[i] - 8, (int)dropY[i] - 1, bandPainted);
        } else {
          paintBox(oldX[i] - 8, oldY[i] - 1, bandPainted);
        }
      }
    }

    drawNext(msIntoMinute);
    blinkLights(ms);
    flickerNeon(ms);
    if (ms - lastShimmer >= 250) {
      lastShimmer = ms;
      reflJ = (int)random(-1, 2);
      if (!bandPainted) repaintRows(170, BAND_END);
    }
  }

 private:
  TFT_eSprite band = TFT_eSprite(&tft);
  TFT_eSprite box = TFT_eSprite(&tft);
  bool bandOk = false, boxOk = false;
  char timeStr[9] = "";
  bool tramShown = false, taxiShown = false;
  int shownNext = -1;
  uint32_t lastShimmer = 0, nextFlicker = 0;

  // Taxis pass at :15, :25, :35 and :45, well clear of the tram (:00 to :09),
  // alternating left to right and right to left.
  void updateTaxi(int msIntoMinute) {
    static const uint16_t COLORS[4] = {X_RED, X_GREEN, X_RED, X_BLUE};
    taxiX = NO_TRAM;
    for (int i = 0; i < 4; i++) {
      int t = msIntoMinute - (15000 + i * 10000);
      if (t >= 0 && t < TAXI_RUN_MS) {
        int d = t * TAXI_SPEED / 1000;
        taxiRight = i % 2 == 0;
        taxiX = taxiRight ? -(TAXI_LEN + 24) + d : 240 - d;
        taxiColor = COLORS[i];
      }
    }
  }

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // Repaint screen rows [ya, yb) through the strip sprite.
  void repaintRows(int ya, int yb) {
    if (!bandOk) return;
    for (int sy = ya; sy < yb; sy += STRIP_H) {
      int h = min(STRIP_H, yb - sy);
      Canvas cv{band, 0, -sy, sy, sy + h};
      drawAll(cv);
      band.pushSprite(0, sy, 0, 0, 240, h);
    }
  }

  // Repaint one small box (clamped below the header and above the HUD).
  void paintBox(int bx, int by, bool skipInBand) {
    if (!boxOk) return;
    by = constrain(by, 51, HUD_Y - BOX_H);
    if (skipInBand && by >= BAND_Y && by + BOX_H <= BAND_END) return;  // band repaint covered it
    Canvas cv{box, -bx, -by, by, by + BOX_H};
    drawAll(cv);
    box.pushSprite(bx, by);
  }

  void spawnDrop(int i, bool anywhere) {
    dropX[i] = random(0, 256);
    dropY[i] = anywhere ? random(52, 196) : random(52, 64);
  }

  void setPrecip(uint8_t p) {
    precip = p;
    for (int i = 0; i < DROPS; i++) spawnDrop(i, true);
    repaintRows(51, HUD_Y);  // clears the old drops or flakes
  }

  void stepPrecip(int* oldX, int* oldY, bool* respawned) {
    for (int i = 0; i < DROPS; i++) {
      oldX[i] = (int)dropX[i];
      oldY[i] = (int)dropY[i];
      if (precip == 1) {
        dropX[i] -= 2.6f;
        dropY[i] += 7;
      } else {
        dropX[i] += random(-1, 2) * 0.5f;
        dropY[i] += 1.2f;
      }
      if (dropY[i] > 196 || dropX[i] < 0) {
        spawnDrop(i, false);
        respawned[i] = true;
      }
    }
  }

  void drawMillis() {
    char buf[6] = ".---";
    if (timeSynced()) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      snprintf(buf, sizeof(buf), ".%03d", (int)(tv.tv_usec / 1000));
    }
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(H_PINKTXT, H_BG);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, 14 + tft.textWidth("88:88:88", 4), 12, 2);
    tft.setTextPadding(0);
  }

  void drawHud() {
    Canvas cv{tft, 0, 0, 0, 240};
    // 天氣 weather chip
    tft.drawRect(178, 6, 54, 44, H_RED);
    tft.fillRect(178, 6, 54, 14, H_RED);
    cv.glyph12(HK_TIAN, 193, 7, H_BG);
    cv.glyph12(HK_QI, 205, 7, H_BG);

    // bottom HUD: 電車 tag, route, countdown
    tft.fillRect(0, HUD_Y, 240, 240 - HUD_Y, H_BG);
    tft.drawRect(6, HUD_Y, 228, 18, H_RED);
    tft.fillRect(6, HUD_Y, 38, 18, H_RED);
    cv.glyph(HK_DIAN, 9, HUD_Y + 1, H_BG);
    cv.glyph(HK_CHE, 25, HUD_Y + 1, H_BG);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(H_GOLD, H_BG);
    tft.drawString("88 HAPPY VALLEY", 50, HUD_Y + 9, 1);
  }

  void drawWeather(const FaceState& s) {
    tft.fillRect(179, 20, 52, 29, H_BG);
    tft.setTextDatum(TL_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(H_CYANLT);
      tft.drawString("NO SIG", 187, 30, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    Canvas cv{tft, 0, 0, 0, 240};
    cv.glyph(HK_SUNNY + w, 182, 22, H_CYANLT);  // weather glyphs are in CnWeather order
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", s.temp);
    tft.setTextColor(TFT_WHITE);
    int x = 202 + tft.drawString(buf, 202, 21, 2);
    tft.drawCircle(x + 2, 23, 1, TFT_WHITE);
    char u[2] = {s.unit, '\0'};
    tft.drawString(u, x + 5, 21, 2);
    tft.setTextColor(H_CYANLT);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(cnWeatherLabel(w), 205, 40, 1);
  }

  void drawNext(int msIntoMinute) {
    int next = msIntoMinute < TRAM_RUN_MS ? 0 : 60 - msIntoMinute / 1000;
    if (next == shownNext) return;
    shownNext = next;
    char buf[16];
    if (next == 0) strcpy(buf, "DING DING");
    else snprintf(buf, sizeof(buf), "NEXT 00:%02d", next);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(H_PINKTXT, H_BG);
    tft.setTextPadding(tft.textWidth("NEXT 00:88", 1));
    tft.drawString(buf, 230, HUD_Y + 9, 1);
    tft.setTextPadding(0);
  }

  // Aircraft-warning lights blink out of step.
  void blinkLights(uint32_t ms) {
    for (int i = 0; i < 5; i++) {
      bool on = ((ms + i * 410) % 1800) < 600;
      if (on == lightOn[i]) continue;
      lightOn[i] = on;
      paintBox(LIGHTS[i][0] - 8, LIGHTS[i][1] - 8, false);
    }
  }

  // 家 on the 酒家 sign is a failing tube.
  void flickerNeon(uint32_t ms) {
    if ((int32_t)(ms - nextFlicker) < 0) return;
    bool on = random(6) != 0;
    nextFlicker = ms + (on ? 400 + random(1500) : 70 + random(120));
    if (on == jiaOn) return;
    jiaOn = on;
    paintBox(172, 110, false);  // 16x18 box holds the glyph
  }
};

static HongKong hongkong;
Face* faceHongKong() { return &hongkong; }
