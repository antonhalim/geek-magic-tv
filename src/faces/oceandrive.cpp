#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "../config.h"
#include "../display.h"
#include "../weather.h"

// Ocean Drive: a neon beachfront at the real time of day over your city. Art-deco hotels
// light their signs from dusk to dawn behind a row of palms, the ocean in front catches the
// sunset and the neon, a car chase tears down the drive at the top of every minute, a
// helicopter sweeps its searchlight at :30 and a speedboat crosses at :15 and :45.

namespace {

constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
struct Rgb { uint8_t r, g, b; };

// ---- HUD palette (the scene's colours depend on the light, see setLight) ----
const uint16_t O_BG = rgb(11, 7, 22), O_TIME = rgb(255, 224, 240), O_CYAN = rgb(60, 230, 255);
const uint16_t O_PINK = rgb(255, 95, 184), O_DATE = rgb(255, 232, 244), O_ORANGE = rgb(255, 169, 77);
const uint16_t O_DIM = rgb(163, 92, 143), O_RED = rgb(255, 48, 80), O_BLUE = rgb(76, 156, 255);
const uint16_t O_BARDIM = rgb(47, 143, 166);
const uint16_t O_SUN = rgb(255, 210, 58), O_CLOUD = rgb(200, 184, 232), O_CLOUDDK = rgb(108, 106, 154);
const uint16_t O_DROP = rgb(95, 212, 255);

// ---- layout ----
const int SCENE_Y0 = 50, HUD_Y = 214, SKY_Y1 = 160, BASE = 158, SAND_Y = 174, SEA_Y = 190, BY = 174;
const int STRIP_H = 12, STRIPS = (HUD_Y - SCENE_Y0 + STRIP_H - 1) / STRIP_H;
const int BOX_W = 40, BOX_H = 44, QMAX = 128;
const int HOTELS = 6, TOWERS = 30, STARS = 44, TWINKLE = 12, CLOUDS = 13, GULLS = 4, PALMS = 6, FRONDS = 9;
const int KNOTS = 12, DROPS = 32, FLAKES = 44, PARTS = 44, TVS = 8;
const int CHASE_LEN = 2400, CHASE_B = 4000, CODE3_MS = 6500, HELI_MS = 30000, HELI_LEN = 11000, BOAT_LEN = 5000;
const int SUN_X = 150, SUN_R = 22, SIREN_R = 22;
const float PIF = 3.14159265f, DEG = PIF / 180;

enum { ST_BROW, ST_PORT, ST_STEP, ST_CAFE, ST_ROUND };
enum { SG_NONE, SG_FIN, SG_ROOF };
struct Hotel {
  uint8_t x, w, h;
  Rgb c, n;           // facade, neon
  const char* s;      // sign
  uint8_t sg, fx, fw, ft, st;  // sign kind, fin x / width / height above the roof, style
};
const Hotel HOT[HOTELS] = {
    {1, 42, 46, {248, 172, 206}, {255, 60, 170}, "SOL", SG_FIN, 29, 7, 14, ST_BROW},
    {45, 36, 36, {156, 238, 216}, {60, 236, 255}, "AZUL", SG_ROOF, 0, 0, 0, ST_PORT},
    {83, 34, 46, {206, 182, 255}, {255, 70, 200}, "PALMA", SG_FIN, 13, 8, 22, ST_STEP},
    {119, 50, 18, {255, 208, 166}, {255, 60, 180}, "", SG_NONE, 0, 0, 0, ST_CAFE},
    {171, 36, 40, {255, 180, 152}, {90, 255, 200}, "CORAL", SG_ROOF, 0, 0, 0, ST_PORT},
    {209, 33, 50, {244, 240, 228}, {60, 220, 255}, "MAR", SG_FIN, 3, 7, 10, ST_ROUND}};

// 3x5 sign letters, top row in the high bits.
const char GL_CH[] = "SOLAZUPMCR";
const uint16_t GL_BITS[10] = {0b011100010001110, 0b010101101101010, 0b100100100100111, 0b010101111101101,
                              0b111001010100111, 0b101101101101011, 0b110101110100100, 0b101111111101101,
                              0b011100100100011, 0b110101110101101};
const int8_t FLAM[32][2] = {{5, 0}, {6, 0}, {4, 1}, {7, 1}, {7, 2}, {4, 2}, {4, 3}, {5, 4}, {5, 5}, {4, 6}, {1, 7},
                            {2, 7}, {3, 7}, {4, 7}, {5, 7}, {0, 8}, {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8},
                            {1, 9}, {2, 9}, {3, 9}, {4, 9}, {5, 9}, {3, 10}, {3, 11}, {3, 12}, {4, 10}, {5, 11}};
const uint8_t IN[6] = {6, 4, 3, 2, 1, 1};  // rounded corner inset per row
const struct { uint8_t x, h; int8_t lean; uint8_t ph; } PALM[PALMS] = {
    {14, 70, -7, 0}, {60, 44, 5, 17}, {126, 76, -4, 31}, {146, 58, -6, 44}, {190, 86, -8, 22}, {230, 56, 5, 53}};
const int16_t FR[FRONDS][2] = {{-170, 17}, {-145, 19}, {-118, 16}, {-92, 13}, {-64, 16}, {-38, 19}, {-12, 17}, {168, 12}, {12, 12}};
const Rgb CARC[7] = {{248, 248, 252}, {120, 214, 255}, {255, 150, 200}, {255, 222, 120}, {150, 255, 206}, {84, 76, 110}, {255, 124, 96}};
const Rgb TNEON[3] = {{60, 236, 255}, {255, 60, 180}, {180, 110, 255}};

struct Area { int16_t x, y, w, h; };

struct Pose {  // one palm: where the trunk leans and the frond knots, relative to the crown
  int16_t cx, cy, top16;  // crown, trunk top offset in 1/16 px
  uint8_t ln[FRONDS], kn[FRONDS];
  int8_t kx[FRONDS][KNOTS], ky[FRONDS][KNOTS];
};

// Everything the scene reads, so every repaint agrees with what is on screen. Lives on the
// heap while this face is up.
struct Drive {
  // light, of 256
  int d, tw, lit, sgn, sa, dim, starA, sunA, daySunA, hazeA, cloudA;
  bool dawn, night, moon, wet, storm, snow, fog, heavy, flash;
  uint8_t kind, clouds;
  int wind10;
  int8_t sinT[256];
  // colours
  uint16_t sky[SKY_Y1 - SCENE_Y0], sea[HUD_Y - SEA_Y];
  Rgb grey, fogC, cloudTop, cloudUnd, waveC, rainC;
  uint16_t gullC, twC, twEdge, twWarm, twCool, twNeon[3];
  uint16_t hFc[HOTELS], hSh[HOTELS], hHi[HOTELS], hDk[HOTELS], hTube[HOTELS], hFin[HOTELS], hUnlit[HOTELS], hDoor[HOTELS];
  Rgb hCore[HOTELS];
  uint16_t glass, warmWin, glint, cafeWin, awnA, awnB, flamDay;
  uint16_t walk, asph, lane, curb, curb2, carC[7], carGlass, polWhite, sports, sportsDk;
  uint16_t sand, sandDot, sandWet, sandWetDot, hutPink, hutRoof, hutStilt, hutWin, flagC;
  uint16_t trunk, trunk2, frond, nut, boatC, heliC;
  // this frame
  uint32_t fms;
  int mim;
  float ft;
  // things that move
  uint8_t twX[TOWERS], twW[TOWERS], twH[TOWERS];
  uint8_t sunHW[SUN_R + 1];
  Pose pose[PALMS];
  Area palmShown[PALMS], crownShown[PALMS];
  float cloudX[CLOUDS], gullX[GULLS];
  int16_t cloudShown[CLOUDS], gullSX[GULLS], gullSY[GULLS];
  uint8_t gullFlap[GULLS];
  int16_t dropX[PARTS], dropY[PARTS];  // rain drops or snowflakes
  int8_t flagOff[4];
  int16_t bolt[13];
  Area tv[TVS];
  uint8_t tvN, flick;
  bool heliOn, beamOn;
  int16_t heliX, heliY, spotX, spotY;
  Area heliShown, beamShown, polShown[4];
  int16_t rise, set;  // local minutes, -1 when the sun does not rise or set
  Area q[QMAX];  // areas to repaint this tick
  uint8_t qn;
};
Drive* dr = nullptr;

uint32_t hsh(uint32_t i) {
  i ^= 0x9e3779b9u;
  i *= 2654435761u;
  i ^= i >> 15;
  i *= 2246822519u;
  i ^= i >> 13;
  return i;
}
int imod(int a, int b) {
  int m = a % b;
  return m < 0 ? m + b : m;
}
int64_t fdiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
int isin(int a) { return dr->sinT[a & 255]; }  // -127..127

Rgb lerpRgb(Rgb a, Rgb b, int t) {  // t of 256
  return {(uint8_t)(a.r + (b.r - a.r) * t / 256), (uint8_t)(a.g + (b.g - a.g) * t / 256),
          (uint8_t)(a.b + (b.b - a.b) * t / 256)};
}
uint16_t c565(Rgb c) { return rgb(c.r, c.g, c.b); }
Rgb un565(uint16_t v) { return {(uint8_t)((v >> 8) & 0xF8), (uint8_t)((v >> 3) & 0xFC), (uint8_t)((v << 3) & 0xF8)}; }
const Rgb BLACK = {0, 0, 0}, WHITE = {255, 255, 255};

// Night, day, dusk and dawn colours, mixed for the current light.
Rgb K(Rgb n, Rgb dy, Rgb du, Rgb da) {
  Rgb c = lerpRgb(n, dy, dr->d);
  return lerpRgb(c, dr->dawn ? da : du, dr->tw * 218 >> 8);
}
Rgb K(Rgb n, Rgb dy, Rgb du) { return K(n, dy, du, du); }

// Everything the scene draws goes through this, into the strip or box sprite, so the same
// code paints the whole scene and repaints small areas. Light and glow are blended into
// the pixels already in the sprite.
struct Canvas {
  TFT_eSprite& g;
  int dx, dy;          // screen point (x, y) maps to (x + dx, y + dy)
  int x0, x1, y0, y1;  // the screen area being painted
  uint16_t* buf;
  bool hits(int x, int y, int w, int h) const { return y < y1 && y + h > y0 && x < x1 && x + w > x0; }
  bool rows(int a, int b) const { return a < y1 && b > y0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (w > 0 && h > 0 && hits(x, y, w, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void px(int x, int y, uint16_t c) { rect(x, y, 1, 1, c); }
  void line(int xa, int ya, int xb, int yb, uint16_t c) {
    if (hits(min(xa, xb), min(ya, yb), abs(xb - xa) + 1, abs(yb - ya) + 1)) g.drawLine(xa + dx, ya + dy, xb + dx, yb + dy, c);
  }
  void dot(int x, int y, int r, uint16_t c) {
    if (hits(x - r, y - r, 2 * r + 1, 2 * r + 1)) g.fillCircle(x + dx, y + dy, r, c);
  }
  static void mixPx(uint16_t& p, Rgb c, int a, bool add) {
    uint16_t v = (uint16_t)((p >> 8) | (p << 8));
    int r = (v >> 8) & 0xF8, gg = (v >> 3) & 0xFC, b = (v << 3) & 0xF8;
    if (add) {
      r += c.r * a >> 8; gg += c.g * a >> 8; b += c.b * a >> 8;
    } else {
      r += (c.r - r) * a >> 8; gg += (c.g - gg) * a >> 8; b += (c.b - b) * a >> 8;
    }
    uint16_t q = rgb(min(r, 255), min(gg, 255), min(b, 255));
    p = (uint16_t)((q >> 8) | (q << 8));
  }
  // Blend c into pixels [x, x + w) of row y: added as light, or mixed in with weight a/256.
  void blend(int x, int y, int w, Rgb c, int a, bool add) {
    if (a <= 0 || y < y0 || y >= y1) return;
    int xa = max(x, x0), xb = min(x + w, x1);
    uint16_t* row = buf + (y + dy) * g.width() + dx;
    for (int i = xa; i < xb; i++) mixPx(row[i], c, a, add);
  }
  void add(int x, int y, int w, Rgb c, int a) { blend(x, y, w, c, a, true); }
  void mix(int x, int y, int w, Rgb c, int a) { blend(x, y, w, c, a, false); }
  void addRect(int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = max(y, y0); yy < min(y + h, y1); yy++) add(x, yy, w, c, a);
  }
  void mixRect(int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = max(y, y0); yy < min(y + h, y1); yy++) mix(x, yy, w, c, a);
  }
  // Round glow fading out from the centre.
  void glow(int cx, int cy, int r, Rgb c, int a) {
    if (a <= 0) return;
    int r2 = r * r;
    for (int y = max(cy - r + 1, y0); y < min(cy + r, y1); y++) {
      uint16_t* row = buf + (y + dy) * g.width() + dx;
      int dy2 = (y - cy) * (y - cy);
      for (int x = max(cx - r + 1, x0); x < min(cx + r, x1); x++) {
        int d2 = dy2 + (x - cx) * (x - cx);
        if (d2 < r2) mixPx(row[x], c, a * (r2 - d2) / r2, true);
      }
    }
  }
};

void weatherIcon(int w, int x, int y, bool night) {
  auto cloud = [&](uint16_t c) {
    tft.fillCircle(x - 3, y, 3, c);
    tft.fillCircle(x + 1, y - 2, 4, c);
    tft.fillCircle(x + 5, y + 1, 3, c);
    tft.fillRect(x - 3, y + 1, 9, 3, c);
  };
  switch (w) {
    case CNW_SUNNY:
      if (night) {
        tft.fillCircle(x, y, 5, O_DATE);
        tft.fillCircle(x + 3, y - 2, 4, O_BG);
        break;
      }
      tft.fillCircle(x, y, 4, O_SUN);
      for (int i = 0; i < 8; i++) {
        float an = i * PIF / 4;
        tft.drawLine(x + (int)lroundf(cosf(an) * 6), y + (int)lroundf(sinf(an) * 6),
                     x + (int)lroundf(cosf(an) * 8), y + (int)lroundf(sinf(an) * 8), O_SUN);
      }
      break;
    case CNW_CLOUD:
      tft.fillCircle(x - 3, y - 4, 4, night ? O_DATE : O_SUN);
      cloud(O_CLOUD);
      break;
    case CNW_OVERCAST:
      tft.fillCircle(x - 4, y - 4, 3, O_CLOUDDK);
      tft.fillCircle(x, y - 6, 3, O_CLOUDDK);
      cloud(O_CLOUD);
      break;
    case CNW_FOG:
      for (int i = 0; i < 3; i++) tft.fillRect(x - 7 + (i & 1) * 2, y + i * 4 - 4, 14, 1, O_CLOUD);
      break;
    case CNW_RAIN:
      cloud(O_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.drawLine(x + d, y + 6, x + d - 1, y + 9, O_DROP);
      break;
    case CNW_SNOW:
      cloud(O_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.fillRect(x + d, y + 6 + ((d + 3) & 4 ? 1 : 0), 2, 2, TFT_WHITE);
      break;
    case CNW_THUNDER:
      cloud(O_CLOUDDK);
      tft.fillTriangle(x + 2, y + 4, x - 2, y + 8, x + 1, y + 8, O_SUN);
      tft.fillTriangle(x + 1, y + 7, x + 3, y + 7, x - 1, y + 11, O_SUN);
      break;
  }
}

// ---- the sky ----

int cloudY(int i) { return 56 + (int)(hsh(i * 9 + 5) % (dr->heavy ? 40 : 46)); }
int cloudW(int i) { return 34 + (int)(hsh(i * 9 + 1) % 70); }
int cloudH(int i) { return 2 + (int)(hsh(i * 9 + 2) % 3); }
int starX(int i) { return (int)(hsh(i * 5 + 1) % 240); }
int starY(int i) { return SCENE_Y0 + (int)(hsh(i * 5 + 2) % 62); }

void drawSky(Canvas& cv) {
  const Drive& r = *dr;
  if (cv.y0 >= SKY_Y1) return;
  for (int y = max(cv.y0, SCENE_Y0); y < min(cv.y1, SKY_Y1); y++) cv.rect(cv.x0, y, cv.x1 - cv.x0, 1, r.sky[y - SCENE_Y0]);
  if (r.starA > 12 && cv.y0 < SCENE_Y0 + 62) {
    for (int i = 0; i < STARS; i++) {
      int a = r.starA * (90 + (int)(hsh(i * 5 + 3) % 154)) >> 8;
      if (i < TWINKLE && (r.fms / 400 + i * 3) % 4 == 0) a = a * 2 / 5;
      cv.mix(starX(i), starY(i), 1, {230, 224, 255}, a);
    }
  }
  if (r.moon && cv.hits(176, 48, 44, 44)) {
    cv.glow(198, 70, 20, {220, 220, 255}, 46);
    cv.dot(198, 70, 6, rgb(244, 239, 220));
    cv.dot(201, 68, 5, r.sky[68 - SCENE_Y0]);
  }
  if (r.sunA > 8) {  // the sun on the horizon at dusk and dawn, striped like a retro poster
    int cy = r.dawn ? 118 : 120;
    if (cv.hits(SUN_X - 60, cy - 60, 120, 120)) {
      cv.glow(SUN_X, cy, 60, {255, 80, 120}, 77 * r.sunA >> 8);
      cv.glow(SUN_X, cy, 32, {255, 190, 110}, 64 * r.sunA >> 8);
      for (int k = -SUN_R; k <= SUN_R; k++) {
        if (k >= 0 && k % 6 < min(4, 1 + k / 6)) continue;
        int y = cy + k;
        if (y < cv.y0 || y >= cv.y1) continue;
        int hw = r.sunHW[abs(k)], f = (k + SUN_R) * 256 / (2 * SUN_R);
        Rgb c = r.dawn ? lerpRgb({255, 246, 200}, {255, 140, 150}, f) : lerpRgb({255, 238, 120}, {255, 64, 150}, f);
        cv.mix(SUN_X - hw, y, 2 * hw + 1, c, r.sunA);
      }
    }
  }
  if (r.daySunA > 8 && cv.hits(164, 42, 60, 60)) {
    cv.glow(194, 72, 30, {255, 244, 210}, 115 * r.daySunA >> 8);
    for (int k = -5; k <= 5; k++) {
      int hw = r.sunHW[abs(k) * SUN_R / 5] * 5 / SUN_R;
      cv.mix(194 - hw, 72 + k, 2 * hw + 1, {255, 252, 236}, r.daySunA);
    }
  }
  if (r.flash) {
    cv.addRect(0, SCENE_Y0, 240, SKY_Y1 - SCENE_Y0, {190, 200, 255}, 90);
    for (int i = 0; i < 12; i++) {
      int ya = 52 + i * 8;
      cv.line(r.bolt[i] + 1, ya, r.bolt[i + 1] + 1, ya + 8, rgb(150, 160, 255));
      cv.line(r.bolt[i] - 1, ya, r.bolt[i + 1] - 1, ya + 8, rgb(150, 160, 255));
      cv.line(r.bolt[i], ya, r.bolt[i + 1], ya + 8, TFT_WHITE);
    }
  }
}

void drawClouds(Canvas& cv) {
  const Drive& r = *dr;
  for (int i = 0; i < r.clouds; i++) {
    int x = r.cloudShown[i], y = cloudY(i), w = cloudW(i), h = cloudH(i);
    if (!cv.hits(x, y - 2, w, h + 4)) continue;
    cv.mix(x + w * 15 / 100, y, w * 60 / 100, r.cloudTop, r.cloudA);
    cv.mixRect(x, y + 1, w, h, r.cloudTop, r.cloudA);
    cv.mix(x + 5, y + 1 + h, w - 12, r.cloudUnd, r.cloudA);
    if (r.kind != CNW_SUNNY) cv.mixRect(x + w * 3 / 10, y - 2, w * 3 / 10, 2, r.cloudTop, r.cloudA);
  }
}

void drawGulls(Canvas& cv) {
  const Drive& r = *dr;
  if (r.storm || r.d < 64) return;
  for (int i = 0; i < GULLS; i++) {
    int x = r.gullSX[i], y = r.gullSY[i];
    if (!cv.hits(x, y, 5, 2)) continue;
    if (r.gullFlap[i]) {
      for (int k = 0; k < 5; k++) cv.px(x + k, y + ((k & 1) ? 0 : 1), r.gullC);
    } else {
      cv.rect(x, y, 2, 1, r.gullC);
      cv.px(x + 2, y + 1, r.gullC);
      cv.rect(x + 3, y, 2, 1, r.gullC);
    }
  }
}

// Downtown across the bay: towers with lit windows after dark, coloured crowns and beacons.
void drawSkyline(Canvas& cv) {
  const Drive& r = *dr;
  const int base = 154;
  if (!cv.rows(base - 62, base)) return;
  for (int i = 0; i < TOWERS; i++) {
    int x = r.twX[i] - 6, w = r.twW[i], h = r.twH[i], top = base - h;
    if (!cv.hits(x - 1, top - 2, w + 2, h + 2)) continue;
    cv.rect(x, top, w, h, r.twC);
    cv.rect(x, top, 1, h, r.twEdge);
    if (r.lit > 25) {
      for (int y = top + 3; y < base - 1; y += 3) {
        if (y < cv.y0 || y >= cv.y1) continue;
        for (int xx = x + 1; xx < x + w - 1; xx += 2) {
          int q = hsh(i * 733 + y * 17 + xx) & 255;
          if (q < 77) cv.px(xx, y, q < 46 ? r.twWarm : r.twCool);
        }
      }
    }
    if (h > 34 && r.sgn > 12) {
      cv.rect(x, top, w, 1, r.twNeon[i % 3]);
      cv.addRect(x - 1, top - 1, w + 2, 3, TNEON[i % 3], 64 * r.sgn >> 8);
    }
    if (h > 40 && ((r.fms / 800 + i) & 1)) cv.mixRect(x + w / 2, top - 2, 1, 2, {255, 50, 60}, 256 - r.d * 2 / 5);
  }
  if (r.fog) cv.mixRect(0, SCENE_Y0, 240, SKY_Y1 - SCENE_Y0, r.fogC, 128);
}

// ---- the hotels ----

int signLevel(int i) { return ((dr->flick >> i) & 1) ? dr->sgn * 38 >> 8 : dr->sgn; }
int hotelTop(int i) {
  const Hotel& H = HOT[i];
  int top = BASE - H.h;
  if (H.sg == SG_FIN) return top - H.ft;
  if (H.sg == SG_ROOF || H.st == ST_STEP) return top - 8;
  if (H.st == ST_CAFE) return top - 15;
  return top;
}
Area hotelArea(int i) {
  int t = hotelTop(i) - 3;
  return {(int16_t)(HOT[i].x - 3), (int16_t)t, (int16_t)(HOT[i].w + 6), (int16_t)(BASE - t)};
}

// A neon tube: painted on the facade by day, glowing after dark.
void neon(Canvas& cv, int x, int y, int w, int h, int i, int s) {
  const Drive& r = *dr;
  if (!cv.hits(x - 2, y - 2, w + 4, h + 4)) return;
  cv.rect(x, y, w, h, r.hTube[i]);
  if (s > 5) {
    cv.mixRect(x, y, w, h, r.hCore[i], s);
    cv.addRect(x - 1, y - 1, w + 2, h + 2, HOT[i].n, 66 * s >> 8);
    cv.addRect(x - 2, y - 2, w + 4, h + 4, HOT[i].n, 23 * s >> 8);
  }
}

void glyph(Canvas& cv, char ch, int x, int y, uint16_t c) {
  const char* p = strchr(GL_CH, ch);
  if (!p) return;
  uint16_t bits = GL_BITS[p - GL_CH];
  for (int k = 0; k < 15; k++)
    if ((bits >> (14 - k)) & 1) cv.px(x + k % 3, y + k / 3, c);
}

void sign(Canvas& cv, int i, int x, int y, bool vert, int s) {
  const Drive& r = *dr;
  const char* str = HOT[i].s;
  int n = strlen(str), w = vert ? 3 : n * 4 - 1, h = vert ? n * 6 - 1 : 5;
  if (!cv.hits(x - 3, y - 3, w + 6, h + 6)) return;
  if (s > 5) {
    cv.addRect(x - 1, y - 1, w + 2, h + 2, HOT[i].n, 26 * s >> 8);
    cv.addRect(x - 3, y - 3, w + 6, h + 6, HOT[i].n, 20 * s >> 8);
  }
  uint16_t c = s > 5 ? c565(lerpRgb(un565(r.hUnlit[i]), r.hCore[i], s)) : r.hUnlit[i];
  for (int k = 0; k < n; k++) glyph(cv, str[k], vert ? x : x + k * 4, vert ? y + k * 6 : y, c);
}

// Window states: lit, a flickering TV, or dark. A few change every nine seconds.
int windowKey(int i, int y, int xx) {
  bool slow = (hsh(xx * 7 + y) & 0xffff) < 7864;
  return hsh(i * 131 + y * 17 + xx + (slow ? (dr->fms / 9000) * 3 : 0)) & 0xffff;
}
uint16_t tvColor(int xx) {
  const Drive& r = *dr;
  int a = (115 + isin((int)(r.fms * 256 / 440) + xx * 41) * 90 / 127) * r.lit >> 8;
  return c565(lerpRgb(un565(r.glass), {90, 150, 255}, a));
}

void flamingo(Canvas& cv, int x, int y, int s) {
  if (!cv.hits(x - 2, y - 2, 11, 17)) return;
  uint16_t c = dr->flamDay;
  if (s > 5) {
    cv.addRect(x - 2, y - 2, 11, 17, {255, 60, 180}, 41 * s >> 8);
    c = c565(lerpRgb(un565(c), {255, 130, 214}, s));
  }
  for (int k = 0; k < 32; k++) cv.px(x + FLAM[k][0], y + FLAM[k][1], c);
}

void drawHotel(Canvas& cv, int i) {
  const Drive& r = *dr;
  const Hotel& H = HOT[i];
  Area a = hotelArea(i);
  if (!cv.hits(a.x, a.y, a.w, a.h)) return;
  int top = BASE - H.h, x = H.x, w = H.w, s = signLevel(i);
  uint16_t fc = r.hFc[i], sh = r.hSh[i], hi = r.hHi[i], dk = r.hDk[i];
  if (H.st == ST_ROUND) {
    for (int k = 0; k < H.h; k++) {
      int in = k < 6 ? IN[k] : 0;
      cv.rect(x + in, top + k, w - in, 1, fc);
    }
    cv.rect(x, top + 6, 1, H.h - 6, hi);
    cv.rect(x + w - 2, top + 1, 2, H.h - 1, sh);
  } else {
    cv.rect(x, top, w, H.h, fc);
    cv.rect(x, top, 1, H.h, hi);
    cv.rect(x + w - 2, top, 2, H.h, sh);
  }
  if (H.st == ST_STEP) {
    cv.rect(x + 5, top - 4, w - 10, 4, fc);
    cv.rect(x + 10, top - 8, w - 20, 4, fc);
    cv.rect(x + 5, top - 4, 1, 4, hi);
    cv.rect(x + 10, top - 8, 1, 4, hi);
    cv.rect(x + w - 7, top - 4, 2, 4, sh);
    cv.rect(x + w - 12, top - 8, 2, 4, sh);
    neon(cv, x + 5, top - 4, w - 10, 1, i, s);
    neon(cv, x + 10, top - 8, w - 20, 1, i, s);
  }
  if (H.st != ST_ROUND && H.st != ST_CAFE) neon(cv, x, top, w, 1, i, s);

  if (H.st == ST_ROUND) {  // ribbon windows and three speed stripes
    for (int y = top + 11; y < BASE - 8; y += 6) {
      if (!cv.rows(y, y + 3)) continue;
      cv.rect(x + 2, y, w - 5, 2, r.glass);
      cv.rect(x + 1, y + 2, w - 3, 1, dk);
      for (int xx = x + 2; xx < x + w - 4; xx += 5) {
        cv.rect(xx + 4, y, 1, 2, fc);
        int q = windowKey(i, y, xx);
        if (q < 27525 && r.lit > 12) cv.rect(xx, y, 4, 2, r.warmWin);
        else if (q > 62259 && r.lit > 77) cv.rect(xx, y, 4, 2, tvColor(xx));
        else if (r.d > 77) cv.px(xx, y, r.glint);
      }
    }
    for (int k = 3; k <= 7; k += 2) {
      int in = k < 6 ? IN[k] : 0;
      neon(cv, x + in + 1, top + k, w - 3 - in, 1, i, s);
    }
  } else if (H.st != ST_CAFE) {
    for (int y = top + 6; y < BASE - 9; y += 7) {
      if (!cv.rows(y - 3, y + 3)) continue;
      if (H.st == ST_BROW || H.st == ST_STEP) {  // eyebrow ledges
        cv.rect(x - 1, y - 3, w + 2, 1, hi);
        cv.rect(x, y - 2, w, 1, dk);
      }
      for (int xx = x + 4; xx < x + w - 5; xx += 6) {
        int q = windowKey(i, y, xx);
        bool on = q < 27525 && r.lit > 12;
        if (H.st == ST_PORT) {  // portholes
          uint16_t c = on ? r.warmWin : r.glass;
          cv.rect(xx + 1, y, 1, 3, c);
          cv.rect(xx, y + 1, 3, 1, c);
        } else {
          cv.rect(xx, y, 3, 3, on ? r.warmWin : r.glass);
          if (!on && q > 62259 && r.lit > 77) cv.rect(xx, y, 3, 3, tvColor(xx));
          else if (!on && r.d > 77) cv.px(xx, y, r.glint);
        }
      }
    }
    int dx = x + w / 2 - 2;  // door and canopy
    cv.rect(dx, BASE - 6, 4, 6, r.hDoor[i]);
    cv.rect(dx - 3, BASE - 8, 10, 1, r.hTube[i]);
  }

  if (H.st == ST_CAFE) {  // striped awning, warm windows, string lights and a neon flamingo
    cv.rect(x + 3, BASE - 11, w - 6, 6, r.cafeWin);
    for (int xx = x + 3; xx < x + w - 3; xx += 8) cv.rect(xx, BASE - 11, 1, 6, fc);
    for (int xx = x; xx < x + w; xx += 2) cv.rect(xx, BASE - 14, 2, 3, ((xx - x) >> 1) & 1 ? r.awnA : r.awnB);
    cv.rect(x, BASE - 11, w, 1, dk);
    cv.rect(x, top, w, 1, hi);
    if (r.sgn > 12 && cv.rows(top - 5, top + 2)) {
      const Rgb LC[3] = {{255, 90, 180}, {60, 236, 255}, {255, 220, 120}};
      for (int xx = x + 1; xx < x + w - 1; xx += 3) {
        int yy = top - 2 + (isin((xx - x) * 256 / w) * 3 / 2 + (isin((xx - x) * 256 / w) > 0 ? 63 : -63)) / 127;
        Rgb c = LC[((xx - x) / 3) % 3];
        cv.mix(xx, yy, 1, c, r.sgn);
        cv.addRect(xx - 1, yy - 1, 3, 3, c, 56 * r.sgn >> 8);
      }
    }
    flamingo(cv, x + w - 10, top - 14, s);
  }
  if (H.sg == SG_FIN) {
    int n = strlen(H.s), fy = top - H.ft, fh = n * 6 + 3, fx = x + H.fx;
    cv.rect(fx, fy, H.fw, fh, r.hFin[i]);
    cv.rect(fx + H.fw - 1, fy, 1, fh, c565(lerpRgb(un565(r.hFin[i]), BLACK, 64)));
    cv.rect(fx, fy, H.fw, 1, c565(lerpRgb(un565(r.hFin[i]), WHITE, 38)));
    sign(cv, i, fx + (H.fw - 3) / 2, fy + 2, true, s);
  } else if (H.sg == SG_ROOF) {
    int n = strlen(H.s), sw = n * 4 - 1, sx = x + (w - sw) / 2, sy = top - 8;
    cv.rect(sx + 1, sy + 5, 1, 3, dk);
    cv.rect(sx + sw - 2, sy + 5, 1, 3, dk);
    cv.rect(sx - 1, sy + 5, sw + 2, 1, dk);
    sign(cv, i, sx, sy, false, s);
  }
}

// ---- the drive ----

void car(Canvas& cv, int x, int y, int dir, int ci) {
  if (!cv.hits(x - 13, y, 39, 4)) return;
  const Drive& r = *dr;
  auto X = [&](int i, int w) { return dir > 0 ? x + i : x + 13 - i - w; };
  cv.rect(X(3, 6), y, 6, 1, r.carGlass);
  cv.rect(X(1, 11), y + 1, 11, 1, r.carC[ci]);
  cv.rect(X(0, 13), y + 2, 13, 1, r.carC[ci]);
  cv.rect(X(2, 2), y + 3, 2, 1, rgb(12, 10, 18));
  cv.rect(X(9, 2), y + 3, 2, 1, rgb(12, 10, 18));
  if (r.lit > 64) {
    int hx = X(12, 1), tx = X(0, 1);
    cv.px(hx, y + 2, rgb(255, 250, 220));
    cv.px(tx, y + 2, rgb(255, 40, 50));
    cv.addRect(dir > 0 ? hx + 1 : hx - 12, y + 1, 12, 3, {255, 236, 190}, 41 * r.lit >> 8);
    cv.addRect(tx - 1, y + 1, 3, 3, {255, 40, 60}, 77 * r.lit >> 8);
  }
}

void sportsCar(Canvas& cv, int x, int y, int dir) {
  if (!cv.hits(x - 16, y, 52, 5)) return;
  const Drive& r = *dr;
  auto X = [&](int i, int w) { return dir > 0 ? x + i : x + 20 - i - w; };
  cv.addRect(dir > 0 ? x - 14 : x + 20, y + 1, 14, 3, {255, 60, 120}, 26 + (13 * r.lit >> 8));  // speed streak
  cv.rect(X(9, 5), y, 5, 1, rgb(40, 50, 84));
  cv.rect(X(5, 12), y + 1, 12, 1, r.sports);
  cv.rect(X(9, 4), y + 1, 4, 1, rgb(40, 50, 84));
  cv.rect(X(1, 19), y + 2, 19, 1, r.sports);
  cv.rect(X(0, 20), y + 3, 20, 1, r.sportsDk);
  cv.rect(X(3, 3), y + 4, 3, 1, rgb(10, 10, 14));
  cv.rect(X(15, 3), y + 4, 3, 1, rgb(10, 10, 14));
  int hx = X(19, 1), tx = X(0, 1);
  cv.px(hx, y + 2, rgb(255, 250, 230));
  cv.px(tx, y + 2, rgb(255, 30, 40));
  if (r.lit > 64) {
    cv.addRect(dir > 0 ? hx + 1 : hx - 16, y + 1, 16, 3, {255, 236, 190}, 46 * r.lit >> 8);
    cv.add(dir > 0 ? tx - 10 : tx + 1, y + 2, 10, {255, 30, 50}, 115 * r.lit >> 8);
  }
}

int sirenX(int x, int dir, int k) {  // centre of the lit half of the light bar
  bool red = ((dr->fms / 120) + k) & 1;
  int i = red ? 7 : 9;
  return (dir > 0 ? x + i : x + 18 - i - 2) + 1;
}

void police(Canvas& cv, int x, int y, int dir, int k) {
  const Drive& r = *dr;
  bool red = ((r.fms / 120) + k) & 1;
  int cx = sirenX(x, dir, k);
  if (cv.hits(cx - SIREN_R, y - SIREN_R, 2 * SIREN_R, 2 * SIREN_R))
    cv.glow(cx, y, SIREN_R, red ? Rgb{255, 40, 60} : Rgb{60, 120, 255}, 77 * (115 + (141 * r.lit >> 8)) >> 8);
  if (!cv.hits(x - 13, y, 44, 5)) return;
  auto X = [&](int i, int w) { return dir > 0 ? x + i : x + 18 - i - w; };
  cv.rect(X(4, 10), y + 1, 10, 1, r.polWhite);
  cv.rect(X(5, 8), y + 1, 8, 1, rgb(34, 40, 64));
  cv.rect(X(0, 18), y + 2, 18, 1, r.polWhite);
  cv.rect(X(0, 18), y + 3, 18, 1, rgb(20, 20, 30));
  cv.rect(X(3, 3), y + 4, 3, 1, rgb(8, 8, 12));
  cv.rect(X(13, 3), y + 4, 3, 1, rgb(8, 8, 12));
  cv.rect(X(7, 2), y, 2, 1, red ? rgb(255, 40, 60) : rgb(90, 20, 30));
  cv.rect(X(9, 2), y, 2, 1, red ? rgb(20, 30, 90) : rgb(60, 120, 255));
  int hx = X(17, 1);
  cv.px(hx, y + 2, rgb(255, 250, 230));
  if (r.lit > 64) cv.addRect(dir > 0 ? hx + 1 : hx - 12, y + 1, 12, 3, {255, 236, 190}, 41 * r.lit >> 8);
}

// Does a car on screen from a to b (ms from now) miss the chase on its lane?
bool laneFree(int a, int b, int w0, int w1) {
  for (int k = -2; k <= 2; k++) {
    int s = -dr->mim + k * 60000 + w0, e = -dr->mim + k * 60000 + w1;
    if (a < e && b > s) return false;
  }
  return true;
}

void drawTraffic(Canvas& cv) {
  const Drive& r = *dr;
  int64_t sc = (int64_t)r.fms * 46 / 1000;  // near lane, heading right
  for (int64_t c = fdiv(-20 - sc, 84) - 1; c <= fdiv(260 - sc, 84) + 1; c++) {
    uint32_t u = (uint32_t)c;
    if ((hsh(u * 5 + 1) & 255) > 140) continue;
    int x = (int)(c * 84 + (int64_t)(hsh(u * 5 + 2) % 30) + sc);
    if (x < -20 || x > 260) continue;
    if (!laneFree((-20 - x) * 1000 / 46, (260 - x) * 1000 / 46, -600, CHASE_LEN + 200)) continue;
    car(cv, x, 166, 1, hsh(u * 5 + 3) % 7);
  }
  sc = (int64_t)r.fms * 38 / 1000;  // far lane, heading left
  for (int64_t c = fdiv(-20 + sc, 90) - 1; c <= fdiv(260 + sc, 90) + 1; c++) {
    uint32_t u = (uint32_t)c;
    if ((hsh(u * 7 + 1) & 255) > 128) continue;
    int x = (int)(c * 90 + (int64_t)(hsh(u * 7 + 2) % 30) - sc);
    if (x < -20 || x > 260) continue;
    if (!laneFree((x - 260) * 1000 / 38, (x + 20) * 1000 / 38, CHASE_B - 500, CHASE_B + CHASE_LEN + 500)) continue;
    car(cv, x, 160, -1, hsh(u * 7 + 3) % 7);
  }
}

// The chase: a sports car with two police cars behind, along the drive and back.
bool chaseRun(int run, int& lead, int& y, int& dir) {
  int s = dr->mim - (run ? CHASE_B : 0);
  if (s < 0 || s > CHASE_LEN) return false;
  dir = run ? -1 : 1;
  y = run ? 159 : 165;
  lead = run ? 262 - 170 * s / 1000 : -22 + 170 * s / 1000;
  return true;
}

void drawChase(Canvas& cv) {
  for (int run = 0; run < 2; run++) {
    int lead, y, dir;
    if (!chaseRun(run, lead, y, dir)) continue;
    police(cv, lead - dir * 40, y, dir, 0);
    police(cv, lead - dir * 74, y, dir, 1);
    sportsCar(cv, lead, y, dir);
  }
}

int signX(int i) {
  const Hotel& H = HOT[i];
  if (H.sg == SG_FIN) return H.x + H.fx + H.fw / 2;
  if (H.st == ST_CAFE) return H.x + H.w - 6;
  return H.x + H.w / 2;
}

void drawRoad(Canvas& cv) {
  const Drive& r = *dr;
  if (!cv.rows(BASE, SAND_Y)) return;
  cv.rect(cv.x0, BASE, cv.x1 - cv.x0, 2, r.walk);
  cv.rect(cv.x0, BASE + 2, cv.x1 - cv.x0, 10, r.asph);
  if (r.wet && r.sgn > 12) {  // the neon in the wet road
    for (int i = 0; i < HOTELS; i++) {
      int sx = signX(i);
      if (!cv.hits(sx - 3, BASE + 2, 6, 10)) continue;
      for (int y = BASE + 2; y < BASE + 12; y++) {
        int sv = isin((int)(r.fms * 256 / 1634) + y * 69), o = sv > 63 ? 1 : (sv < -63 ? -1 : 0);
        cv.add(sx - 2 + o, y, 4, HOT[i].n, (97 * r.sgn >> 8) * (14 - (y - BASE - 2)) / 14);
      }
    }
  }
  for (int x = 0; x < 240; x += 12) cv.rect(x + 3, 165, 6, 1, r.lane);
  if (r.wet) {
    for (int i = 0; i < 14; i++) {
      if ((hsh(i * 5 + r.fms / 120) & 255) < 128) continue;
      cv.mix(hsh(i * 5 + 1) % 240, 161 + hsh(i * 5 + 2) % 9, 1, {220, 230, 255}, 154);
    }
  }
  drawTraffic(cv);
  drawChase(cv);
  cv.rect(cv.x0, BASE + 12, cv.x1 - cv.x0, 2, r.curb);
  cv.rect(cv.x0, BASE + 14, cv.x1 - cv.x0, 2, r.curb2);
}

// ---- the beach ----

void drawBeach(Canvas& cv) {
  const Drive& r = *dr;
  if (!cv.rows(SAND_Y - 12, SEA_Y)) return;
  if (cv.rows(SAND_Y, SEA_Y)) {
    cv.rect(cv.x0, SAND_Y, cv.x1 - cv.x0, 12, r.sand);
    cv.rect(cv.x0, SAND_Y + 12, cv.x1 - cv.x0, 4, r.sandWet);
    for (int i = 0; i < 80; i++) {
      int x = hsh(i * 3 + 1) % 240, y = 175 + hsh(i * 3 + 2) % 14;
      cv.px(x, y, y >= SAND_Y + 12 ? r.sandWetDot : r.sandDot);
    }
    if (r.d > 102 && !r.wet && !r.snow) {  // beach umbrellas by day
      static const struct { uint8_t x; Rgb a, b; } UMB[3] = {
          {24, {255, 90, 150}, {255, 240, 120}}, {120, {60, 200, 230}, {255, 255, 255}}, {204, {255, 170, 60}, {255, 255, 255}}};
      for (const auto& u : UMB) {
        if (!cv.hits(u.x - 4, 178, 13, 9)) continue;
        for (int k = -4; k <= 4; k++)
          cv.rect(u.x + k, 178 + (abs(k) > 2 ? 1 : 0), 1, abs(k) > 2 ? 1 : 2, c565((abs(k) >> 1) & 1 ? u.a : u.b));
        cv.rect(u.x, 180, 1, 6, rgb(90, 80, 80));
        cv.rect(u.x + 3, 185, 6, 2, c565(u.a));
      }
    }
  }
  const int hx = 70, hy = 175;  // lifeguard hut
  if (cv.hits(hx - 9, hy - 10, 27, 26)) {
    cv.rect(hx + 1, hy + 8, 1, 8, r.hutStilt);
    cv.rect(hx + 11, hy + 8, 1, 8, r.hutStilt);
    cv.rect(hx - 1, hy + 7, 15, 1, r.hutStilt);
    cv.rect(hx, hy + 1, 13, 6, r.hutPink);
    cv.rect(hx + 3, hy + 2, 7, 2, r.hutWin);
    cv.rect(hx - 1, hy, 15, 1, r.hutRoof);
    cv.rect(hx + 1, hy - 1, 11, 1, r.hutRoof);
    cv.rect(hx + 3, hy - 2, 7, 1, r.hutRoof);
    for (int k = 0; k < 8; k++) cv.px(hx - 1 - k, hy + 7 + k, r.hutStilt);
    cv.rect(hx + 12, hy - 9, 1, 9, r.hutStilt);
    for (int k = 0; k < 4; k++) cv.rect(hx + 13 + k, hy - 9 + r.flagOff[k], 1, 3, r.flagC);
  }
}

void drawPalm(Canvas& cv, int i) {
  const Drive& r = *dr;
  const Area& b = r.palmShown[i];
  if (!cv.hits(b.x, b.y, b.w, b.h)) return;
  const Pose& p = r.pose[i];
  int h = PALM[i].h;
  for (int y = max(cv.y0, BY - h); y <= min(cv.y1 - 1, BY); y++) {
    int s = BY - y, off = p.top16 * s * s / (h * h * 16), w = s * 10 < h * 4 ? 3 : 2;
    cv.rect(PALM[i].x + off - w / 2, y, w, 1, (s / 3) & 1 ? r.trunk : r.trunk2);
  }
  for (int j = 0; j < FRONDS; j++) {
    int ln = p.ln[j], lx = p.cx, ly = p.cy;
    for (int k = 0; k < p.kn[j]; k++) {
      int x = p.cx + p.kx[j][k], y = p.cy + p.ky[j][k], s = min(2 * (k + 1), ln);
      cv.line(lx, ly, x, y, r.frond);
      if (s * 100 < ln * 35) cv.line(lx + 1, ly, x + 1, y, r.frond);
      if (s > 2) cv.rect(x, y + 1, 1, max(1, (34 * (ln - s) + 5 * ln) / (10 * ln)), r.frond);
      lx = x;
      ly = y;
    }
  }
  cv.rect(p.cx - 2, p.cy - 1, 5, 3, r.frond);
  if (r.d > 128) {
    cv.rect(p.cx - 2, p.cy + 2, 2, 2, r.nut);
    cv.rect(p.cx + 1, p.cy + 2, 2, 2, r.nut);
  }
}

// ---- the ocean ----

bool boatAt(int& x) {
  static const int AT[2] = {15000, 45000};
  int m = dr->mim;
  for (int s : AT)
    if (m >= s && m < s + BOAT_LEN) {
      x = 250 - 300 * (m - s) / BOAT_LEN;
      return true;
    }
  return false;
}

void drawSea(Canvas& cv) {
  const Drive& r = *dr;
  if (!cv.rows(SEA_Y - 3, HUD_Y)) return;
  for (int y = max(cv.y0, SEA_Y); y < min(cv.y1, HUD_Y); y++) cv.rect(cv.x0, y, cv.x1 - cv.x0, 1, r.sea[y - SEA_Y]);
  int rough = r.storm ? 2 : 1;
  for (int y = SEA_Y + 2; y < HUD_Y; y += 2) {  // waves
    if (y < cv.y0 || y >= cv.y1) continue;
    for (int k = 0; k < 5 * rough; k++) {
      float sp = (3 + (y - SEA_Y) * 0.25f) * ((k & 1) ? 1 : -1) * rough;
      int x = imod((int)(hsh(y * 31 + k) % 300) + (int)(r.ft * sp), 300) - 30;
      int w = 3 + (int)(hsh(y * 31 + k + 7) % (6 + (y - SEA_Y) / 2));
      cv.mix(x, y, w, r.waveC, 90);
    }
  }
  if (r.sunA > 8 && cv.hits(100, SEA_Y, 100, 24)) {  // the sunset on the water
    for (int y = max(cv.y0, SEA_Y + 1); y < min(cv.y1, HUD_Y); y++) {
      int spread = 6 + (y - SEA_Y) * 16 / 10, a = (128 * r.sunA >> 8) * (40 - (y - SEA_Y)) / 40;
      for (int j = 0; j < 3; j++) {
        int x = SUN_X + ((int)(hsh(y * 7 + j + (r.fms / 350) * 13) & 255) - 128) * spread / 128;
        cv.add(x, y, 2 + hsh(y * 5 + j) % 5, {255, 170, 90}, a);
      }
    }
  }
  int spk = r.d * r.sa >> 8;
  if (spk > 26)
    for (int i = 0; i < 26; i++)
      if ((hsh(i * 11 + (r.fms / 300) * 7) & 255) < 128)
        cv.mix(hsh(i * 11 + 1) % 240, SEA_Y + 2 + hsh(i * 11 + 2) % 21, 1, WHITE, 205 * spk >> 8);
  if (r.sgn > 12) {  // the neon on the water
    for (int i = 0; i < HOTELS; i++) {
      int sx = signX(i);
      if (!cv.hits(sx - 6, SEA_Y + 2, 12, 22)) continue;
      for (int y = max(cv.y0, SEA_Y + 2); y < min(cv.y1, HUD_Y); y++) {
        if ((hsh(y * 3 + sx + r.fms / 220) & 255) < 115) continue;
        int w = 2 + hsh(y + sx) % 4, o = isin((int)(r.fms * 256 / 2513) + y * 41) * 3 / 254;
        cv.add(sx - w / 2 + o, y, w, HOT[i].n, (102 * r.sgn >> 8) * (34 - (y - SEA_Y)) / 34);
      }
    }
  }
  if (r.moon && cv.hits(194, SEA_Y, 10, 24))
    for (int y = SEA_Y + 2; y < HUD_Y; y += 2)
      if ((hsh(y + r.fms / 300) & 255) > 77) cv.add(196 + isin((int)(r.fms * 256 / 3141) + y * 40) * 2 / 127, y, 4, {240, 236, 220}, 77);
  if (cv.rows(SEA_Y - 2, SEA_Y + 3)) {  // surf on the sand
    int surge = (int)lroundf(sinf(r.ft / 2.6f) * 1.6f * 127), p1 = (int)(r.fms % 6911) * 256 / 6911, p2 = (int)(r.fms % 4398) * 256 / 4398;
    for (int x = max(cv.x0, 0); x < min(cv.x1, 240); x++) {
      int v = isin((x * 4528 >> 10) + p1) * 7 / 10 + surge, yf = SEA_Y + (v >= 0 ? v + 63 : v - 63) / 127;
      cv.mix(x, yf, 1, WHITE, 70 + isin((x * 8344 >> 10) + p2) * 70 / 127);
    }
  }
  int bx;
  if (boatAt(bx) && cv.hits(bx - 3, 197, 63, 16)) {
    const int y = 201;
    for (int k = 0; k < 44; k++) {
      int a = 166 * (44 - k) / 44, sp = k * 28 / 100;
      cv.mix(bx + 15 + k, y + 3 - sp / 2, 1, WHITE, a);
      cv.mix(bx + 15 + k, y + 4 + sp * 6 / 10, 1, WHITE, a * 4 / 5);
    }
    cv.rect(bx + 5, y - 2, 5, 1, rgb(40, 56, 90));
    cv.rect(bx + 4, y - 1, 8, 1, r.boatC);
    cv.rect(bx + 1, y, 15, 1, r.boatC);
    cv.rect(bx, y + 1, 16, 1, rgb(255, 60, 150));
    cv.rect(bx + 2, y + 2, 14, 1, rgb(28, 24, 44));
    cv.mix(bx - 1, y + 1, 1, WHITE, 230);
    cv.mix(bx - 2, y, 1, WHITE, 154);
    if (r.lit > 77) {
      cv.px(bx + 1, y, rgb(255, 40, 40));
      cv.px(bx + 14, y, rgb(40, 255, 90));
      cv.addRect(bx, y - 1, 3, 3, {255, 40, 40}, 77);
      cv.addRect(bx + 13, y - 1, 3, 3, {40, 255, 90}, 77);
    }
  }
}

// ---- the helicopter ----

void drawHeli(Canvas& cv) {
  const Drive& r = *dr;
  if (!r.heliOn) return;
  int x = r.heliX, y = r.heliY;
  if (r.beamOn && cv.hits(r.beamShown.x, r.beamShown.y, r.beamShown.w, r.beamShown.h)) {
    int sx = r.spotX, sy = r.spotY, y0 = y + 5;
    for (int yy = max(cv.y0, y0); yy < min(cv.y1, sy); yy++) {
      int f = (yy - y0) * 256 / max(1, sy - y0), xl = x + 3 + ((sx - 9) - (x + 3)) * f / 256, xr = x + 6 + ((sx + 9) - (x + 6)) * f / 256;
      cv.add(xl, yy, xr - xl, {230, 236, 255}, 26 * r.lit >> 8);
    }
    static const uint8_t OUT[6] = {10, 10, 9, 8, 6, 3}, INN[3] = {5, 4, 2};
    for (int k = -5; k <= 5; k++) {
      cv.add(sx - OUT[abs(k)], sy + k, 2 * OUT[abs(k)] + 1, {255, 255, 240}, 90 * r.lit >> 8);
      if (abs(k) < 3) cv.add(sx - INN[abs(k)], sy + k, 2 * INN[abs(k)] + 1, {255, 255, 240}, 77 * r.lit >> 8);
    }
  }
  if (!cv.hits(x - 7, y - 3, 26, 10)) return;
  cv.rect(x, y, 9, 4, r.heliC);
  cv.rect(x + 1, y + 1, 3, 2, rgb(110, 190, 220));
  cv.rect(x + 9, y + 1, 9, 1, r.heliC);
  cv.rect(x + 17, y - 1, 1, 3, r.heliC);
  cv.rect(x + 16, y - 2 + ((r.fms / 50) & 1) * 2, 3, 1, rgb(150, 150, 160));
  cv.rect(x, y + 5, 10, 1, r.heliC);
  cv.px(x + 2, y + 4, r.heliC);
  cv.px(x + 7, y + 4, r.heliC);
  int mr = (r.fms / 40) % 3;
  cv.rect(x - 6 + mr, y - 2, 21 - mr * 2, 1, rgb(120, 120, 135));
  cv.px(x + 4, y - 1, r.heliC);
  if ((r.fms / 500) & 1) {
    cv.px(x + 5, y + 4, rgb(255, 50, 50));
    cv.addRect(x + 4, y + 3, 3, 3, {255, 50, 50}, 77);
  }
}

void drawPrecip(Canvas& cv) {
  const Drive& r = *dr;
  if (r.wet) {
    int sl = r.storm ? -5 : -2;
    for (int i = 0; i < DROPS; i++) {
      int x = r.dropX[i], y = r.dropY[i];
      if (!cv.hits(min(x, x + sl), y, abs(sl) + 1, 8)) continue;
      for (int k = 0; k < 8; k++) cv.mix(x + sl * k / 7, y + k, 1, r.rainC, 102);
    }
  } else if (r.snow) {
    for (int i = 0; i < FLAKES; i++) {
      int x = r.dropX[i], y = r.dropY[i];
      if (cv.hits(x, y, 2, 1)) cv.mix(x, y, (hsh(i) & 255) < 77 ? 2 : 1, {250, 250, 255}, 217);
    }
  }
  if (r.fog) cv.mixRect(cv.x0, max(cv.y0, SCENE_Y0), cv.x1 - cv.x0, min(cv.y1, HUD_Y) - max(cv.y0, SCENE_Y0), r.fogC, 36);
}

void drawAll(Canvas& cv) {
  const Drive& r = *dr;
  drawSky(cv);
  drawClouds(cv);
  drawGulls(cv);
  drawSkyline(cv);
  for (int i = 0; i < HOTELS; i++) drawHotel(cv, i);
  if (r.hazeA > 4 && cv.hits(80, 58, 140, 140)) cv.glow(SUN_X, 128, 70, {255, 110, 140}, r.hazeA);
  if (r.fog) cv.mixRect(0, SCENE_Y0, 240, SKY_Y1 - SCENE_Y0, r.fogC, 72);
  drawRoad(cv);
  drawBeach(cv);
  for (int i = 0; i < PALMS; i++) drawPalm(cv, i);
  drawSea(cv);
  drawHeli(cv);
  drawPrecip(cv);
}

}  // namespace

class OceanDrive : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band = new TFT_eSprite(&tft);  // allocated while this face is up, so other faces get the RAM back
    box = new TFT_eSprite(&tft);
    band->setColorDepth(16);
    bandOk = band->createSprite(240, STRIP_H) != nullptr;
    box->setColorDepth(16);
    boxOk = box->createSprite(BOX_W, BOX_H) != nullptr;
    dr = (Drive*)calloc(1, sizeof(Drive));
    tft.fillScreen(O_BG);
    drawHud();
    shownRight = -1;
    shownBars = -1;
    wxDrawn = false;
    if (!dr) return;

    Drive& r = *dr;
    for (int i = 0; i < 256; i++) r.sinT[i] = (int8_t)lroundf(127 * sinf(i * 2 * PIF / 256));
    for (int i = 0; i < TOWERS; i++) {
      r.twX[i] = (uint8_t)(i * 84 / 10 + hsh(i * 3 + 1) % 6);
      r.twW[i] = 5 + hsh(i * 3 + 2) % 6;
      r.twH[i] = 14 + hsh(i * 3 + 3) % 30 + ((hsh(i * 3 + 4) & 255) < 51 ? 16 : 0);
    }
    for (int k = 0; k <= SUN_R; k++) r.sunHW[k] = (uint8_t)sqrtf((float)(SUN_R * SUN_R - k * k));
    for (int i = 0; i < CLOUDS; i++) r.cloudX[i] = hsh(i * 9 + 3) % 400;
    for (int i = 0; i < GULLS; i++) r.gullX[i] = hsh(i * 17) % 320;
    findTVs();
    r.kind = CNW_CLOUD;
    r.d = 256;
    r.rise = r.set = -1;
    setLight(true);
    uint32_t ms = millis();
    lastMs = ms;
    nextFlash = ms + 2000;
    stepScene(ms, msIntoMin(ms), true);
    r.qn = 0;
    for (bool& d : dirty) d = true;
  }

  void end() override {
    band->deleteSprite();
    box->deleteSprite();
    delete band;
    delete box;
    band = box = nullptr;
    bandOk = boxOk = false;
    free(dr);
    dr = nullptr;
  }

  void update(const FaceState& s, const FaceState& prev) override {
    bool minute = strcmp(s.time, prev.time) != 0;
    if (minute || s.second != prev.second) drawTime(s);
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 118, textWidth2);
      tft.fillRect(6, 32, 122, 16, O_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(O_PINK, O_BG);
      tft.drawString(city, 6, 32, 2);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate();
    bool wxChanged = weatherChanged(s, prev) || s.code != prev.code;
    if (!dr) {
      if (wxChanged) drawWeather(s);
      return;
    }
    bool wasNight = dr->night;
    if (wxChanged) dr->kind = s.weatherValid ? cnWeather(s.code) : CNW_CLOUD;
    if (minute) {
      sunLight();
      sunTimes();
      shownRight = -1;
    }
    if (wxChanged || minute) setLight(wxChanged);
    if (wxChanged || dr->night != wasNight || !wxDrawn) drawWeather(s);
    wxDrawn = true;
  }

  void tick(uint32_t ms) override {
    if (!dr || !bandOk) return;
    int mim = msIntoMin(ms);
    stepScene(ms, mim, false);
    flush();
    for (int i = 0; i < dr->qn; i++) paintArea(dr->q[i]);
    dr->qn = 0;
    drawFoot(mim, ms);
  }

 private:
  TFT_eSprite* band = nullptr;
  TFT_eSprite* box = nullptr;
  bool bandOk = false, boxOk = false, wxDrawn = false;
  bool dirty[STRIPS] = {};
  uint16_t flushed = 0;  // strips repainted this tick
  int shownRight = -1, shownBars = -1, lightSig = -1;
  uint32_t lastMs = 0, lastRoad = 0, lastSea = 0, lastPalm = 0, lastSky = 0, lastStar = 0, lastTV = 0;
  int palmNext = 0;
  uint32_t lastFlag = 0, lastPrecip = 0, lastFlick = 0, lastBeacon = 0, lastWin = 0, nextFlash = 0, flashEnd = 0;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // Where the sun is over your city: daylight, how close it is to the horizon, and which side of noon.
  void sunLight() {
    Drive& r = *dr;
    r.d = 256;
    r.tw = 0;
    r.night = false;
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm u;
    gmtime_r(&now, &u);
    float doy = u.tm_yday + 1, utcH = u.tm_hour + u.tm_min / 60.0f;
    float B = 360 * DEG * (doy - 81) / 365;
    float eot = 9.87f * sinf(2 * B) - 7.53f * cosf(B) - 1.5f * sinf(B);  // equation of time, minutes
    float dec = -23.44f * DEG * cosf(360 * DEG * (doy + 10) / 365);
    float lat = settings.lat * DEG;
    float hd = (utcH + settings.lon / 15 + eot / 60 - 12) * 15;  // sun's hour angle, degrees
    float el = asinf(sinf(lat) * sinf(dec) + cosf(lat) * cosf(dec) * cosf(hd * DEG)) / DEG;
    r.d = (int)(constrain((el + 4) / 10, 0.0f, 1.0f) * 256);
    r.tw = (int)(constrain(1 - fabsf(el - 1) / 7, 0.0f, 1.0f) * 256);
    r.dawn = fmodf(hd + 540, 360) < 180;
    r.night = r.d < 77;
  }

  // Today's sunrise and sunset over your city, in local minutes.
  void sunTimes() {
    Drive& r = *dr;
    r.rise = r.set = -1;
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm u, l;
    gmtime_r(&now, &u);
    localtime_r(&now, &l);
    int dd = l.tm_yday - u.tm_yday;
    if (dd > 1) dd = -1;
    else if (dd < -1) dd = 1;
    int off = (l.tm_hour * 60 + l.tm_min) - (u.tm_hour * 60 + u.tm_min) + dd * 1440;
    float doy = u.tm_yday + 1, B = 360 * DEG * (doy - 81) / 365;
    float eot = 9.87f * sinf(2 * B) - 7.53f * cosf(B) - 1.5f * sinf(B);
    float dec = -23.44f * DEG * cosf(360 * DEG * (doy + 10) / 365), lat = settings.lat * DEG;
    float cosH = (sinf(-0.833f * DEG) - sinf(lat) * sinf(dec)) / (cosf(lat) * cosf(dec));
    if (cosH <= -1 || cosH >= 1) return;
    float H = acosf(cosH) / DEG, noon = 720 - 4 * settings.lon - eot;
    r.rise = (int16_t)imod((int)lroundf(noon - 4 * H) + off, 1440);
    r.set = (int16_t)imod((int)lroundf(noon + 4 * H) + off, 1440);
  }

  // Every colour in the scene from the weather and the light; repaints all when they change.
  void setLight(bool force) {
    Drive& r = *dr;
    int k = r.kind;
    r.lit = 256 - r.d;
    r.sgn = constrain((r.lit - 77) * 256 / 102, 0, 256);
    r.storm = k == CNW_THUNDER;
    r.wet = k == CNW_RAIN || r.storm;
    r.snow = k == CNW_SNOW;
    r.fog = k == CNW_FOG;
    r.heavy = r.wet || r.snow || k == CNW_OVERCAST;
    r.dim = k == CNW_SUNNY ? 0 : k == CNW_CLOUD ? 90 : k == CNW_OVERCAST ? 128 : k == CNW_FOG ? 115 : 141;
    r.sa = k == CNW_SUNNY ? 256 : k == CNW_CLOUD ? 102 : 0;
    r.wind10 = r.storm ? 45 : k == CNW_RAIN ? 24 : (k == CNW_SNOW || k == CNW_OVERCAST) ? 16 : k == CNW_CLOUD ? 14 : 10;
    int sig = r.d * 7 + r.tw * 3 + (r.dawn ? 1 : 0) + k * 10000;
    if (!force && sig == lightSig) return;
    lightSig = sig;
    int d = r.d, tw = r.tw;
    r.grey = lerpRgb(lerpRgb({14, 12, 26}, r.storm ? Rgb{84, 86, 104} : Rgb{156, 160, 172}, d), {90, 60, 96}, tw / 2);
    r.fogC = lerpRgb(lerpRgb({60, 50, 84}, {210, 212, 222}, d), {190, 140, 170}, tw / 2);

    for (int y = 0; y < SKY_Y1 - SCENE_Y0; y++) {
      int f = y * 256 / (SKY_Y1 - SCENE_Y0);
      Rgb nt = f < 154 ? lerpRgb({8, 6, 30}, {30, 14, 66}, f * 256 / 154) : lerpRgb({30, 14, 66}, {86, 26, 100}, (f - 154) * 256 / 102);
      Rgb dy = lerpRgb({44, 116, 214}, {176, 222, 248}, f);
      Rgb du = f < 108 ? lerpRgb({34, 18, 84}, {170, 44, 140}, f * 256 / 108)
                       : f < 200 ? lerpRgb({170, 44, 140}, {255, 104, 110}, (f - 108) * 256 / 92)
                                 : lerpRgb({255, 104, 110}, {255, 178, 84}, (f - 200) * 256 / 56);
      Rgb da = f < 128 ? lerpRgb({46, 58, 132}, {206, 128, 176}, f * 2) : lerpRgb({206, 128, 176}, {255, 212, 156}, (f - 128) * 2);
      r.sky[y] = c565(lerpRgb(K(nt, dy, du, da), r.grey, r.dim));
    }
    r.starA = ((256 - d) * (256 - (tw * 218 >> 8)) >> 8) * (k == CNW_SUNNY ? 256 : k == CNW_CLOUD ? 90 : 0) >> 8;
    r.moon = k == CNW_SUNNY && d < 64 && tw < 64;
    r.sunA = tw * r.sa >> 8;
    r.daySunA = (d * max(0, 256 - tw * 2) >> 8) * r.sa >> 8;  // gone once the striped sun is up
    r.hazeA = 41 * r.sunA >> 8;
    r.clouds = k == CNW_SUNNY ? 5 : k == CNW_CLOUD ? 11 : k == CNW_FOG ? 0 : k == CNW_SNOW ? 9 : 13;
    r.cloudTop = K({30, 22, 56}, {250, 250, 255}, {255, 150, 176}, {255, 200, 190});
    r.cloudUnd = K({40, 26, 64}, {214, 222, 236}, {255, 190, 110}, {255, 230, 190});
    if (r.heavy) {
      r.cloudTop = lerpRgb(r.cloudTop, r.grey, 154);
      r.cloudUnd = lerpRgb(r.cloudUnd, r.grey, 154);
    }
    r.cloudA = k == CNW_SUNNY ? 218 : 243;
    r.gullC = c565(K({30, 20, 40}, {70, 70, 84}, {60, 24, 64}));

    Rgb tw0 = K({22, 14, 48}, {146, 168, 204}, {104, 40, 118}, {150, 120, 170});
    r.twC = c565(tw0);
    r.twEdge = c565(lerpRgb(tw0, WHITE, 20));
    r.twWarm = c565(lerpRgb(tw0, {255, 206, 140}, 141 * r.lit >> 8));
    r.twCool = c565(lerpRgb(tw0, {150, 206, 255}, 141 * r.lit >> 8));
    for (int i = 0; i < 3; i++) r.twNeon[i] = c565(lerpRgb(tw0, TNEON[i], 230 * r.sgn >> 8));

    Rgb glass = K({24, 16, 44}, {86, 136, 170}, {64, 40, 96}, {200, 150, 150}), warm = {255, 204, 138};
    r.glass = c565(glass);
    r.warmWin = c565(lerpRgb(glass, warm, 230 * r.lit >> 8));
    r.glint = c565(lerpRgb(glass, {230, 246, 255}, 141 * d >> 8));
    r.cafeWin = c565(lerpRgb(glass, {255, 186, 120}, 192 * r.lit >> 8));
    for (int i = 0; i < HOTELS; i++) {
      Rgb c = HOT[i].c;
      Rgb fc = K(lerpRgb(c, {16, 10, 34}, 215), c, lerpRgb(c, {92, 46, 120}, 143), lerpRgb(c, {255, 194, 160}, 77));
      fc = lerpRgb(fc, r.grey, r.dim * 77 >> 8);
      Rgb hi = lerpRgb(fc, WHITE, 36), dk = lerpRgb(fc, BLACK, 108), tube = lerpRgb(fc, HOT[i].n, 77);
      r.hFc[i] = c565(fc);
      r.hSh[i] = c565(lerpRgb(fc, BLACK, 56));
      r.hHi[i] = c565(hi);
      r.hDk[i] = c565(dk);
      r.hTube[i] = c565(tube);
      r.hCore[i] = lerpRgb(HOT[i].n, WHITE, 77);
      r.hFin[i] = c565(lerpRgb(hi, {18, 10, 32}, 179 * r.sgn >> 8));
      r.hUnlit[i] = c565(lerpRgb(HOT[i].sg == SG_FIN ? un565(r.hFin[i]) : fc, {40, 40, 60}, 128));
      r.hDoor[i] = c565(lerpRgb(dk, warm, 179 * r.lit >> 8));
    }
    r.awnA = c565(K({44, 22, 54}, {255, 250, 246}, {170, 120, 160}, {255, 236, 226}));
    r.awnB = c565(K({66, 16, 54}, {255, 106, 168}, {200, 60, 140}, {255, 140, 170}));
    r.flamDay = c565(K({70, 30, 70}, {255, 140, 186}, {150, 60, 120}, {255, 150, 180}));

    r.walk = c565(K({34, 24, 52}, {206, 198, 188}, {118, 74, 120}, {226, 194, 172}));
    Rgb asph = K({14, 12, 26}, {74, 76, 88}, {44, 30, 62}, {112, 96, 112});
    if (r.wet) asph = lerpRgb(lerpRgb(asph, BLACK, 77), un565(r.sky[90]), 46);
    r.asph = c565(lerpRgb(asph, {255, 60, 170}, 13 * r.sgn >> 8));
    r.lane = c565(K({66, 58, 40}, {236, 210, 100}, {196, 136, 96}, {240, 200, 120}));
    r.curb = c565(K({46, 34, 66}, {228, 222, 208}, {164, 110, 144}, {244, 216, 192}));
    r.curb2 = c565(K({30, 22, 46}, {196, 188, 172}, {122, 80, 114}, {214, 182, 160}));
    for (int i = 0; i < 7; i++) {
      Rgb c = CARC[i];
      r.carC[i] = c565(K(lerpRgb(c, {14, 10, 30}, 200), c, lerpRgb(c, {96, 50, 112}, 128), lerpRgb(c, {255, 200, 170}, 51)));
    }
    r.carGlass = c565(K({20, 18, 36}, {60, 80, 110}, {50, 34, 70}));
    r.polWhite = c565(K({110, 110, 130}, {244, 244, 248}, {196, 170, 200}, {255, 236, 226}));
    Rgb sp = K({190, 26, 80}, {255, 44, 104}, {240, 40, 100}, {255, 70, 110});
    r.sports = c565(sp);
    r.sportsDk = c565(lerpRgb(sp, BLACK, 115));

    Rgb sand = K({34, 24, 52}, {240, 216, 172}, {176, 112, 138}, {255, 204, 160});
    if (r.snow) sand = lerpRgb(sand, {232, 234, 244}, 102);
    Rgb wetSand = lerpRgb(sand, BLACK, 31);
    r.sand = c565(sand);
    r.sandDot = c565(lerpRgb(sand, BLACK, 20));
    r.sandWet = c565(wetSand);
    r.sandWetDot = c565(lerpRgb(wetSand, BLACK, 20));
    r.hutPink = c565(K({60, 26, 58}, {255, 128, 178}, {180, 70, 130}, {255, 150, 170}));
    r.hutRoof = c565(K({20, 40, 50}, {60, 214, 206}, {70, 90, 130}, {110, 220, 200}));
    r.hutStilt = c565(K({40, 30, 50}, {240, 236, 226}, {150, 110, 140}, {250, 230, 214}));
    r.hutWin = c565(lerpRgb({20, 16, 30}, {255, 200, 130}, r.lit > 77 ? 204 * r.lit >> 8 : 0));
    Rgb fl = r.wet ? Rgb{255, 50, 50} : Rgb{255, 220, 60};
    r.flagC = c565(K(lerpRgb(fl, {20, 10, 30}, 179), fl, lerpRgb(fl, {120, 50, 90}, 102)));

    Rgb tr = K({18, 10, 30}, {150, 118, 90}, {46, 18, 54}, {190, 134, 100});
    r.trunk = c565(tr);
    r.trunk2 = c565(lerpRgb(tr, BLACK, 64));
    r.frond = c565(K({10, 8, 22}, {46, 126, 66}, {38, 12, 48}, {100, 128, 70}));
    r.nut = c565(K({20, 12, 20}, {110, 76, 40}, {40, 16, 40}));

    Rgb top = K({16, 12, 48}, {60, 190, 214}, {120, 50, 128}, {200, 150, 170});
    Rgb bot = K({6, 6, 24}, {18, 104, 170}, {54, 20, 84}, {100, 80, 140});
    for (int y = 0; y < HUD_Y - SEA_Y; y++) r.sea[y] = c565(lerpRgb(top, bot, y * 256 / (HUD_Y - SEA_Y)));
    r.waveC = K({40, 36, 90}, {170, 236, 246}, {200, 110, 170}, {255, 210, 200});
    r.boatC = c565(K({150, 150, 170}, {248, 248, 252}, {230, 200, 220}));
    r.heliC = c565(K({22, 22, 34}, {70, 74, 90}, {40, 26, 56}));
    r.rainC = lerpRgb({150, 150, 210}, {225, 235, 250}, d);
    mark(SCENE_Y0, HUD_Y);
  }

  // Windows that show a flickering TV after dark, so they can be repainted as they flicker.
  void findTVs() {
    Drive& r = *dr;
    r.tvN = 0;
    for (int i = 0; i < HOTELS; i++) {
      const Hotel& H = HOT[i];
      int top = BASE - H.h;
      if (H.st == ST_PORT || H.st == ST_CAFE) continue;
      bool round = H.st == ST_ROUND;
      for (int y = top + (round ? 11 : 6); y < BASE - (round ? 8 : 9); y += round ? 6 : 7)
        for (int xx = H.x + (round ? 2 : 4); xx < H.x + H.w - (round ? 4 : 5); xx += round ? 5 : 6) {
          if ((hsh(xx * 7 + y) & 0xffff) < 7864 || (hsh(i * 131 + y * 17 + xx) & 0xffff) <= 62259 || r.tvN >= TVS) continue;
          r.tv[r.tvN++] = {(int16_t)xx, (int16_t)y, (int16_t)(round ? 4 : 3), (int16_t)(round ? 2 : 3)};
        }
    }
  }

  void mark(int a, int b) {
    for (int i = 0; i < STRIPS; i++) {
      int sy = SCENE_Y0 + i * STRIP_H;
      if (sy < b && sy + STRIP_H > a) dirty[i] = true;
    }
  }

  // Queue an area for repainting after the strips; falls back to whole strips when the queue is full.
  void push(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (dr->qn < QMAX) dr->q[dr->qn++] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    else mark(y, y + h);
  }
  void push(const Area& a) { push(a.x, a.y, a.w, a.h); }
  // Something moved: repaint where it was and where it is now.
  void moved(Area& shown, Area now) {
    int xa = min(shown.x, now.x), ya = min(shown.y, now.y);
    int xb = max(shown.x + shown.w, now.x + now.w), yb = max(shown.y + shown.h, now.y + now.h);
    if (shown.w && now.w && xb - xa <= now.w + 12 && yb - ya <= now.h + 12) {
      push(xa, ya, xb - xa, yb - ya);
    } else {
      if (shown.w) push(shown);
      if (now.w) push(now);
    }
    shown = now;
  }

  static bool due(uint32_t& last, uint32_t ms, uint32_t every, bool force, uint32_t* el = nullptr) {
    if (!force && ms - last < every) return false;
    if (el) *el = min(ms - last, (uint32_t)500);
    last = ms;
    return true;
  }

  // Advance everything that moves and note what needs repainting.
  void stepScene(uint32_t ms, int mim, bool force) {
    Drive& r = *dr;
    r.fms = ms;
    r.mim = mim;
    r.ft = (ms % 600000) / 1000.0f;
    float t = r.ft;
    uint32_t el;

    if (r.flash && (int32_t)(ms - flashEnd) >= 0) {  // lightning
      r.flash = false;
      nextFlash = ms + 2500 + random(5000);
      mark(SCENE_Y0, SKY_Y1);
    } else if (!r.flash && r.storm && (int32_t)(ms - nextFlash) >= 0) {
      r.flash = true;
      flashEnd = ms + 130;
      r.bolt[0] = 40 + random(160);
      for (int i = 1; i < 13; i++) r.bolt[i] = r.bolt[i - 1] + random(-6, 7);
      mark(SCENE_Y0, SKY_Y1);
    }

    bool chase = mim < CHASE_LEN + 300 || (mim >= CHASE_B && mim < CHASE_B + CHASE_LEN + 300);
    if (chase || due(lastRoad, ms, 100, force)) mark(BASE, BASE + 12);
    for (int run = 0; run < 2; run++)
      for (int k = 0; k < 2; k++) {
        int lead, y, dir;
        Area a = {0, 0, 0, 0};
        if (chaseRun(run, lead, y, dir)) {
          int cx = sirenX(lead - dir * (k ? 74 : 40), dir, k);
          a = {(int16_t)(cx - SIREN_R - 2), (int16_t)(y - SIREN_R), (int16_t)(2 * SIREN_R + 4), (int16_t)(2 * SIREN_R)};
        }
        if (a.w || r.polShown[run * 2 + k].w) moved(r.polShown[run * 2 + k], a);
      }

    int bx;
    if (due(lastSea, ms, boatAt(bx) ? 100 : 150, force)) mark(SEA_Y - 2, HUD_Y);

    if (force) {  // one palm per frame, so their repaints are spread out
      for (int i = 0; i < PALMS; i++) stepPalm(i, t, true);
    } else if (ms - lastPalm >= 45) {
      lastPalm = ms;
      stepPalm(palmNext, t, false);
      palmNext = (palmNext + 1) % PALMS;
    }

    if (due(lastSky, ms, 100, force, &el)) {
      float wind = r.wind10 / 10.0f;
      for (int i = 0; i < r.clouds; i++) {
        r.cloudX[i] += (1.5f + (hsh(i * 9 + 4) & 255) / 102.0f) * wind * el / 1000;
        if (r.cloudX[i] >= 400) r.cloudX[i] -= 400;
        int x = (int)r.cloudX[i] - 80, o = r.cloudShown[i];
        if (x != o || force) {
          r.cloudShown[i] = x;
          push(min(x, o), cloudY(i) - 2, cloudW(i) + abs(x - o), cloudH(i) + 4);
        }
      }
      bool gulls = !r.storm && r.d >= 64;
      for (int i = 0; i < GULLS; i++) {
        r.gullX[i] += (5 + i * 1.5f) * el / 1000;
        if (r.gullX[i] >= 320) r.gullX[i] -= 320;
        int x = (int)r.gullX[i] - 40, y = 62 + i * 8 + (int)lroundf(sinf(t / 1.1f + i * 2) * 3);
        uint8_t f = ((int)(t / 0.26f + i * 1.7f)) & 1;
        if (x == r.gullSX[i] && y == r.gullSY[i] && f == r.gullFlap[i] && !force) continue;
        if (gulls) {
          push(r.gullSX[i], r.gullSY[i], 5, 2);
          push(x, y, 5, 2);
        }
        r.gullSX[i] = x;
        r.gullSY[i] = y;
        r.gullFlap[i] = f;
      }
      stepHeli(mim);
    }

    if (r.starA > 12 && ms / 400 != lastStar) {  // twinkle: repaint the stars that dim or brighten
      lastStar = ms / 400;
      for (int i = 0; i < TWINKLE; i++)
        if ((lastStar + i * 3) % 4 <= 1) push(starX(i), starY(i), 1, 1);
    }

    if (ms / 800 != lastBeacon) {
      lastBeacon = ms / 800;
      for (int i = 0; i < TOWERS; i++)
        if (r.twH[i] > 40) push(r.twX[i] - 6 + r.twW[i] / 2, 154 - r.twH[i] - 2, 1, 2);
    }

    if (r.sgn > 12 && ms / 110 != lastFlick) {  // neon flicker
      lastFlick = ms / 110;
      uint8_t f = 0;
      for (int i = 0; i < HOTELS; i++)
        if (hsh(lastFlick * 7 + i * 977) % (i == 1 ? 9 : 53) == 0) f |= 1 << i;
      for (int i = 0; i < HOTELS; i++)
        if (((f ^ r.flick) >> i) & 1) push(hotelArea(i));
      r.flick = f;
    }

    if (r.lit > 77 && due(lastTV, ms, 250, force))
      for (int i = 0; i < r.tvN; i++) push(r.tv[i]);

    if (r.lit > 12 && ms / 9000 != lastWin) {
      lastWin = ms / 9000;
      mark(BASE - 60, BASE);
    }

    if (due(lastFlag, ms, 150, force)) {
      bool ch = false;
      for (int k = 0; k < 4; k++) {
        int8_t o = (int8_t)lroundf(sinf(t * 1000 / (130 * 10.0f / r.wind10) + k) * 0.8f);
        ch |= o != r.flagOff[k];
        r.flagOff[k] = o;
      }
      if (ch) push(83, 164, 5, 9);
    }

    if (due(lastPrecip, ms, r.snow ? 200 : 100, force)) stepPrecip(t);
  }

  // Sway one palm. Only the crown is repainted unless the trunk itself moved.
  void stepPalm(int i, float t, bool force) {
    Drive& r = *dr;
    float wind = r.wind10 / 10.0f, push0 = -(wind - 1) * 1.6f;
    float ph = PALM[i].ph / 10.0f, sc = PALM[i].h / 70.0f;
    float gust = sinf(t / 1.7f + ph) * 0.9f + sinf(t / 0.61f + ph * 2) * 0.35f;
    float sw = gust * wind * 0.9f + push0, wp = push0 * 0.8f + gust * wind * 0.6f;
    Pose p;
    memset(&p, 0, sizeof(p));
    p.top16 = (int16_t)lroundf((PALM[i].lean + sw) * 16);
    p.cx = PALM[i].x + (int16_t)lroundf(PALM[i].lean + sw);
    p.cy = BY - PALM[i].h;
    int xa = p.cx - 3, xb = p.cx + 4, ya = p.cy - 2, yb = p.cy + 5;  // the crown
    for (int j = 0; j < FRONDS; j++) {
      int ln = (int)lroundf(FR[j][1] * sc);
      float an = FR[j][0] * DEG + sinf(t / 0.65f + ph + j * 1.3f) * 0.05f * wind, dx = cosf(an), dy = sinf(an);
      p.ln[j] = ln;
      p.kn[j] = min((ln + 1) / 2, KNOTS);
      for (int k = 0; k < p.kn[j]; k++) {
        int s = min(2 * (k + 1), ln);
        float f = (float)s / ln;
        p.kx[j][k] = (int8_t)lroundf(dx * s + wp * f * f * ln / 10);
        p.ky[j][k] = (int8_t)lroundf(dy * s + 0.8f * f * f * ln);
        xa = min(xa, p.cx + p.kx[j][k] - 1);
        xb = max(xb, p.cx + p.kx[j][k] + 3);
        ya = min(ya, p.cy + p.ky[j][k] - 1);
        yb = max(yb, p.cy + p.ky[j][k] + 5);
      }
    }
    if (!force && memcmp(&p, &r.pose[i], sizeof(p)) == 0) return;
    bool trunk = force || p.top16 != r.pose[i].top16;
    r.pose[i] = p;
    Area crown = {(int16_t)xa, (int16_t)ya, (int16_t)(xb - xa), (int16_t)(yb - ya)};
    int fa = min(xa, min((int)PALM[i].x, (int)p.cx) - 3), fb = max(xb, max((int)PALM[i].x, (int)p.cx) + 4);
    Area full = {(int16_t)fa, (int16_t)ya, (int16_t)(fb - fa), (int16_t)(BY + 1 - ya)};
    if (force) {
      r.palmShown[i] = full;
    } else if (trunk) {
      moved(r.palmShown[i], full);
    } else {
      Area& o = r.crownShown[i];
      int ua = min(o.x, crown.x), ub = max(o.x + o.w, crown.x + crown.w), va = min(o.y, crown.y), vb = max(o.y + o.h, crown.y + crown.h);
      push(ua, va, ub - ua, vb - va);
      r.palmShown[i] = full;
    }
    r.crownShown[i] = crown;
  }

  void stepHeli(int mim) {
    Drive& r = *dr;
    bool on = mim >= HELI_MS && mim < HELI_MS + HELI_LEN;
    if (!on) {
      if (r.heliOn) {
        r.heliOn = r.beamOn = false;
        moved(r.heliShown, {0, 0, 0, 0});
        moved(r.beamShown, {0, 0, 0, 0});
      }
      return;
    }
    float p = (mim - HELI_MS) / (float)HELI_LEN;
    r.heliOn = true;
    r.heliX = (int16_t)(258 - 300 * p);
    r.heliY = (int16_t)(76 + lroundf(sinf(p * 8) * 3));
    moved(r.heliShown, {(int16_t)(r.heliX - 7), (int16_t)(r.heliY - 3), 26, 10});
    r.beamOn = r.lit > 64;
    if (!r.beamOn) {
      if (r.beamShown.w) moved(r.beamShown, {0, 0, 0, 0});
      return;
    }
    r.spotX = (int16_t)constrain(r.heliX + 4 + (int)lroundf(sinf(r.ft / 1.1f) * 34), 4, 236);
    r.spotY = (int16_t)(128 + lroundf(sinf(r.ft / 1.5f) * 12));
    int xa = min(r.heliX + 3, r.spotX - 10), xb = max(r.heliX + 7, r.spotX + 11);
    moved(r.beamShown, {(int16_t)xa, (int16_t)(r.heliY + 5), (int16_t)(xb - xa), (int16_t)(r.spotY + 6 - r.heliY - 5)});
  }

  void stepPrecip(float t) {
    Drive& r = *dr;
    if (r.wet) {
      int n = r.storm ? DROPS : DROPS * 2 / 3, sl = r.storm ? -5 : -2;
      for (int i = 0; i < DROPS; i++) {
        int ox = r.dropX[i], oy = r.dropY[i];
        if (i >= n) {
          r.dropY[i] = -100;
          continue;
        }
        float sp = 190 + (hsh(i * 3) & 255) * 90 / 256.0f;
        int y = SCENE_Y0 + (int)fmodf((hsh(i * 3 + 1) % 180) + t * sp, 180) - 8;
        int x = imod((int)(hsh(i * 3 + 2) % 270) + (y - SCENE_Y0) * sl / 7, 270) - 15;
        r.dropX[i] = x;
        r.dropY[i] = y;
        if (oy > -50) push(min(ox, ox + sl), oy, abs(sl) + 1, 8);
        push(min(x, x + sl), y, abs(sl) + 1, 8);
      }
    } else if (r.snow) {
      for (int i = 0; i < FLAKES; i++) {
        int ox = r.dropX[i], oy = r.dropY[i];
        float sp = 14 + (hsh(i * 3) & 255) * 12 / 256.0f;
        int y = SCENE_Y0 + (int)fmodf((hsh(i * 3 + 1) % 164) + t * sp, 164);
        int x = imod((int)(hsh(i * 3 + 2) % 250) - (int)(t * r.wind10 * 4 / 10) + (int)lroundf(sinf(t / 0.8f + i) * 3), 250) - 5;
        if (x == ox && y == oy) continue;
        r.dropX[i] = x;
        r.dropY[i] = y;
        if (abs(x - ox) <= 3 && abs(y - oy) <= 3) {
          push(min(x, ox), min(y, oy), abs(x - ox) + 2, abs(y - oy) + 1);
        } else {
          push(ox, oy, 2, 1);
          push(x, y, 2, 1);
        }
      }
    }
  }

  void flush() {
    flushed = 0;
    for (int i = 0; i < STRIPS; i++) {
      if (!dirty[i]) continue;
      int sy = SCENE_Y0 + i * STRIP_H, h = min(STRIP_H, HUD_Y - sy);
      Canvas cv{*band, 0, -sy, 0, 240, sy, sy + h, (uint16_t*)band->getPointer()};
      drawAll(cv);
      band->pushSprite(0, sy, 0, 0, 240, h);
      dirty[i] = false;
      flushed |= 1 << i;
    }
  }

  // Repaint an area in box-sized tiles, skipping tiles the strips already covered this tick.
  void paintArea(Area a) {
    int xa = max(0, (int)a.x), ya = max(SCENE_Y0, (int)a.y), xb = min(240, a.x + a.w), yb = min(HUD_Y, a.y + a.h);
    if (!boxOk) return;
    for (int by = ya; by < yb; by += BOX_H) {
      int h = min(BOX_H, yb - by);
      bool done = true;
      for (int i = (by - SCENE_Y0) / STRIP_H; i <= (by + h - 1 - SCENE_Y0) / STRIP_H; i++) done &= (flushed >> i) & 1;
      if (done) continue;
      for (int bx = xa; bx < xb; bx += BOX_W) {
        int w = min(BOX_W, xb - bx);
        Canvas cv{*box, -bx, -by, bx, bx + w, by, by + h, (uint16_t*)box->getPointer()};
        drawAll(cv);
        box->pushSprite(bx, by, 0, 0, w, h);
      }
    }
  }

  void drawTime(const FaceState& s) {
    char sec[4] = "--";
    if (s.time[0] != '-') snprintf(sec, sizeof(sec), "%02d", s.second);
    int w = tft.textWidth("88:88", 4);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(O_TIME, O_BG);
    tft.setTextPadding(w);
    tft.drawString(s.time, 6, 5, 4);
    tft.setTextColor(O_CYAN, O_BG);
    tft.setTextPadding(tft.textWidth("88", 2));
    tft.drawString(sec, 10 + w, 13, 2);
    tft.setTextPadding(0);
  }

  void drawDate() {
    static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    tft.fillRect(140, 4, 96, 16, O_BG);
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, MONTHS[t.tm_mon]);
    tft.fillRect(212, 6, 23, 12, O_CYAN);  // 12 NOV, then the weekday boxed
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(O_BG);
    tft.drawString(DAYS[t.tm_wday], 224, 12, 1);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(O_DATE);
    tft.drawString(buf, 207, 4, 2);
  }

  // Pink-to-cyan rules above and below the scene, and the fixed parts of the footer.
  void drawHud() {
    for (int x = 0; x < 240; x++) {
      tft.drawPixel(x, SCENE_Y0 - 1, c565(lerpRgb({255, 60, 170}, {60, 230, 255}, x * 256 / 239)));
      tft.drawPixel(x, HUD_Y, c565(lerpRgb({60, 230, 255}, {255, 60, 170}, x * 256 / 239)));
    }
    tft.drawRect(4, HUD_Y + 6, 52, 14, O_PINK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(O_PINK);
    tft.drawString("OCEAN DR", 30, HUD_Y + 13, 1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(O_CYAN);
    tft.drawString("SCANNER CH9", 61, HUD_Y + 13, 1);
  }

  // Weather on the beach: icon, temperature, label.
  void drawWeather(const FaceState& s) {
    tft.fillRect(130, 21, 106, 27, O_BG);
    tft.setTextDatum(MR_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(O_DIM);
      tft.drawString("BEACH NO SIGNAL", 235, 44, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    const char* lab = cnWeatherLabel(w);
    tft.setTextColor(O_ORANGE);
    tft.drawString(lab, 235, 44, 1);
    tft.setTextColor(O_DIM);
    tft.drawString("BEACH", 230 - tft.textWidth(lab, 1), 44, 1);
    char buf[8], u[2] = {s.unit, '\0'};
    snprintf(buf, sizeof(buf), "%d", s.temp);
    int tw = tft.textWidth(buf, 2), x = 235 - tft.textWidth(u, 2) - 5 - tw;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(O_ORANGE);
    tft.drawString(buf, x, 22, 2);
    tft.drawCircle(x + tw + 2, 25, 1, O_ORANGE);
    tft.drawString(u, x + tw + 5, 22, 2);
    weatherIcon(w, x - 15, 28, dr && dr->night);
  }

  // Scanner bars, then SUNSET 16:58 (SUNRISE after dark), or CODE 3 flashing during the chase.
  void drawFoot(int mim, uint32_t ms) {
    bool code3 = mim < CODE3_MS;
    int bars = (int)(ms / 140);
    if (bars != shownBars) {
      shownBars = bars;
      tft.fillRect(132, HUD_Y + 5, 16, 12, O_BG);
      for (int k = 0; k < 5; k++) {
        int h = 1 + hsh(k * 7 + bars) % (code3 ? 7 : 3);
        tft.fillRect(132 + k * 3, HUD_Y + 17 - h, 2, h, code3 ? O_PINK : O_BARDIM);
      }
    }
    int key = code3 ? 100000 + (int)(ms / 250) % 6 : 0;
    if (key == shownRight) return;
    shownRight = key;
    tft.fillRect(152, HUD_Y + 5, 86, 16, O_BG);
    tft.setTextDatum(MR_DATUM);
    if (code3) {
      int ph = (int)(ms / 250) % 6;
      if (ph % 3 == 0) return;
      tft.setTextColor(ph & 1 ? O_RED : O_BLUE);
      tft.drawString("CODE 3", 236, HUD_Y + 13, 1);
      return;
    }
    char buf[8] = "--:--";
    int nowMin = -1;
    if (timeSynced()) {
      time_t now = time(nullptr);
      struct tm l;
      localtime_r(&now, &l);
      nowMin = l.tm_hour * 60 + l.tm_min;
    }
    const Drive& r = *dr;
    bool day = r.rise >= 0 && nowMin >= r.rise && nowMin < r.set;
    int m = day ? r.set : r.rise;
    if (m >= 0) snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
    const char* lab = r.rise < 0 ? "SUNSET" : (day ? "SUNSET" : "SUNRISE");
    tft.setTextColor(O_ORANGE);
    tft.drawString(buf, 236, HUD_Y + 13, 1);
    tft.setTextColor(O_DIM);
    tft.drawString(lab, 230 - tft.textWidth(buf, 1), HUD_Y + 13, 1);
  }
};

static OceanDrive oceanDrive;
Face* faceOceanDrive() { return &oceanDrive; }
