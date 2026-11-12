#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "../config.h"
#include "../display.h"
#include "../weather.h"

// Abyss: the window of a sea-floor habitat. Light shafts fall from the surface and follow
// the weather and the real day and night over your city, jellyfish glow brighter after
// dark, a sub patrols with its headlight, and a whale passes at the top of every minute.

namespace {

constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
struct Rgb { uint8_t r, g, b; };

// ---- palette ----
const uint16_t B_BG = rgb(2, 10, 14);
const uint16_t B_AQUA = rgb(143, 246, 255), B_TEAL = rgb(25, 167, 189), B_DIM = rgb(14, 142, 163);
const uint16_t B_LINE = rgb(12, 74, 86);
const uint16_t B_AMBER = rgb(255, 184, 48), B_DATE = rgb(212, 244, 255);
const uint16_t B_BOX = rgb(25, 200, 224), B_ONBOX = rgb(0, 17, 34);
const uint16_t B_HULL = rgb(12, 31, 38), B_HULLEDGE = rgb(30, 67, 80), B_GLASSEDGE = rgb(42, 106, 122);
const uint16_t B_RIVET = rgb(47, 100, 116);
const uint16_t B_ROCK = rgb(1, 7, 11);
const uint16_t B_SUB = rgb(233, 169, 22), B_SUBDK = rgb(168, 111, 10), B_SUBGREY = rgb(90, 100, 106);
const uint16_t B_GLASS = rgb(143, 242, 255), B_RED = rgb(255, 59, 59);
const uint16_t B_HABEDGE = rgb(46, 130, 146), B_LATTICE = rgb(25, 75, 88), B_PLANT = rgb(12, 59, 46);
const uint16_t B_BEACONOFF = rgb(90, 60, 8);
// weather icon
const uint16_t B_SUN = rgb(255, 210, 122), B_CLOUD = rgb(168, 200, 232), B_CLOUDDK = rgb(100, 118, 154);
const uint16_t B_DROP = rgb(95, 180, 255), B_YELLOW = rgb(255, 210, 58);

// ---- layout ----
const int SCENE_Y0 = 50, HUD_Y = 214;
const int X0 = 7, Y0 = 55, X1 = 233, Y1 = 207;  // the glass
const int STRIP_H = 12, STRIPS = (HUD_Y - SCENE_Y0 + STRIP_H - 1) / STRIP_H;
const int BOX_W = 40, BOX_H = 44;
const int SHAFTS = 5, SHAFT_H = 128, JELLIES = 3, KELPS = 5, KNODES = 14, FISH = 10, VENTS = 5;
const int SNOW_MAX = 40, DOTS = 9, BEACONS = 6, QMAX = 96;
const int WHALE_L = 95, WHALE_MS = 15000;  // the whale crosses during the first 15 s of each minute
const int PIPE_Y = 166;
const float PIF = 3.14159265f, DEG = PIF / 180;

const uint8_t CORNER[6] = {6, 4, 3, 2, 1, 1};  // glass corner inset per row
const struct { uint8_t x, w, ph; } SHAFT[SHAFTS] = {{24, 14, 0}, {66, 22, 17}, {112, 10, 31}, {146, 26, 44}, {196, 12, 56}};
const struct { uint8_t x, spd, ph, sz; Rgb c; } JEL[JELLIES] = {
    {70, 32, 4, 13, {255, 79, 216}}, {132, 24, 21, 16, {95, 255, 255}}, {186, 38, 40, 10, {167, 139, 255}}};
const struct { uint8_t x, h, ph; } KELP[KELPS] = {{20, 62, 0}, {29, 80, 12}, {40, 50, 23}, {50, 68, 31}, {224, 34, 7}};
const uint8_t DOTX[DOTS] = {12, 64, 71, 78, 104, 143, 200, 208, 216};

struct Area { int16_t x, y, w, h; };

struct Jelly {
  int16_t x, y;       // middle of the bell's rim
  uint8_t h;          // bell height
  uint8_t hw[14];     // half width of each bell row
  int8_t tx[4];       // where the tentacles hang from
  int8_t toff[4][5];  // their sway
  uint8_t ty[5];
  Area shown;
};

// Everything the scene reads, so every repaint agrees with what is on screen. Lives on the
// heap while this face is up.
struct Sea {
  uint8_t farY[240], nearY[240];  // top of the far and near sea floor per column
  int8_t whaleTop[WHALE_L], whaleBot[WHALE_L];
  // light
  int light;  // 0..256
  bool night;
  Rgb top, mid;
  uint16_t cFar, cEdge, cPipe, cHab, cKelp, cFish, cWhale, cRim, cAnem;
  int glowB;   // jellyfish brightness, 0..256
  int snowA;   // marine snow brightness, 0..256
  int shaftBase, shaftA[SHAFTS];
  int8_t shaftSway[SHAFTS];
  uint8_t shimmer, flashX;
  bool flash, rainy, storm;
  // things that move
  Jelly jel[JELLIES];
  int16_t subX, subY, bubX[3], bubY[3];
  int8_t subDir;
  bool subProp, subLamp;
  bool whaleOn;
  int16_t whaleX, whaleY;
  int8_t whaleTail;
  int16_t fishX[FISH], fishY[FISH];
  int16_t ventX[VENTS], ventY[VENTS];
  int8_t kelpX[KELPS][KNODES], kelpLeaf[KELPS][KNODES];
  uint8_t dotA[DOTS];
  int8_t anem[3];
  uint8_t beacon;
  bool mastOn, winOff;
  uint8_t snowN, snowX[SNOW_MAX], snowY[SNOW_MAX], snowTw[SNOW_MAX];
  float snowFX[SNOW_MAX], snowFY[SNOW_MAX], snowS[SNOW_MAX], current;
  Area subShown, whaleShown, fishShown;
  Area q[QMAX];  // areas to repaint this tick
  uint8_t qn;
};
Sea* sea = nullptr;

uint32_t hsh(uint32_t i) {
  i ^= 0x9e3779b9u;
  i *= 2654435761u;
  i ^= i >> 15;
  i *= 2246822519u;
  i ^= i >> 13;
  return i;
}

Rgb lerpRgb(Rgb a, Rgb b, int t) {  // t of 256
  return {(uint8_t)(a.r + (b.r - a.r) * t / 256), (uint8_t)(a.g + (b.g - a.g) * t / 256),
          (uint8_t)(a.b + (b.b - a.b) * t / 256)};
}
uint16_t c565(Rgb c) { return rgb(c.r, c.g, c.b); }
uint16_t lit(Rgb dark, Rgb bright, int t) { return c565(lerpRgb(dark, bright, t)); }

// Everything the scene draws goes through this, into the strip or box sprite, so the same
// code paints the whole scene and repaints small areas. Light and glow are blended into
// the pixels already in the sprite.
struct Canvas {
  TFT_eSprite& g;
  int dx, dy;          // screen point (x, y) maps to (x + dx, y + dy)
  int x0, x1, y0, y1;  // the screen area being painted
  uint16_t* buf;
  bool hits(int x, int y, int w, int h) const { return y < y1 && y + h > y0 && x < x1 && x + w > x0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (hits(x, y, w, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void px(int x, int y, uint16_t c) { rect(x, y, 1, 1, c); }
  void frame(int x, int y, int w, int h, uint16_t c) {
    if (hits(x, y, w, h)) g.drawRect(x + dx, y + dy, w, h, c);
  }
  void rrect(int x, int y, int w, int h, int r, uint16_t c, bool fill) {
    if (!hits(x, y, w, h)) return;
    if (fill) g.fillRoundRect(x + dx, y + dy, w, h, r, c);
    else g.drawRoundRect(x + dx, y + dy, w, h, r, c);
  }
  void line(int xa, int ya, int xb, int yb, uint16_t c) {
    if (hits(min(xa, xb), min(ya, yb), abs(xb - xa) + 1, abs(yb - ya) + 1)) g.drawLine(xa + dx, ya + dy, xb + dx, yb + dy, c);
  }
  void dot(int x, int y, int r, uint16_t c, bool fill = true) {
    if (!hits(x - r, y - r, 2 * r + 1, 2 * r + 1)) return;
    if (fill) g.fillCircle(x + dx, y + dy, r, c);
    else g.drawCircle(x + dx, y + dy, r, c);
  }
  void tri(int xa, int ya, int xb, int yb, int xc, int yc, uint16_t c) {
    int lx = min(xa, min(xb, xc)), hx = max(xa, max(xb, xc)), ly = min(ya, min(yb, yc)), hy = max(ya, max(yb, yc));
    if (hits(lx, ly, hx - lx + 1, hy - ly + 1)) g.fillTriangle(xa + dx, ya + dy, xb + dx, yb + dy, xc + dx, yc + dy, c);
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
  // Round glow fading out from the centre.
  void glow(int cx, int cy, int r, Rgb c, int a) {
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
        tft.fillCircle(x, y, 5, B_DATE);
        tft.fillCircle(x + 3, y - 2, 4, B_BG);
        break;
      }
      tft.fillCircle(x, y, 4, B_SUN);
      for (int i = 0; i < 8; i++) {
        float an = i * PIF / 4;
        tft.drawLine(x + (int)lroundf(cosf(an) * 6), y + (int)lroundf(sinf(an) * 6),
                     x + (int)lroundf(cosf(an) * 8), y + (int)lroundf(sinf(an) * 8), B_SUN);
      }
      break;
    case CNW_CLOUD:
      tft.fillCircle(x - 3, y - 4, 4, night ? B_DATE : B_SUN);
      cloud(B_CLOUD);
      break;
    case CNW_OVERCAST:
      tft.fillCircle(x - 4, y - 4, 3, B_CLOUDDK);
      tft.fillCircle(x, y - 6, 3, B_CLOUDDK);
      cloud(B_CLOUD);
      break;
    case CNW_FOG:
      for (int i = 0; i < 3; i++) tft.fillRect(x - 7 + (i & 1) * 2, y + i * 4 - 4, 14, 1, B_CLOUD);
      break;
    case CNW_RAIN:
      cloud(B_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.drawLine(x + d, y + 6, x + d - 1, y + 9, B_DROP);
      break;
    case CNW_SNOW:
      cloud(B_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.fillRect(x + d, y + 6 + ((d + 3) & 4 ? 1 : 0), 2, 2, TFT_WHITE);
      break;
    case CNW_THUNDER:
      cloud(B_CLOUDDK);
      tft.fillTriangle(x + 2, y + 4, x - 2, y + 8, x + 1, y + 8, B_YELLOW);
      tft.fillTriangle(x + 1, y + 7, x + 3, y + 7, x - 1, y + 11, B_YELLOW);
      break;
  }
}

// ---- scene ----

void drawWater(Canvas& cv) {
  const Rgb bot = {1, 6, 12};
  for (int y = max(cv.y0, Y0); y < min(cv.y1, Y1); y++) {
    int f = (y - Y0) * 256 / (Y1 - Y0);
    Rgb c = f < 115 ? lerpRgb(sea->top, sea->mid, f * 256 / 115) : lerpRgb(sea->mid, bot, (f - 115) * 256 / 141);
    cv.rect(cv.x0, y, cv.x1 - cv.x0, 1, c565(c));
  }
}

// Light from the surface: slanted shafts, a shimmer along the top edge, lightning.
void drawLight(Canvas& cv) {
  for (int k = 0; k < SHAFTS; k++) {
    int a0 = sea->shaftA[k], w = SHAFT[k].w, sw = sea->shaftSway[k];
    if (!a0) continue;
    for (int y = max(cv.y0, Y0); y < min(cv.y1, Y0 + SHAFT_H); y++) {
      int f = y - Y0;
      int xl = SHAFT[k].x + (38 + sw - w * 3 / 10) * f / SHAFT_H;
      int xr = SHAFT[k].x + w + (38 + sw + w * 8 / 10) * f / SHAFT_H;
      cv.add(xl, y, xr - xl, {170, 236, 255}, a0 * (SHAFT_H - f) / SHAFT_H);
    }
  }
  if (cv.hits(X0, Y0, X1 - X0, 1)) {
    int a = 56 * sea->light >> 8;
    for (int x = X0; x < X1; x += 3)
      if ((hsh(x + sea->shimmer * 977u) & 7) < 3) cv.add(x, Y0, 2, {190, 245, 255}, a);
  }
  if (sea->flash) cv.glow(sea->flashX, Y0, 130, {215, 240, 255}, 128);
}

void drawWhale(Canvas& cv) {
  if (!sea->whaleOn) return;
  int x = sea->whaleX, y = sea->whaleY, to = sea->whaleTail;
  if (!cv.hits(x - 1, y - 18, 110, 56)) return;
  uint16_t c = sea->cWhale;
  int stock = to * 6 / 10;
  for (int i = max(0, cv.x0 - x); i < min(WHALE_L, cv.x1 - x); i++) {
    int bend = i > 70 ? stock * (i - 70) / 24 : 0;
    int ty = sea->whaleTop[i] + bend;
    cv.rect(x + i, y + ty, 1, sea->whaleBot[i] + bend - ty + 1, c);
    if (i > 2 && i <= 70) cv.px(x + i, y + ty, sea->cRim);
  }
  cv.tri(x + 70, y - 6, x + 73, y - 9, x + 77, y - 5, c);                              // dorsal fin
  cv.tri(x + 94, y - 2 + stock, x + 106, y - 10 + to, x + 102, y - 1 + to, c);       // flukes
  cv.tri(x + 93, y + 3 + stock, x + 102, y - 1 + to, x + 107, y + 7 + to, c);
  cv.tri(x + 94, y - 2 + stock, x + 93, y + 3 + stock, x + 102, y - 1 + to, c);
  cv.tri(x + 26, y + 9, x + 39, y + 10, x + 50, y + 30 + to / 3, c);                   // flipper
  for (int k = 0; k < 3; k++) cv.line(x + 5 + k * 2, y + 6 + k, x + 28, y + 9 + k, sea->cRim);  // throat grooves
  cv.px(x + 13, y + 1, rgb(150, 190, 200));
}

void drawFish(Canvas& cv) {
  for (int i = 0; i < FISH; i++) {
    int x = sea->fishX[i], y = sea->fishY[i];
    cv.rect(x, y, 5, 1, sea->cFish);  // body, back and a forked tail
    cv.rect(x + 1, y - 1, 2, 1, sea->cFish);
    cv.px(x + 5, y - 1, sea->cFish);
    cv.px(x + 5, y + 1, sea->cFish);
  }
}

// Pipeline on stilts with a chasing row of beacons, then the far ridge it runs behind.
void drawFloorFar(Canvas& cv) {
  if (cv.y1 <= 158) return;
  cv.rect(X0, PIPE_Y, 158, 2, sea->cPipe);
  for (int x = 14; x < 160; x += 22) cv.rect(x, PIPE_Y + 2, 1, 24, sea->cPipe);
  for (int i = 0; i < BEACONS; i++) {
    int x = 20 + i * 26;
    if (i == sea->beacon) {
      for (int y = PIPE_Y - 4; y <= PIPE_Y; y++) cv.add(x - 2, y, 5, {255, 176, 32}, 64);
      cv.rect(x, PIPE_Y - 2, 1, 2, B_AMBER);
    } else {
      cv.rect(x, PIPE_Y - 2, 1, 2, B_BEACONOFF);
    }
  }
  for (int x = max(cv.x0, X0); x < min(cv.x1, X1); x++) cv.rect(x, sea->farY[x], 1, Y1 - sea->farY[x], sea->cFar);
}

void drawHabitat(Canvas& cv) {
  if (!cv.hits(158, 140, 76, 44)) return;
  uint16_t hab = sea->cHab;
  cv.dot(201, 181, 17, hab);  // greenhouse dome, lit from inside
  cv.glow(201, 181, 15, {255, 200, 90}, 107);
  cv.rect(201, 164, 1, 17, B_LATTICE);
  cv.rect(192, 167, 1, 14, B_LATTICE);
  cv.rect(210, 167, 1, 14, B_LATTICE);
  cv.rect(187, 173, 29, 1, B_LATTICE);
  for (int i = 0; i < 4; i++) cv.rect(189 + i * 7 - (i > 1 ? 1 : 0), 178 - (i & 1) * 2, 3, 3 + (i & 1) * 2, B_PLANT);
  cv.dot(201, 181, 17, B_HABEDGE, false);
  cv.rect(183, 181, 37, Y1 - 181, sea->cFar);  // the dome's lower half is under the ridge

  cv.rrect(160, 163, 23, 11, 4, hab, true);  // crew module
  cv.rrect(160, 163, 23, 11, 4, B_HABEDGE, false);
  cv.rect(164, 174, 1, 7, hab);
  cv.rect(178, 174, 1, 7, hab);
  cv.rect(183, 167, 4, 3, hab);
  for (int i = 0; i < 3; i++)
    if (!(i == 1 && sea->winOff)) cv.rect(164 + i * 6, 167, 2, 2, B_GLASS);

  cv.rect(220, 160, 9, 21, hab);  // tower and mast
  cv.frame(220, 160, 9, 21, B_HABEDGE);
  for (int y = 164; y <= 174; y += 5) cv.rect(223, y, 3, 1, B_AMBER);
  cv.rect(224, 144, 1, 16, rgb(44, 127, 144));
  if (sea->mastOn) {
    for (int y = 141; y < 146; y++) cv.add(222, y, 5, {255, 59, 59}, 77);
    cv.rect(224, 143, 1, 2, B_RED);
  }
}

void drawSub(Canvas& cv) {
  int x = sea->subX, y = sea->subY, d = sea->subDir;
  if (!cv.hits(d > 0 ? x - 40 : x - 58, y - 13, 99, 30)) return;
  auto X = [&](int i, int w) { return d > 0 ? x + i : x - i - w + 1; };  // mirrored when heading left
  for (int k = 0; k < 3; k++) cv.px(sea->bubX[k], sea->bubY[k], rgb(105 - k * 22, 132 - k * 28, 140 - k * 30));
  for (int dy = -10; dy <= 14; dy++) {  // headlight cone
    int i0 = dy < 0 ? 12 - dy * 44 / 10 : (dy > 3 ? 12 + (dy - 3) * 44 / 11 : 12);
    for (int i = i0; i < 56; i += 2) cv.add(X(i, 2), y + dy, 2, {255, 240, 190}, 88 * (56 - i) / 44);
  }
  cv.rect(X(-8, 17), y + 6, 17, 1, B_SUBGREY);  // skid
  cv.rect(X(-6, 1), y + 4, 1, 2, B_SUBGREY);
  cv.rect(X(6, 1), y + 4, 1, 2, B_SUBGREY);
  cv.rrect(X(-12, 24), y - 5, 24, 10, 5, B_SUB, true);  // hull and sail
  cv.rect(X(-5, 7), y - 8, 7, 3, B_SUB);
  cv.rect(X(-9, 18), y + 2, 18, 2, B_SUBDK);
  cv.rect(X(-7, 2), y - 2, 2, 2, rgb(16, 38, 46));
  cv.rect(X(-3, 2), y - 2, 2, 2, rgb(16, 38, 46));
  cv.dot(X(7, 1), y - 1, 3, B_GLASS);  // cockpit
  cv.px(X(8, 1), y - 3, TFT_WHITE);
  cv.rect(X(12, 2), y + 1, 2, 2, TFT_WHITE);
  cv.rect(X(-2, 1), y - 11, 1, 3, B_SUB);
  if (sea->subLamp) cv.px(X(-2, 1), y - 12, B_RED);
  if (sea->subProp) cv.rect(X(-14, 1), y - 4, 1, 8, rgb(154, 167, 173));
  else cv.rect(X(-15, 3), y - 1, 3, 2, rgb(154, 167, 173));
}

void drawVent(Canvas& cv) {
  for (int k = 0; k < VENTS; k++) {
    int x = sea->ventX[k], y = sea->ventY[k], a = 64 + (k % 3) * 51;
    if (k & 1) {
      cv.mix(x, y, 3, {190, 240, 255}, a);
      cv.mix(x, y + 1, 1, {190, 240, 255}, a);
      cv.mix(x + 2, y + 1, 1, {190, 240, 255}, a);
      cv.mix(x, y + 2, 3, {190, 240, 255}, a);
    } else {
      cv.mix(x, y, 2, {190, 240, 255}, a);
      cv.mix(x, y + 1, 2, {190, 240, 255}, a);
    }
  }
}

// Near rocks with kelp, glowing polyps and an anemone.
void drawFloorNear(Canvas& cv) {
  if (cv.y1 <= 110) return;
  for (int x = max(cv.x0, X0); x < min(cv.x1, X1); x++) {
    cv.rect(x, sea->nearY[x], 1, Y1 - sea->nearY[x], B_ROCK);
    cv.px(x, sea->nearY[x], sea->cEdge);
  }
  for (int k = 0; k < KELPS; k++) {
    int bx = KELP[k].x, by = sea->nearY[bx], n = KELP[k].h / 6;
    if (!cv.hits(bx - 13, by - n * 6 - 5, 32, n * 6 + 6)) continue;
    int lx = bx, ly = by;
    for (int i = 1; i <= n; i++) {
      int nx = bx + sea->kelpX[k][i], ny = by - i * 6, sd = i & 1 ? 5 : -5, ty = ny - 3 + sea->kelpLeaf[k][i];
      cv.line(lx, ly, nx, ny, sea->cKelp);
      cv.line(lx + 1, ly, nx + 1, ny, sea->cKelp);
      cv.line(nx, ny, nx + sd, ty, sea->cKelp);
      cv.line(nx, ny + 1, nx + sd, ty + 1, sea->cKelp);
      lx = nx;
      ly = ny;
    }
  }
  for (int i = 0; i < DOTS; i++) {
    int x = DOTX[i], y = sea->nearY[x] - 2;
    Rgb c = i & 1 ? Rgb{95, 255, 208} : Rgb{110, 200, 255};
    cv.mix(x, y, 1, c, sea->dotA[i]);
    cv.mix(x, y + 1, 1, c, sea->dotA[i]);
  }
  for (int i = 0; i < 3; i++) {
    int x = 204 + i * 3;
    cv.line(x, sea->nearY[x], x + (i - 1) * 2 + sea->anem[i], sea->nearY[x] - 6, sea->cAnem);
  }
}

void drawJelly(Canvas& cv, int n) {
  const Jelly& j = sea->jel[n];
  Rgb c = JEL[n].c;
  int r = JEL[n].sz * 5 / 4, b = sea->glowB;
  if (!cv.hits(j.x - r, j.y - r, 2 * r + 1, 2 * r + 8)) return;
  cv.glow(j.x, j.y, r, c, 82 * b >> 8);
  uint16_t tc = c565(lerpRgb({0, 0, 0}, c, (141 * b >> 8) + 26));
  for (int k = 0; k < 4; k++) {
    int lx = j.x + j.tx[k], ly = j.y;
    for (int i = 0; i < 5; i++) {
      int nx = j.x + j.tx[k] + j.toff[k][i], ny = j.y + j.ty[i];
      cv.line(lx, ly, nx, ny, tc);
      lx = nx;
      ly = ny;
    }
  }
  int ba = (128 * b >> 8) + 51, ra = 141 * b >> 8;
  for (int d = 1; d <= j.h; d++) {  // translucent bell with a bright rim
    int hw = j.hw[d], y = j.y - d;
    cv.mix(j.x - hw, y, 2 * hw + 1, c, ba);
    if (d == j.h) {
      cv.mix(j.x - hw, y, 2 * hw + 1, {255, 255, 255}, ra);
    } else {
      int e = max(1, hw - j.hw[d + 1]);
      cv.mix(j.x - hw, y, e, {255, 255, 255}, ra);
      cv.mix(j.x + hw - e + 1, y, e, {255, 255, 255}, ra);
    }
  }
}

// Marine snow drifting down; after dark some of it is glowing plankton.
void drawSnow(Canvas& cv) {
  for (int i = 0; i < sea->snowN; i++) {
    int x = X0 + sea->snowX[i], y = Y0 + sea->snowY[i];
    if (sea->night && (i & 3) == 0) cv.add(x, y, 1, {95, 255, 255}, sea->snowTw[i]);
    else cv.add(x, y, 1, {200, 235, 245}, (77 + (i % 3) * 51) * sea->snowA >> 8);
  }
}

// The glass's reflection and the riveted hull around it.
void drawFrame(Canvas& cv) {
  for (int y = max(cv.y0, Y0); y < min(cv.y1, Y0 + 54); y++) {
    if (y < Y0 + 40) cv.add(X0 + 5 + (Y0 + 40 - y) * 38 / 40, y, 3, {205, 245, 255}, 18);
    cv.add(X0 + 5 + (Y0 + 54 - y) * 52 / 54, y, 3, {205, 245, 255}, 18);
  }
  cv.rect(0, SCENE_Y0, 240, Y0 - SCENE_Y0, B_HULL);
  cv.rect(0, Y1, 240, 5, B_HULL);
  cv.rect(0, Y1 + 5, 240, HUD_Y - Y1 - 5, B_BG);
  cv.rect(0, Y0, X0, Y1 - Y0, B_HULL);
  cv.rect(X1, Y0, 240 - X1, Y1 - Y0, B_HULL);
  cv.frame(0, SCENE_Y0, 240, Y1 + 5 - SCENE_Y0, B_HULLEDGE);
  cv.rect(X0 + 6, Y0 - 1, X1 - X0 - 12, 1, B_GLASSEDGE);
  cv.rect(X0 + 6, Y1, X1 - X0 - 12, 1, B_GLASSEDGE);
  cv.rect(X0 - 1, Y0 + 6, 1, Y1 - Y0 - 12, B_GLASSEDGE);
  cv.rect(X1, Y0 + 6, 1, Y1 - Y0 - 12, B_GLASSEDGE);
  for (int k = 0; k < 6; k++) {  // rounded corners
    int in = CORNER[k], a = in - 1, b = k ? max(in - 1, CORNER[k - 1] - 2) : in - 1;
    for (int s = 0; s < 2; s++) {
      int y = s ? Y1 - 1 - k : Y0 + k;
      cv.rect(X0, y, in, 1, B_HULL);
      cv.rect(X1 - in, y, in, 1, B_HULL);
      cv.rect(X0 + a, y, b - a + 1, 1, B_GLASSEDGE);
      cv.rect(X1 - 1 - b, y, b - a + 1, 1, B_GLASSEDGE);
    }
  }
  for (int x = 14; x < 232; x += 30) {
    cv.rect(x, SCENE_Y0 + 2, 2, 2, B_RIVET);
    cv.rect(x, Y1 + 1, 2, 2, B_RIVET);
  }
  for (int y = 70; y < 200; y += 30) {
    cv.rect(3, y, 2, 2, B_RIVET);
    cv.rect(235, y, 2, 2, B_RIVET);
  }
}

void drawAll(Canvas& cv) {
  drawWater(cv);
  drawLight(cv);
  drawWhale(cv);
  drawFish(cv);
  drawFloorFar(cv);
  drawHabitat(cv);
  drawSub(cv);
  drawVent(cv);
  drawFloorNear(cv);
  for (int i = 0; i < JELLIES; i++) drawJelly(cv, i);
  drawSnow(cv);
  drawFrame(cv);
}

// Whale outline: the top and bottom edge per column, from the curves of the mockup.
void curve(int8_t* tab, float xa, float ya, float cx, float cy, float xb, float yb) {
  for (int i = 0; i <= 96; i++) {
    float t = i / 96.0f, u = 1 - t;
    int x = (int)lroundf(u * u * xa + 2 * u * t * cx + t * t * xb);
    if (x >= 0 && x < WHALE_L) tab[x] = (int8_t)lroundf(u * u * ya + 2 * u * t * cy + t * t * yb);
  }
}

void buildShapes() {
  for (int x = 0; x < 240; x++) {
    float f = 176 - 9 * sinf(x / 41.0f + 0.6f) - 4 * sinf(x / 15.0f + 2) - 2 * sinf(x / 6.0f);
    float k = constrain((x - 148) / 12.0f, 0.0f, 1.0f);  // levelled where the habitat stands
    sea->farY[x] = (uint8_t)(f + (181 - f) * k);
    float n = 198 - 4 * sinf(x / 27.0f + 2) - 3 * sinf(x / 11.0f + 1);
    if (x > 58 && x < 82) n -= 6 * sinf(PIF * (x - 58) / 24);
    if (x > 192 && x < 228) n -= 10 * sinf(PIF * (x - 192) / 36);
    sea->nearY[x] = (uint8_t)n;
  }
  curve(sea->whaleTop, 0, 3, 6, -8, 30, -11);
  curve(sea->whaleTop, 30, -11, 52, -12, 70, -6);
  curve(sea->whaleTop, 70, -6, 74, -5, 77, -5);
  curve(sea->whaleTop, 77, -5, 86, -3, 94, -2);
  curve(sea->whaleBot, 0, 3, 14, 13, 44, 12);
  curve(sea->whaleBot, 44, 12, 70, 9, 94, 3);
}

}  // namespace

class Abyss : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band = new TFT_eSprite(&tft);  // allocated while this face is up, so other faces get the RAM back
    box = new TFT_eSprite(&tft);
    band->setColorDepth(16);
    bandOk = band->createSprite(240, STRIP_H) != nullptr;
    box->setColorDepth(16);
    boxOk = box->createSprite(BOX_W, BOX_H) != nullptr;
    sea = (Sea*)calloc(1, sizeof(Sea));
    tft.fillScreen(B_BG);
    drawHud();
    shownCount = -1;
    wxDrawn = false;
    if (!sea) return;

    buildShapes();
    for (int i = 0; i < SNOW_MAX; i++) {
      sea->snowFX[i] = random(0, X1 - X0);
      sea->snowFY[i] = random(0, Y1 - Y0);
      sea->snowS[i] = 2 + random(0, 50) / 10.0f;
    }
    wxKind = CNW_CLOUD;
    dayF = 1;
    setLight();
    stepScene(millis(), true);
    sea->qn = 0;
    for (bool& d : dirty) d = true;
  }

  void end() override {
    band->deleteSprite();
    box->deleteSprite();
    delete band;
    delete box;
    band = box = nullptr;
    bandOk = boxOk = false;
    free(sea);
    sea = nullptr;
  }

  void update(const FaceState& s, const FaceState& prev) override {
    bool minute = strcmp(s.time, prev.time) != 0;
    if (minute || s.second != prev.second) drawTime(s);
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 118, textWidth2);
      tft.fillRect(6, 32, 122, 16, B_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(B_TEAL, B_BG);
      tft.drawString(city, 6, 32, 2);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate();
    bool wxChanged = weatherChanged(s, prev) || s.code != prev.code;
    if (!sea) {
      if (wxChanged) drawWeather(s);
      return;
    }
    bool wasNight = sea->night;
    if (wxChanged) wxKind = s.weatherValid ? cnWeather(s.code) : CNW_CLOUD;
    if (minute) dayF = dayFraction();
    if (wxChanged || minute) setLight();
    if (wxChanged || sea->night != wasNight || !wxDrawn) drawWeather(s);
    wxDrawn = true;
  }

  void tick(uint32_t ms) override {
    if (!sea) return;
    int mim = msIntoMin(ms);
    stepScene(ms, false);
    flush();
    for (int i = 0; i < sea->qn; i++) paintArea(sea->q[i]);
    sea->qn = 0;
    drawCount(mim, ms);
  }

 private:
  TFT_eSprite* band = nullptr;
  TFT_eSprite* box = nullptr;
  bool bandOk = false, boxOk = false, wxDrawn = false;
  bool dirty[STRIPS] = {};
  uint16_t flushed = 0;  // strips repainted this tick
  int shownCount = -1;
  uint8_t wxKind = CNW_CLOUD;
  float dayF = 1;  // 0 night .. 1 day
  uint32_t lastShaft = 0, lastShimmer = 0, lastJelly = 0, lastSub = 0, lastWhale = 0, lastFish = 0;
  uint32_t lastVent = 0, lastKelp = 0, lastSnow = 0, nextFlash = 0, flashEnd = 0;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // How much daylight reaches the surface over your city: 0 at night, 1 by day, in between at dusk and dawn.
  float dayFraction() {
    if (!timeSynced()) return 1;
    time_t now = time(nullptr);
    struct tm u;
    gmtime_r(&now, &u);
    float doy = u.tm_yday + 1, utcH = u.tm_hour + u.tm_min / 60.0f;
    float B = 360 * DEG * (doy - 81) / 365;
    float eot = 9.87f * sinf(2 * B) - 7.53f * cosf(B) - 1.5f * sinf(B);  // equation of time, minutes
    float dec = -23.44f * DEG * cosf(360 * DEG * (doy + 10) / 365);
    float lat = settings.lat * DEG;
    float h = (utcH + settings.lon / 15 + eot / 60 - 12) * 15 * DEG;  // sun's hour angle
    float el = asinf(sinf(lat) * sinf(dec) + cosf(lat) * cosf(dec) * cosf(h)) / DEG;
    return constrain((el + 4) / 10, 0.0f, 1.0f);
  }

  // Water colours, shaft strength and glow from the weather and the time of day.
  void setLight() {
    static const uint8_t DAY[7] = {100, 80, 65, 60, 55, 65, 35};  // percent, by CnWeather
    float ld = DAY[wxKind] / 100.0f;
    float ln = wxKind == CNW_SUNNY ? 0.2f : (wxKind == CNW_CLOUD ? 0.14f : 0.08f);
    float sn = wxKind == CNW_SUNNY ? 0.07f : (wxKind == CNW_CLOUD ? 0.03f : 0);  // moonlight
    float sd = 0.2f * ld * (wxKind == CNW_FOG ? 0.5f : (wxKind == CNW_THUNDER ? 0.3f : 1));
    int L = (int)((ln + (ld - ln) * dayF) * 256);
    sea->light = L;
    sea->night = dayF < 0.3f;
    sea->shaftBase = (int)((sn + (sd - sn) * dayF) * 256);
    sea->glowB = (int)((1 - 0.38f * dayF) * 256);
    sea->snowA = 90 + (166 * L >> 8);
    sea->rainy = wxKind == CNW_RAIN || wxKind == CNW_THUNDER;
    sea->storm = wxKind == CNW_THUNDER;
    sea->snowN = wxKind == CNW_SNOW ? 40 : (sea->storm ? 36 : (wxKind == CNW_RAIN ? 30 : (wxKind == CNW_FOG ? 24 : 20)));
    sea->current = sea->storm ? 9 : 2;
    sea->top = lerpRgb({2, 14, 24}, {18, 104, 134}, L);
    sea->mid = lerpRgb({1, 8, 16}, {6, 50, 74}, L);
    sea->cFar = lit({2, 12, 18}, {5, 42, 56}, L);
    sea->cEdge = lit({3, 18, 24}, {10, 60, 72}, L);
    sea->cPipe = lit({4, 22, 28}, {18, 74, 88}, L);
    sea->cHab = lit({3, 20, 28}, {11, 58, 72}, L);
    sea->cKelp = lit({4, 28, 22}, {16, 96, 66}, L);
    sea->cFish = lit({10, 40, 52}, {74, 165, 186}, L);
    Rgb whale = lerpRgb({1, 7, 12}, {3, 30, 44}, L);
    sea->cWhale = c565(whale);
    sea->cRim = sea->night ? rgb(30, 96, 100) : c565(lerpRgb(whale, {160, 228, 242}, 26 + (64 * L >> 8)));
    sea->cAnem = c565(lerpRgb({0, 0, 0}, {255, 79, 216}, sea->night ? 230 : 154));
    setShafts(millis(), false);
    mark(SCENE_Y0, HUD_Y);
  }

  void setShafts(uint32_t ms, bool flicker) {
    float t = ms / 1000.0f;
    for (int k = 0; k < SHAFTS; k++) {
      float ph = SHAFT[k].ph / 10.0f;
      int a = (int)(sea->shaftBase * (0.6f + 0.4f * sinf(t / 3 + ph)));
      if (flicker) a = a * (140 + random(116)) >> 8;  // rain on the surface
      sea->shaftA[k] = a;
      sea->shaftSway[k] = (int8_t)lroundf(sinf(t / 5 + ph) * 6);
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
    if (sea->qn < QMAX) sea->q[sea->qn++] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    else mark(y, y + h);
  }
  // Something moved: repaint where it was and where it is now.
  void moved(Area& shown, Area now) {
    int xa = min(shown.x, now.x), ya = min(shown.y, now.y);
    int xb = max(shown.x + shown.w, now.x + now.w), yb = max(shown.y + shown.h, now.y + now.h);
    if (shown.w && xb - xa <= now.w + 12 && yb - ya <= now.h + 12) {
      push(xa, ya, xb - xa, yb - ya);
    } else {
      if (shown.w) push(shown.x, shown.y, shown.w, shown.h);
      push(now.x, now.y, now.w, now.h);
    }
    shown = now;
  }

  static bool due(uint32_t& last, uint32_t ms, uint32_t every, bool force) {
    if (!force && ms - last < every) return false;
    last = ms;
    return true;
  }

  // Advance everything that moves and note what needs repainting.
  void stepScene(uint32_t ms, bool force) {
    float t = ms / 1000.0f;
    if (due(lastShaft, ms, 600, force)) {
      bool had = false;
      for (int k = 0; k < SHAFTS; k++) had |= sea->shaftA[k] != 0;
      setShafts(ms, sea->rainy);
      sea->shimmer++;
      if (had || sea->shaftBase) mark(Y0, Y0 + SHAFT_H);
      else mark(Y0, Y0 + 1);
    }
    if (sea->rainy && due(lastShimmer, ms, 200, force)) {
      sea->shimmer++;
      push(X0, Y0, 113, 1);
      push(X0 + 113, Y0, 113, 1);
    }
    if (sea->flash && (int32_t)(ms - flashEnd) >= 0) {
      sea->flash = false;
      nextFlash = ms + 3500 + random(5000);
      mark(Y0, Y0 + 131);
    } else if (!sea->flash && sea->storm && (int32_t)(ms - nextFlash) >= 0) {
      sea->flash = true;
      sea->flashX = 40 + random(160);
      flashEnd = ms + 130;
      mark(Y0, Y0 + 131);
    }
    if (due(lastJelly, ms, 150, force))
      for (int i = 0; i < JELLIES; i++) stepJelly(i, t);
    if (due(lastSub, ms, 150, force)) stepSub(ms, t);
    stepWhale(ms, t, force);
    if (due(lastFish, ms, 100, force)) stepFish(t);
    if (due(lastVent, ms, 100, force)) {
      for (int k = 0; k < VENTS; k++) {
        int x = 118 + (int)lroundf(sinf(t * 2 + k * 2) * 3), y = Y1 - 10 - (int)fmodf(t * 14 + k * 30, 150);
        int xa = min(x, (int)sea->ventX[k]), ya = min(y, (int)sea->ventY[k]);
        push(xa, ya, abs(x - sea->ventX[k]) + 3, abs(y - sea->ventY[k]) + 3);
        sea->ventX[k] = x;
        sea->ventY[k] = y;
      }
      uint8_t b = (ms / 350) % BEACONS;
      if (b != sea->beacon) {
        push(18 + sea->beacon * 26, PIPE_Y - 4, 5, 5);
        push(18 + b * 26, PIPE_Y - 4, 5, 5);
        sea->beacon = b;
      }
      bool m = (ms / 600) & 1, w = (ms / 2300) % 5 == 0;
      if (m != sea->mastOn || w != sea->winOff) {
        sea->mastOn = m;
        sea->winOff = w;
        push(222, 141, 5, 5);
        push(170, 167, 2, 2);
      }
    }
    if (due(lastKelp, ms, 300, force)) stepKelp(t);
    if (due(lastSnow, ms, 200, force)) stepSnow();
  }

  void stepJelly(int n, float t) {
    Jelly& j = sea->jel[n];
    float ph = JEL[n].ph / 10.0f, sz = JEL[n].sz;
    j.x = JEL[n].x + (int)lroundf(sinf(t / 2.6f + ph) * 9);
    j.y = Y1 + 24 - (int)fmodf(t * JEL[n].spd / 10 + ph * 60, 200);
    float p = (sinf(t / 0.65f + ph) + 1) / 2, w = sz * (0.78f + 0.22f * p), h = sz * (0.75f - 0.2f * p);
    j.h = (uint8_t)lroundf(h);
    for (int d = 1; d <= j.h; d++) {
      float r = (d - 0.5f) / h;
      j.hw[d] = (uint8_t)lroundf(w / 2 * sqrtf(max(0.0f, 1 - r * r)));
    }
    for (int k = 0; k < 4; k++) {
      j.tx[k] = (int8_t)lroundf(-w / 2 + 1 + k * (w - 2) / 3);
      for (int i = 0; i < 5; i++) j.toff[k][i] = (int8_t)lroundf(sinf(t / 0.4f + ph + (i + 1) * 0.9f + k) * 1.6f);
    }
    for (int i = 0; i < 5; i++) j.ty[i] = (uint8_t)lroundf((i + 1) * sz * 0.27f * (1 + 0.15f * p));
    int r = JEL[n].sz * 5 / 4;
    moved(j.shown, {(int16_t)(j.x - r), (int16_t)(j.y - r), (int16_t)(2 * r + 1), (int16_t)(r + max(r, j.ty[4] + 2))});
  }

  void stepSub(uint32_t ms, float t) {
    float u = fmodf(t, 50) / 50;
    int d = u < 0.5f ? 1 : -1;
    int x = (int)(u < 0.5f ? 26 + 162 * u * 2 : 188 - 162 * (u - 0.5f) * 2), y = 127 + (int)lroundf(sinf(t / 1.7f) * 3);
    sea->subX = x;
    sea->subY = y;
    sea->subDir = d;
    sea->subProp = (ms / 150) & 1;
    sea->subLamp = (ms / 500) & 1;
    for (int k = 0; k < 3; k++) {
      sea->bubX[k] = x - d * (17 + k * 7 + (int)(ms / 150 % 7));
      sea->bubY[k] = y - k * 2 - (int)lroundf(sinf(t / 0.3f + k) * 1.5f);
    }
    moved(sea->subShown, {(int16_t)(d > 0 ? x - 40 : x - 58), (int16_t)(y - 13), 99, 30});
  }

  void stepWhale(uint32_t ms, float t, bool force) {
    int mim = msIntoMin(ms);
    if (mim >= WHALE_MS) {
      if (sea->whaleOn) {
        sea->whaleOn = false;
        moved(sea->whaleShown, {0, 0, 0, 0});
      }
      return;
    }
    if (!due(lastWhale, ms, 100, force || !sea->whaleOn)) return;
    float p = mim / (float)WHALE_MS;
    sea->whaleOn = true;
    sea->whaleX = (int16_t)(246 - 360 * p);
    sea->whaleY = (int16_t)(103 + lroundf(sinf(p * 7) * 4));
    sea->whaleTail = (int8_t)lroundf(sinf(t / 0.52f) * 5);
    moved(sea->whaleShown, {(int16_t)(sea->whaleX - 1), (int16_t)(sea->whaleY - 18), 110, 56});
  }

  void stepFish(float t) {
    int fx = 262 - (int)fmodf(t * 17, 430), fy = 90 + (int)lroundf(sinf(t / 2.1f) * 6);
    for (int i = 0; i < FISH; i++) {
      sea->fishX[i] = fx + (int)(hsh(i + 50) % 31);
      sea->fishY[i] = fy + (int)(hsh(i + 90) % 19) - 9 + (int)lroundf(sinf(t / 0.4f + i));
    }
    if (fx > -40 && fx < 240) moved(sea->fishShown, {(int16_t)fx, (int16_t)(fy - 12), 38, 25});
  }

  void stepKelp(float t) {
    for (int k = 0; k < KELPS; k++) {
      int n = KELP[k].h / 6;
      float ph = KELP[k].ph / 10.0f;
      for (int i = 1; i <= n; i++) {
        float f = i / (float)n;
        sea->kelpX[k][i] = (int8_t)lroundf(sinf(t / 1.6f + i * 0.45f + ph) * f * 7 + f * 4);
        sea->kelpLeaf[k][i] = (int8_t)lroundf(sinf(t / 0.9f + i + ph));
      }
    }
    for (int i = 0; i < DOTS; i++) {
      float s = sinf(t / 0.9f + i * 1.7f);
      sea->dotA[i] = (uint8_t)((0.35f + 0.65f * s * s) * (sea->night ? 255 : 178));
      push(DOTX[i], sea->nearY[DOTX[i]] - 2, 1, 2);
    }
    for (int i = 0; i < 3; i++) sea->anem[i] = (int8_t)lroundf(sinf(t / 0.7f + i * 3) * 1.2f);
    push(8, 112, 60, 88);
    push(212, 156, 28, 44);
    push(200, 178, 14, 16);
  }

  void stepSnow() {
    for (int i = 0; i < sea->snowN; i++) {
      sea->snowFY[i] += sea->snowS[i] * 0.2f;
      sea->snowFX[i] += sea->current * 0.2f;
      if (sea->snowFY[i] >= Y1 - Y0) sea->snowFY[i] -= Y1 - Y0;
      if (sea->snowFX[i] >= X1 - X0) sea->snowFX[i] -= X1 - X0;
      int x = (int)sea->snowFX[i], y = (int)sea->snowFY[i], ox = sea->snowX[i], oy = sea->snowY[i];
      bool glows = sea->night && (i & 3) == 0;
      if (glows) sea->snowTw[i] = 77 + random(153);
      if (x == ox && y == oy && !glows) continue;
      sea->snowX[i] = x;
      sea->snowY[i] = y;
      if (abs(x - ox) <= 3 && abs(y - oy) <= 3) {
        push(X0 + min(x, ox), Y0 + min(y, oy), abs(x - ox) + 1, abs(y - oy) + 1);
      } else {
        push(X0 + ox, Y0 + oy, 1, 1);
        push(X0 + x, Y0 + y, 1, 1);
      }
    }
  }

  void flush() {
    flushed = 0;
    if (!bandOk) return;
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
    tft.setTextColor(B_AQUA, B_BG);
    tft.setTextPadding(w);
    tft.drawString(s.time, 6, 5, 4);
    tft.setTextColor(B_TEAL, B_BG);
    tft.setTextPadding(tft.textWidth("88", 2));
    tft.drawString(sec, 10 + w, 13, 2);
    tft.setTextPadding(0);
  }

  void drawDate() {
    static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    tft.fillRect(140, 4, 96, 16, B_BG);
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, MONTHS[t.tm_mon]);
    tft.fillRect(212, 6, 23, 12, B_BOX);  // 11 NOV, then the weekday boxed
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(B_ONBOX);
    tft.drawString(DAYS[t.tm_wday], 224, 12, 1);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(B_DATE);
    tft.drawString(buf, 207, 4, 2);
  }

  void drawHud() {
    tft.fillRect(0, HUD_Y, 240, 240 - HUD_Y, B_BG);
    tft.drawFastHLine(0, HUD_Y, 240, B_LINE);
    tft.drawRect(4, HUD_Y + 6, 35, 14, B_AMBER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(B_AMBER);
    tft.drawString("HAB-7", 22, HUD_Y + 13, 1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(B_TEAL);
    tft.drawString("40M  5.0BAR", 45, HUD_Y + 13, 1);
  }

  // Weather at the surface: icon, temperature, label.
  void drawWeather(const FaceState& s) {
    tft.fillRect(130, 21, 106, 28, B_BG);
    tft.setTextDatum(MR_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(B_DIM);
      tft.drawString("SURFACE NO SIGNAL", 235, 44, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    const char* lab = cnWeatherLabel(w);
    tft.setTextColor(B_AMBER);
    tft.drawString(lab, 235, 44, 1);
    tft.setTextColor(B_DIM);
    tft.drawString("SURFACE", 230 - tft.textWidth(lab, 1), 44, 1);
    char buf[8], u[2] = {s.unit, '\0'};
    snprintf(buf, sizeof(buf), "%d", s.temp);
    int tw = tft.textWidth(buf, 2), x = 235 - tft.textWidth(u, 2) - 5 - tw;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(B_AMBER);
    tft.drawString(buf, x, 22, 2);
    tft.drawCircle(x + tw + 2, 25, 1, B_AMBER);
    tft.drawString(u, x + tw + 5, 22, 2);
    weatherIcon(w, x - 15, 28, sea && sea->night);
  }

  // WHALE 00:12 until the next pass, WHALE CONTACT blinking while it crosses.
  void drawCount(int mim, uint32_t ms) {
    bool live = mim < WHALE_MS;
    int key = live ? 1000 + (ms / 400) % 2 : 60 - mim / 1000;
    if (key == shownCount) return;
    shownCount = key;
    char buf[16] = " ";
    if (!live) snprintf(buf, sizeof(buf), "WHALE 00:%02d", key);
    else if (key == 1001) strcpy(buf, "WHALE CONTACT");
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(live ? B_AMBER : B_DIM, B_BG);
    tft.setTextPadding(tft.textWidth("WHALE CONTACT", 1));
    tft.drawString(buf, 236, HUD_Y + 13, 1);
    tft.setTextPadding(0);
  }
};

static Abyss abyss;
Face* faceAbyss() { return &abyss; }
