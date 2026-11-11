#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "jp_glyphs.h"
#include "../display.h"
#include "../weather.h"

// ---- palette (RGB565) ----
#define S_BG 0x0022        // #070614 night
#define S_SKY_MID 0x1888   // #1A1240
#define S_SKY_LOW 0x38CA   // #3A1850
#define S_FAR 0x1066       // #140F30 skyline
#define S_WARM 0xFF14      // #FFE0A0 lit windows
#define S_COOL 0xA6DF      // #A0D8FF
#define S_PINKLT 0xFCFC    // #FF9FE6
#define S_TOWER 0xFAC3     // #FF5A1A Tokyo Tower
#define S_TOWERLT 0xFF18   // #FFE0C0 its decks
#define S_LATTICE 0xFE94   // #FFD0A0
#define S_BLDG 0x0844      // #0E0A24
#define S_BLDG2 0x1065     // #120C2A round tower
#define S_WINDARK 0x18A8   // #1C1640
#define S_LEDGE 0x20E8     // #241C44
#define S_FRAME 0x2949     // #2A2A4A screen frame
#define S_GROUND 0x0843    // #0A0818
#define S_ROAD 0x1084      // #141226
#define S_ZEBRA 0x5ACF     // #5A5A7A
#define S_ZEBRAFAR 0x4A4D  // #4A4A6A
#define S_POLE 0x39CB      // #3A3A5A
#define S_SIGBOX 0x0021    // #05040E
#define S_RED 0xF949       // #FF2A4A
#define S_AMBER 0xFD80     // #FFB000
#define S_MAGENTA 0xFA7B   // #FF4FD8
#define S_CYAN 0x073F      // #00E5FF
#define S_BLUE 0x39DF      // #3A3AFF
#define S_GREEN 0x3FED     // #39FF6A
#define S_WHITE 0xFF5E     // #FFE8F6
#define S_F_RED 0x1821     // sign backings
#define S_F_AMB 0x1060
#define S_F_MAG 0x1021
#define S_GLOW_RED 0x5084  // neon halos
#define S_GLOW_AMB 0x51C1
#define S_GLOW_MAG 0x50E9
#define S_DEAD_MAG 0x58C9  // dead tube
#define S_R_MAG 0x796E     // wet-street reflections
#define S_R_BLUE 0x2131
#define S_R_RED 0x78E6
#define S_R_AMB 0x7AC2
#define S_R_CYAN 0x0B91
#define S_R_ORANGE 0x7984
#define S_RAIN 0x4AAF
#define S_SNOW 0xBDFA
#define S_SIGOFF 0x0943    // unlit signal lamps
#define S_SIGREDOFF 0x2842
// crowd
#define S_SHADOW 0x0021
#define S_CLEAR 0x424C     // clear umbrella over the road
#define S_CLEAREDGE 0x8C95
#define S_DARKUMB 0x18E5
#define S_UMBHI 0x422A
#define S_NAVY 0x3A51
#define S_YELLOW 0xFE8F
#define S_CRIMSON 0xC885
#define S_COAT1 0x2948
#define S_COAT2 0x49CB
#define S_COAT3 0x8BCD
#define S_COAT4 0xCE5B
// cars
#define S_CARNAVY 0x1949   // #1D2A4A taxi
#define S_CARWHITE 0xDEFD
#define S_CARGLASS 0x2947
#define S_BEAM 0x3146

// ---- layout and timing ----
static const int CROSS_Y = 150, CROSS_END = 214;  // crossing, repainted while people or cars move
static const int STRIP_H = 12;
static const int HUD_Y = 214;
static const int BOX_W = 16, BOX_H = 18;
static const int WALK_MS = 20000;   // green from :00, blinking for the last 4 s
static const int BLINK_MS = 16000;
static const int WALKERS = 30;
static const int DROPS = 14;
static const int NO_CAR = -9999;
static const int CAR_Y = 170, CAR_LEN = 34, CAR_SPEED = 110;
static const int CAR_RUN_MS = (240 + CAR_LEN + 24) * 1000 / CAR_SPEED;
static const int SCREEN_X = 16, SCREEN_Y = 62, SCREEN_W = 42, SCREEN_H = 24;

// Scene state the drawing reads, so every repaint agrees with what is on screen.
static int crowdMs = 0;        // ms into the walk phase (clamped)
static bool crowdOdd = false;  // direction flips every minute
static uint8_t walkLight = 0;     // 0 red, 1 green, 2 green blink off
static int carX = NO_CAR;
static bool carRight = true;
static uint16_t carColor = S_CARNAVY;
static uint8_t screenMode = 0;
static bool lightOn[2];
static bool keOn = true;  // ケ on the カラオケ sign sputters
static int reflJ = 0;
static uint8_t precip = 0;  // 0 none, 1 rain, 2 snow
static float dropX[DROPS], dropY[DROPS];
static uint8_t screenGlyph = JP_RAIN;  // current weather, for the video screen
static char screenTemp[8] = "";

static const int16_t LIGHTS[2][2] = {{119, 53}, {199, 57}};  // Tokyo Tower beacon, round tower roof

namespace {  // file-local: other faces have their own Canvas

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
  void ellipse(int x, int y, int rx, int ry, uint16_t c) {
    if (hits(y - ry, 2 * ry + 1)) g.fillEllipse(x + dx, y + dy, rx, ry, c);
  }
  void ellipseEdge(int x, int y, int rx, int ry, uint16_t c) {
    if (hits(y - ry, 2 * ry + 1)) g.drawEllipse(x + dx, y + dy, rx, ry, c);
  }
  void tri(int x0, int y0_, int x1, int y1_, int x2, int y2_, uint16_t c) {
    int lo = min(y0_, min(y1_, y2_)), hi = max(y0_, max(y1_, y2_));
    if (hits(lo, hi - lo + 1)) g.fillTriangle(x0 + dx, y0_ + dy, x1 + dx, y1_ + dy, x2 + dx, y2_ + dy, c);
  }
  void quad(int x0, int y0_, int x1, int y1_, int x2, int y2_, int x3, int y3_, uint16_t c) {
    tri(x0, y0_, x1, y1_, x2, y2_, c);
    tri(x0, y0_, x2, y2_, x3, y3_, c);
  }
  void glyph(int idx, int x, int y, uint16_t c) {
    if (hits(y, 16)) g.drawBitmap(x + dx, y + dy, JP16[idx], 16, 16, c);
  }
  void glyph12(int idx, int x, int y, uint16_t c) {
    if (hits(y, 12)) g.drawBitmap(x + dx, y + dy, JP12[idx], 12, 12, c);
  }
};

}  // namespace

static uint32_t cellHash(int x, int y) {  // stable pseudo-random per cell
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  return h ^ (h >> 13);
}

static uint16_t lerp565(uint16_t a, uint16_t b, int t, int n) {
  int r = ((a >> 11) * (n - t) + (b >> 11) * t) / n;
  int g = (((a >> 5) & 63) * (n - t) + ((b >> 5) & 63) * t) / n;
  int bl = ((a & 31) * (n - t) + (b & 31) * t) / n;
  return (r << 11) | (g << 5) | bl;
}

static uint16_t litColor(uint32_t h, uint16_t dark) {
  uint32_t k = h % 20;
  return k < 6 ? S_WARM : (k < 8 ? S_COOL : dark);
}

// Vertical neon sign: halo, backing, tube, then the caller draws the characters.
static void neonBox(Canvas& cv, int x, int y, int w, int h, uint16_t tube, uint16_t glow, uint16_t fill) {
  cv.frame(x - 2, y - 2, w + 4, h + 4, glow);
  cv.frame(x - 1, y - 1, w + 2, h + 2, glow);
  cv.rect(x, y, w, h, fill);
  cv.frame(x, y, w, h, tube);
}

// Skyline behind the crossing: x, width, top.
static const int16_t SKY[][3] = {{60, 9, 105}, {70, 10, 108}, {81, 9, 90},  {91, 9, 91},   {101, 9, 93},
                                 {111, 6, 92}, {118, 10, 92}, {129, 9, 111}, {139, 9, 96}, {149, 6, 110},
                                 {156, 9, 109}, {166, 7, 88}, {174, 6, 101}};

static void drawScreen(Canvas& cv) {  // the big video screen cycles through three spots
  static const uint16_t TOP[3] = {S_MAGENTA, S_CYAN, S_AMBER}, BOT[3] = {S_BLUE, S_BLUE, S_RED};
  for (int y = 0; y < SCREEN_H; y++) {
    uint16_t c = lerp565(TOP[screenMode], BOT[screenMode], y, SCREEN_H);
    cv.hline(SCREEN_X, SCREEN_Y + y, SCREEN_W, (y & 1) ? lerp565(c, S_BG, 1, 3) : c);
  }
  if (screenMode == 0) {  // 東京
    cv.glyph(JP_TOU, SCREEN_X + 5, SCREEN_Y + 4, TFT_WHITE);
    cv.glyph(JP_KYOU, SCREEN_X + 21, SCREEN_Y + 4, TFT_WHITE);
  } else if (screenMode == 1) {  // weather spot
    cv.glyph(screenGlyph, SCREEN_X + 3, SCREEN_Y + 4, TFT_WHITE);
    if (cv.hits(SCREEN_Y + 8, 8)) {
      cv.g.setTextDatum(TL_DATUM);
      cv.g.setTextColor(TFT_WHITE);
      cv.g.drawString(screenTemp, SCREEN_X + 21 + cv.dx, SCREEN_Y + 8 + cv.dy, 1);
    }
  } else {  // 渋谷
    cv.glyph(JP_SHIBU, SCREEN_X + 5, SCREEN_Y + 4, TFT_WHITE);
    cv.glyph(JP_YA_VALLEY, SCREEN_X + 21, SCREEN_Y + 4, TFT_WHITE);
  }
}

static void drawSignal(Canvas& cv) {  // pedestrian lights: red man above, green man below
  cv.vline(224, 162, 22, S_POLE);
  cv.vline(225, 162, 22, S_POLE);
  cv.rect(218, 136, 14, 26, S_SIGBOX);
  cv.frame(218, 136, 14, 26, S_POLE);
  uint16_t red = walkLight == 0 ? S_RED : S_SIGREDOFF, green = walkLight == 1 ? S_GREEN : S_SIGOFF;
  cv.dot(225, 140, 1, red);  // standing figure
  cv.rect(223, 142, 5, 4, red);
  cv.rect(223, 146, 2, 3, red);
  cv.rect(226, 146, 2, 3, red);
  cv.dot(226, 152, 1, green);  // walking figure
  cv.rect(224, 154, 3, 3, green);
  cv.line(224, 157, 222, 160, green);
  cv.line(226, 157, 228, 160, green);
  cv.line(223, 155, 221, 157, green);
}

static void drawScene(Canvas& cv) {
  cv.rect(0, 0, 240, 240, S_BG);
  for (int y = max(48, cv.y0); y < min(150, cv.y1); y++) {  // city glow
    uint16_t c = y < 106 ? lerp565(S_BG, S_SKY_MID, y - 48, 58) : lerp565(S_SKY_MID, S_SKY_LOW, y - 106, 44);
    cv.hline(0, y, 240, c);
  }

  // skyline
  static const uint16_t SKYWIN[3] = {S_WARM, S_COOL, S_PINKLT};
  for (auto& t : SKY) {
    cv.rect(t[0], t[2], t[1], 150 - t[2], S_FAR);
    for (int wy = t[2] + 3; wy < 146; wy += 4) {
      if (!cv.hits(wy, 1)) continue;
      for (int wx = t[0] + 2; wx < t[0] + t[1] - 1; wx += 3) {
        uint32_t h = cellHash(wx, wy);
        if (h % 7 == 0) cv.rect(wx, wy, 1, 1, SKYWIN[(h >> 4) % 3]);
      }
    }
  }

  // Tokyo Tower
  cv.tri(120, 62, 117, 84, 123, 84, S_TOWER);
  cv.quad(117, 84, 123, 84, 127, 108, 113, 108, S_TOWER);
  cv.quad(113, 108, 127, 108, 124, 116, 116, 116, S_TOWER);
  cv.quad(113, 108, 116, 116, 110, 136, 106, 136, S_TOWER);
  cv.quad(127, 108, 134, 136, 130, 136, 124, 116, S_TOWER);
  cv.rect(114, 94, 12, 4, S_TOWERLT);
  cv.rect(111, 108, 18, 3, S_TOWERLT);
  cv.rect(117, 76, 6, 2, TFT_WHITE);
  cv.line(113, 108, 124, 98, S_LATTICE);
  cv.line(127, 108, 116, 98, S_LATTICE);
  cv.line(109, 126, 126, 112, S_LATTICE);
  cv.line(131, 126, 114, 112, S_LATTICE);
  cv.vline(120, 54, 8, S_LATTICE);

  // left building with the big video screen
  cv.rect(0, 50, 74, 100, S_BLDG);
  cv.vline(18, 56, 4, S_FRAME);
  cv.vline(55, 56, 4, S_FRAME);
  cv.rect(SCREEN_X - 2, SCREEN_Y - 2, SCREEN_W + 4, SCREEN_H + 4, S_FRAME);
  drawScreen(cv);
  for (int wy = 94; wy < 148; wy += 6) {
    if (!cv.hits(wy, 3)) continue;
    for (int wx = 4; wx < 70; wx += 6) cv.rect(wx, wy, 3, 3, litColor(cellHash(wx, wy), S_WINDARK));
  }

  // round department-store tower
  for (int y = max(58, cv.y0); y < min(64, cv.y1); y++) {
    float u = (64 - y) / 6.0f;
    int hw = (int)(33 * sqrtf(1 - u * u));
    cv.hline(203 - hw, y, 2 * hw, S_BLDG2);
  }
  cv.rect(170, 64, 66, 86, S_BLDG2);
  cv.vline(200, 58, 6, S_POLE);
  for (int fy = 70; fy < 150; fy += 7) {
    cv.hline(170, fy, 66, S_LEDGE);
    if (!cv.hits(fy + 2, 3)) continue;
    for (int wx = 172; wx < 236; wx += 4) cv.rect(wx, fy + 2, 3, 3, litColor(cellHash(wx, fy), S_WINDARK));
  }

  // blinking beacons
  for (int i = 0; i < 2; i++)
    if (lightOn[i]) cv.rect(LIGHTS[i][0], LIGHTS[i][1], 2, 2, S_RED);

  // vertical signs: ラーメン, 居酒屋, カラオケ (the long-vowel mark stands upright in vertical text)
  neonBox(cv, 76, 56, 18, 70, S_RED, S_GLOW_RED, S_F_RED);
  cv.glyph(JP_RA, 77, 58, S_RED);
  cv.rect(84, 77, 2, 12, S_RED);
  cv.glyph(JP_ME, 77, 92, S_RED);
  cv.glyph(JP_N, 77, 108, S_RED);
  neonBox(cv, 96, 74, 14, 42, S_AMBER, S_GLOW_AMB, S_F_AMB);
  cv.glyph12(JP_I, 97, 76, S_AMBER);
  cv.glyph12(JP_SAKA, 97, 89, S_AMBER);
  cv.glyph12(JP_YA, 97, 102, S_AMBER);
  neonBox(cv, 147, 58, 18, 70, S_MAGENTA, S_GLOW_MAG, S_F_MAG);
  cv.glyph(JP_KA, 148, 60, S_MAGENTA);
  cv.glyph(JP_RA, 148, 76, S_MAGENTA);
  cv.glyph(JP_O, 148, 92, S_MAGENTA);
  cv.glyph(JP_KE, 148, 108, keOn ? S_MAGENTA : S_DEAD_MAG);

  // the crossing: pavement, road, zebra stripes (both straight crossings and the X)
  cv.rect(0, 150, 240, CROSS_END - 150, S_GROUND);
  for (int y = max(150, cv.y0); y < min(CROSS_END, cv.y1); y++) {
    int w = y < 176 ? 30 * (176 - y) / 26 : 0;
    cv.hline(w, y, 240 - 2 * w, S_ROAD);
  }
  for (int x = 44; x < 200; x += 8) cv.rect(x, 153, 4, 4, S_ZEBRAFAR);
  for (int x = 4; x < 240; x += 11) cv.rect(x, 200, 6, 9, S_ZEBRA);
  for (int k = 0; k < 11; k++) {
    float t = (k + 0.5f) / 11;
    int y = (int)(198 - 40 * t), w = 4 + (int)(2 * (y - 150) / 48.0f), h = 2 + (y > 180);
    for (int side = 0; side < 2; side++) {
      int x = side ? (int)(220 - 180 * t) : (int)(20 + 180 * t);
      for (int r = 0; r < h; r++) cv.hline(x - w / 2 + (side ? -r : r), y - h / 2 + r, w, S_ZEBRAFAR);
    }
  }

  // neon on the wet road
  static const struct { uint8_t x, y, w, h; uint16_t c; } REF[] = {
      {20, 160, 40, 2, S_R_MAG}, {26, 166, 28, 2, S_R_BLUE}, {84, 162, 3, 16, S_R_RED},  {102, 164, 3, 12, S_R_AMB},
      {155, 160, 3, 18, S_R_MAG}, {178, 162, 2, 14, S_R_CYAN}, {198, 162, 2, 14, S_R_MAG}, {118, 160, 4, 10, S_R_ORANGE}};
  for (auto& r : REF) {
    if (r.h > r.w) {  // vertical streaks break up in the ripples
      for (int y = 0; y < r.h; y += 4) cv.rect(r.x + ((y >> 2) & 1 ? reflJ : -reflJ), r.y + y, r.w, 3, r.c);
    } else {
      cv.rect(r.x + reflJ, r.y, r.w, r.h, r.c);
    }
  }
  drawSignal(cv);
}

// ---- crowd ----
struct Path { int16_t x0, y0, x1, y1; };
static const Path ROUTES[6] = {{20, 198, 200, 158}, {220, 198, 40, 158}, {30, 205, 210, 205},
                               {60, 156, 180, 156}, {30, 196, 52, 160}, {210, 196, 188, 160}};
static const uint8_t ROUTE_OF[WALKERS] = {0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2,
                                          3, 0, 1, 4, 5, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5};

static void walker(int i, int& x, int& y, uint32_t& h) {
  h = cellHash(i * 31 + 7, i * 17 + 3);
  const Path& p = ROUTES[ROUTE_OF[i]];
  int delay = h % 3000, dur = 11000 + (int)((h >> 12) % 6000);
  float t = constrain((crowdMs - delay) / (float)dur, 0.0f, 1.0f);
  if (((i & 1) != 0) != crowdOdd) t = 1 - t;  // half go each way; everyone turns around each minute
  x = p.x0 + (int)((p.x1 - p.x0) * t) + (int)(h % 9) - 4;
  y = p.y0 + (int)((p.y1 - p.y0) * t) + (int)((h >> 4) % 7) - 3;
}

static void drawCrowd(Canvas& cv) {
  static const uint16_t UMB[13] = {1, 1, 1, 1, 1, S_DARKUMB, S_DARKUMB, S_DARKUMB, S_DARKUMB,
                                   S_NAVY, S_MAGENTA, S_YELLOW, S_CRIMSON};  // 1 = clear
  static const uint16_t COAT[6] = {S_COAT1, S_COAT1, S_COAT2, S_COAT3, S_COAT4, S_CRIMSON};
  for (int i = 0; i < WALKERS; i++) {
    int x, y;
    uint32_t h;
    walker(i, x, y, h);
    bool near = y > 182;
    if (precip) {  // umbrellas seen from above
      int rx = near ? 4 : 3, ry = near ? 3 : 2;
      cv.ellipse(x, y + ry + 1, 1, 1, S_SHADOW);
      uint16_t c = UMB[(h >> 16) % 13];
      if (c == 1) {
        cv.ellipse(x, y, rx, ry, S_CLEAR);
        cv.ellipseEdge(x, y, rx, ry, S_CLEAREDGE);
      } else {
        cv.ellipse(x, y, rx, ry, c);
        cv.rect(x - rx / 2 - 1, y - ry / 2, 2, 1, c == S_DARKUMB ? S_UMBHI : TFT_WHITE);
      }
    } else {  // no rain: just people
      uint16_t c = COAT[(h >> 16) % 6];
      cv.rect(x - 1, y - 1, 2 + near, 3 + near, c);
      cv.rect(x - 1, y - 3, 2, 2, S_COAT3);
      cv.rect(x - 1, y + 2 + near, 2 + near, 1, S_SHADOW);
    }
  }
}

static void drawCar(Canvas& cv) {  // x = left end of the body
  if (carX == NO_CAR) return;
  int x = carX, y = CAR_Y;
  auto R = [x](int dx, int w) { return carRight ? x + CAR_LEN - dx - w : x + dx; };  // dx from the nose
  int nose = carRight ? x + CAR_LEN : x, ahead = carRight ? 22 : -22;
  cv.tri(nose, y + 8, nose + ahead, y + 5, nose + ahead, y + 14, S_BEAM);
  if (carColor == S_CARNAVY) cv.rect(R(15, 6), y, 6, 2, S_YELLOW);  // taxi roof lamp
  cv.rect(R(9, 18), y + 2, 18, 2, carColor);
  cv.rect(R(5, 26), y + 4, 26, 3, carColor);
  cv.rect(R(8, 19), y + 4, 19, 2, S_CARGLASS);
  cv.rect(x, y + 7, CAR_LEN, 6, carColor);
  cv.dot(R(6, 1), y + 13, 2, TFT_BLACK);
  cv.dot(R(27, 1), y + 13, 2, TFT_BLACK);
  cv.rect(R(0, 2), y + 8, 2, 2, S_WARM);
  cv.rect(R(CAR_LEN - 2, 2), y + 8, 2, 2, S_RED);
}

static void drawPrecip(Canvas& cv) {
  for (int i = 0; i < DROPS && precip; i++) {
    int x = (int)dropX[i], y = (int)dropY[i];
    if (precip == 1) cv.line(x, y, x - 3, y + 8, S_RAIN);
    else cv.rect(x, y, 2, 2, S_SNOW);
  }
}

static void drawAll(Canvas& cv) {
  drawScene(cv);
  drawCrowd(cv);
  drawCar(cv);
  drawPrecip(cv);
}

class Shibuya : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band.setColorDepth(16);
    bandOk = band.createSprite(240, STRIP_H) != nullptr;  // 5.8 KB, fits fragmented heap
    box.setColorDepth(16);
    boxOk = box.createSprite(BOX_W, BOX_H) != nullptr;

    precip = 0;
    keOn = true;
    carX = NO_CAR;
    for (bool& b : lightOn) b = false;
    updateCrossing(msIntoMin(millis()));
    Canvas cv{tft, 0, 0, 0, 240};
    drawAll(cv);
    drawHud();
    timeStr[0] = '\0';
    crowdShown = carShown = false;
    shownCount = -1;
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
      tft.setTextColor(S_WHITE, S_BG);
      tft.setTextPadding(tft.textWidth("88:88:88", 4));
      tft.drawString(timeStr, 10, 6, 4);
      tft.setTextPadding(0);
    }
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 92, textWidth2);
      tft.fillRect(10, 34, 94, 16, S_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(S_CYAN, S_BG);
      tft.drawString(city, 10, 34, 2);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate(s);
    if (weatherChanged(s, prev) || s.code != prev.code) {
      drawWeather(s);
      CnWeather w = cnWeather(s.code);
      screenGlyph = s.weatherValid ? JP_SUNNY + w : JP_CLOUD;
      if (s.weatherValid) snprintf(screenTemp, sizeof(screenTemp), "%d%c", s.temp, s.unit);
      else strcpy(screenTemp, "--");
      uint8_t p = !s.weatherValid ? 0 : (w == CNW_RAIN || w == CNW_THUNDER) ? 1 : (w == CNW_SNOW ? 2 : 0);
      if (p != precip) setPrecip(p);
    }
  }

  void tick(uint32_t ms) override {
    drawMillis();
    int msIntoMinute = msIntoMin(ms);
    uint8_t oldLight = walkLight;
    updateCrossing(msIntoMinute);

    int oldX[DROPS], oldY[DROPS];
    bool respawned[DROPS] = {};
    if (precip) stepPrecip(oldX, oldY, respawned);

    bool moving = msIntoMinute < WALK_MS || carX != NO_CAR;
    bool bandPainted = false;
    if (moving || crowdShown || carShown) {
      repaintRows(CROSS_Y, CROSS_END);
      bandPainted = true;
    }
    crowdShown = msIntoMinute < WALK_MS;
    carShown = carX != NO_CAR;
    if (precip) {
      for (int i = 0; i < DROPS; i++) {
        paintBox(oldX[i] - 8, oldY[i] - 1, bandPainted);
        if (respawned[i]) paintBox((int)dropX[i] - 8, (int)dropY[i] - 1, bandPainted);
      }
    }
    if (walkLight != oldLight) {  // the signal head straddles the band edge
      paintBox(217, 134, false);
      if (!bandPainted) paintBox(217, 150, false);
    }

    drawCount(msIntoMinute);
    uint8_t mode = (ms / 5000) % 3;
    if (mode != screenMode) {
      screenMode = mode;
      repaintRows(SCREEN_Y, SCREEN_Y + SCREEN_H);
    }
    blinkLights(ms);
    flickerNeon(ms);
    if (ms - lastShimmer >= 250) {
      lastShimmer = ms;
      reflJ = (int)random(-1, 2);
      if (!bandPainted) repaintRows(158, 182);
    }
  }

 private:
  TFT_eSprite band = TFT_eSprite(&tft);
  TFT_eSprite box = TFT_eSprite(&tft);
  bool bandOk = false, boxOk = false;
  char timeStr[9] = "";
  bool crowdShown = false, carShown = false;
  int shownCount = -1;
  uint32_t lastShimmer = 0, nextFlicker = 0;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // Walk from :00 to :20 (blinking green at the end), then red while cars cross
  // at :30 (left to right) and :44 (right to left).
  void updateCrossing(int msIntoMinute) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    crowdOdd = (tv.tv_sec / 60) & 1;
    if (!timeSynced()) crowdOdd = (millis() / 60000) & 1;
    crowdMs = min(msIntoMinute, WALK_MS);
    if (msIntoMinute >= WALK_MS) walkLight = 0;
    else if (msIntoMinute < BLINK_MS) walkLight = 1;
    else walkLight = ((msIntoMinute - BLINK_MS) / 250) & 1 ? 2 : 1;

    carX = NO_CAR;
    static const int START[2] = {30000, 44000};
    for (int i = 0; i < 2; i++) {
      int t = msIntoMinute - START[i];
      if (t >= 0 && t < CAR_RUN_MS) {
        int d = t * CAR_SPEED / 1000;
        carRight = i == 0;
        carColor = i == 0 ? S_CARNAVY : S_CARWHITE;
        carX = carRight ? -(CAR_LEN + 24) + d : 240 - d;
      }
    }
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
    if (skipInBand && by >= CROSS_Y && by + BOX_H <= CROSS_END) return;  // band repaint covered it
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
    repaintRows(51, HUD_Y);  // clears old drops, swaps umbrellas and coats
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
    tft.setTextColor(S_CYAN, S_BG);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, 14 + tft.textWidth("88:88:88", 4), 12, 2);
    tft.setTextPadding(0);
  }

  // Japanese date: 11月10日 with the weekday boxed, e.g. [火]
  void drawDate(const FaceState& s) {
    tft.fillRect(104, 34, 74, 16, S_BG);
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    Canvas cv{tft, 0, 0, 0, 240};
    char buf[4];
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(S_PINKLT, S_BG);
    snprintf(buf, sizeof(buf), "%d", t.tm_mon + 1);
    int x = 104 + tft.drawString(buf, 104, 35, 2);
    cv.glyph12(JP_MON, x, 37, S_PINKLT);
    x += 12;
    snprintf(buf, sizeof(buf), "%d", t.tm_mday);
    x += tft.drawString(buf, x, 35, 2);
    cv.glyph12(JP_SUN, x, 37, S_PINKLT);
    x += 14;
    tft.drawRect(x, 36, 14, 14, S_PINKLT);
    cv.glyph12(JP_SUN + t.tm_wday, x + 1, 37, S_PINKLT);  // weekday glyphs run Sunday to Saturday
  }

  void drawHud() {
    Canvas cv{tft, 0, 0, 0, 240};
    // 天気 weather chip
    tft.drawRect(178, 6, 54, 44, S_MAGENTA);
    tft.fillRect(178, 6, 54, 14, S_MAGENTA);
    cv.glyph12(JP_TEN, 193, 7, S_BG);
    cv.glyph12(JP_KI, 205, 7, S_BG);

    // bottom HUD: 渋谷 tag, crossing name, signal countdown
    tft.fillRect(0, HUD_Y, 240, 240 - HUD_Y, S_BG);
    tft.drawRect(6, HUD_Y, 228, 18, S_CYAN);
    tft.fillRect(6, HUD_Y, 38, 18, S_CYAN);
    cv.glyph(JP_SHIBU, 9, HUD_Y + 1, S_BG);
    cv.glyph(JP_YA_VALLEY, 25, HUD_Y + 1, S_BG);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(S_WHITE, S_BG);
    tft.drawString("SCRAMBLE CROSSING", 50, HUD_Y + 9, 1);
  }

  void drawWeather(const FaceState& s) {
    tft.fillRect(179, 20, 52, 29, S_BG);
    tft.setTextDatum(TL_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(S_COOL);
      tft.drawString("NO SIG", 187, 30, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    Canvas cv{tft, 0, 0, 0, 240};
    cv.glyph(JP_SUNNY + w, 182, 22, S_COOL);  // weather glyphs are in CnWeather order
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", s.temp);
    tft.setTextColor(TFT_WHITE);
    int x = 202 + tft.drawString(buf, 202, 21, 2);
    tft.drawCircle(x + 2, 23, 1, TFT_WHITE);
    char u[2] = {s.unit, '\0'};
    tft.drawString(u, x + 5, 21, 2);
    tft.setTextColor(S_COOL);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(cnWeatherLabel(w), 205, 40, 1);
  }

  // WALK 00:12 while green, NEXT 00:40 while red.
  void drawCount(int msIntoMinute) {
    bool walk = msIntoMinute < WALK_MS;
    int left = walk ? (WALK_MS - msIntoMinute + 999) / 1000 : 60 - msIntoMinute / 1000;
    int key = walk ? 100 + left : left;
    if (key == shownCount) return;
    shownCount = key;
    char buf[16];
    snprintf(buf, sizeof(buf), "%s 00:%02d", walk ? "WALK" : "NEXT", left);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(walk ? S_GREEN : S_MAGENTA, S_BG);
    tft.setTextPadding(tft.textWidth("NEXT 00:88", 1));
    tft.drawString(buf, 230, HUD_Y + 9, 1);
    tft.setTextPadding(0);
  }

  void blinkLights(uint32_t ms) {
    for (int i = 0; i < 2; i++) {
      bool on = ((ms + i * 700) % 2000) < 700;
      if (on == lightOn[i]) continue;
      lightOn[i] = on;
      paintBox(LIGHTS[i][0] - 8, LIGHTS[i][1] - 8, false);
    }
  }

  // ケ on the カラオケ sign is a failing tube.
  void flickerNeon(uint32_t ms) {
    if ((int32_t)(ms - nextFlicker) < 0) return;
    bool on = random(6) != 0;
    nextFlicker = ms + (on ? 400 + random(1500) : 70 + random(120));
    if (on == keOn) return;
    keOn = on;
    paintBox(148, 108, false);
  }
};

static Shibuya shibuya;
Face* faceShibuya() { return &shibuya; }
