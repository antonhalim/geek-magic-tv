#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "../config.h"
#include "../display.h"
#include "../weather.h"

// Launch: a coastal launch pad at the real time of day over your city. A rocket lifts off
// at the top of every minute, its booster flies back and lands on its legs, a truck hauls
// the booster away and the tower raises the next rocket. Thunderstorms scrub the launch.

namespace {

constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
struct Rgb { uint8_t r, g, b; };

// ---- HUD palette ----
const uint16_t P_BG = rgb(7, 11, 20), P_TIME = rgb(238, 244, 255), P_AMBER = rgb(255, 176, 64);
const uint16_t P_CITY = rgb(120, 188, 255), P_DIM = rgb(79, 122, 166), P_LINE = rgb(29, 51, 80);
const uint16_t P_ONBOX = rgb(28, 16, 4), P_CYAN = rgb(159, 232, 255), P_RED = rgb(255, 80, 80);
const uint16_t P_SUN = rgb(255, 210, 58), P_CLOUD = rgb(184, 200, 224), P_CLOUDDK = rgb(106, 114, 144);
const uint16_t P_DROP = rgb(95, 180, 255);

// ---- layout ----
const int SCENE_Y0 = 50, HUD_Y = 214, HZ = 146, SEA_Y1 = 158;
const int STRIP_H = 12, STRIPS = (HUD_Y - SCENE_Y0 + STRIP_H - 1) / STRIP_H;
const int BOX_W = 40, BOX_H = 44, QMAX = 128;
const int PX = 66, PY = 194, RXO = 9, LZX = 196, LZY = 200;  // tower pivot, rocket offset, landing zone
const int STARS = 60, TWINKLE = 12, CLOUDS = 13, BILLOWS = 24, TRAIL = 36, PUFFS = BILLOWS + TRAIL, PARTS = 44;
const int GLOW_LAUNCH = 80, GLOW_LAND = 50, GLOW_ENTRY = 36;
const float PIF = 3.14159265f, DEG = PIF / 180, QTR_TURN = PIF / 2;
const struct { uint8_t x, y; } LAMPS[3] = {{30, 202}, {98, 203}, {146, 201}};

struct Area { int16_t x, y, w, h; };
struct Puff { int16_t x, y; uint8_t r, a; Rgb c; };

// Everything the scene reads, so every repaint agrees with what is on screen. Lives on the
// heap while this face is up.
struct Pad {
  // light, of 256
  int d, tw, lit, sa, dim, starA, daySunA, lowSunA;
  bool dawn, night, moon, wet, storm, snow, fog, heavy, flash;
  uint8_t kind, clouds;
  int wind10;
  // colours
  uint16_t sky[HZ - SCENE_Y0], sea[SEA_Y1 - HZ];
  Rgb grey, fogC, cloudTop, cloudUnd, glintC, puffBase, rainC;
  uint16_t scrub, ground, groundLine, road, dune, grass, steel, steelHi, tank, tankBand, hangar, hangarHi, hangarSh;
  uint16_t door, hangWin, padC, padHi, padSh, lz, lzMark, rocketW, rocketWLit, rocketSh, rocketShLit, rocketHl, rocketHlLit;
  uint16_t teSteel, teHi, truck;
  // geometry
  uint8_t scrubH[240], duneH[240], wireY[69];
  // this frame
  uint32_t fms;
  int m;    // ms into the minute
  float s;  // seconds into the minute
  float te, rk, slide;
  bool onTE;
  int ascentY, ascentLen, igLen, igA, launchA, flick;  // flame lengths, strengths of 256
  bool boosterDown, boosterLanded;
  int boosterY, entryA, landLen, landA, truckX, dustAge, bdx;
  float legs, bang;
  int sx, sy;  // sonic boom shake
  bool dotsOn;
  int dotX[3], dotY[3], dotR[3], dotA[3];
  Puff puff[PUFFS];
  uint8_t puffN;
  Area smokeBox;
  // things that move
  float cloudX[CLOUDS];
  int16_t cloudShown[CLOUDS];
  int16_t dropX[PARTS], dropY[PARTS];
  int16_t bolt[7];
  uint8_t boltMast;
  Area teShown, boosterShown, truckShown, dotShown[3], ventShown, steamShown, dustShown;
  Area q[QMAX];  // areas to repaint this tick
  uint8_t qn;
};
Pad* pd = nullptr;

uint32_t hsh(uint32_t i) {
  i ^= 0x9e3779b9u;
  i *= 2654435761u;
  i ^= i >> 15;
  i *= 2246822519u;
  i ^= i >> 13;
  return i;
}
float frh(uint32_t i) { return (hsh(i) & 0xffff) / 65536.0f; }
int imod(int a, int b) {
  int m = a % b;
  return m < 0 ? m + b : m;
}
float sat(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float ease(float u) {
  u = sat(u);
  return u * u * (3 - 2 * u);
}

Rgb lerpRgb(Rgb a, Rgb b, int t) {  // t of 256
  return {(uint8_t)(a.r + (b.r - a.r) * t / 256), (uint8_t)(a.g + (b.g - a.g) * t / 256),
          (uint8_t)(a.b + (b.b - a.b) * t / 256)};
}
uint16_t c565(Rgb c) { return rgb(c.r, c.g, c.b); }
Rgb un565(uint16_t v) { return {(uint8_t)((v >> 8) & 0xF8), (uint8_t)((v >> 3) & 0xFC), (uint8_t)((v << 3) & 0xF8)}; }
const Rgb BLACK = {0, 0, 0}, WHITE = {255, 255, 255};

// Night, day, dusk and dawn colours, mixed for the current light.
Rgb K(Rgb n, Rgb dy, Rgb du, Rgb da) {
  Rgb c = lerpRgb(n, dy, pd->d);
  return lerpRgb(c, pd->dawn ? da : du, pd->tw * 218 >> 8);
}
Rgb K(Rgb n, Rgb dy, Rgb du) { return K(n, dy, du, du); }

// Everything the scene draws goes through this, into the strip or box sprite, so the same
// code paints the whole scene and repaints small areas. Light and smoke are blended into
// the pixels already in the sprite.
struct Canvas {
  TFT_eSprite& g;
  int dx, dy;          // scene point (x, y) maps to sprite (x + dx, y + dy)
  int x0, x1, y0, y1;  // the scene area being painted
  uint16_t* buf;
  bool hits(int x, int y, int w, int h) const { return y < y1 && y + h > y0 && x < x1 && x + w > x0; }
  bool rows(int a, int b) const { return a < y1 && b > y0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (w > 0 && h > 0 && hits(x, y, w, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void px(int x, int y, uint16_t c) { rect(x, y, 1, 1, c); }
  // Bresenham, writing only the pixels inside the area being painted.
  void line(int xa, int ya, int xb, int yb, uint16_t c) {
    if (!hits(min(xa, xb), min(ya, yb), abs(xb - xa) + 1, abs(yb - ya) + 1)) return;
    uint16_t sc = (uint16_t)((c >> 8) | (c << 8));  // sprite pixels are byte-swapped
    int ex = abs(xb - xa), sx = xa < xb ? 1 : -1, ey = -abs(yb - ya), sy = ya < yb ? 1 : -1, err = ex + ey, w = g.width();
    for (;;) {
      if (xa >= x0 && xa < x1 && ya >= y0 && ya < y1) buf[(ya + dy) * w + xa + dx] = sc;
      if (xa == xb && ya == yb) break;
      int e2 = 2 * err;
      if (e2 >= ey) { err += ey; xa += sx; }
      if (e2 <= ex) { err += ex; ya += sy; }
    }
  }
  // Fill a convex quad (corners in order), pixel centres inside.
  void quad(const float* xs, const float* ys, uint16_t c) {
    float xmn = min(min(xs[0], xs[1]), min(xs[2], xs[3])), xmx = max(max(xs[0], xs[1]), max(xs[2], xs[3]));
    float ymn = min(min(ys[0], ys[1]), min(ys[2], ys[3])), ymx = max(max(ys[0], ys[1]), max(ys[2], ys[3]));
    if (!hits((int)floorf(xmn), (int)floorf(ymn), (int)(xmx - xmn) + 2, (int)(ymx - ymn) + 2)) return;
    float slope[4];  // one division per edge, not per row: this chip divides in software
    for (int i = 0; i < 4; i++) {
      float dyv = ys[(i + 1) & 3] - ys[i];
      slope[i] = dyv != 0 ? (xs[(i + 1) & 3] - xs[i]) / dyv : 0;
    }
    int ya = max((int)ceilf(ymn - 0.5f), y0), yb = min((int)floorf(ymx - 0.5f), y1 - 1);
    for (int y = ya; y <= yb; y++) {
      float yc = y + 0.5f, lo = 1e9f, hi = -1e9f;
      for (int i = 0; i < 4; i++) {
        float ay = ys[i], by = ys[(i + 1) & 3];
        if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) {
          float x = xs[i] + (yc - ay) * slope[i];
          lo = min(lo, x);
          hi = max(hi, x);
        }
      }
      if (hi < lo) continue;
      int xl = (int)ceilf(lo - 0.5f), xr = (int)floorf(hi - 0.5f);
      rect(xl, y, xr - xl + 1, 1, c);
    }
  }
  void dot(int x, int y, int r, uint16_t c) {
    if (hits(x - r, y - r, 2 * r + 1, 2 * r + 1)) g.fillCircle(x + dx, y + dy, r, c);
  }
  static void mixPx(uint16_t& p, Rgb c, int a, bool add) {
    uint32_t v = (uint16_t)((p >> 8) | (p << 8));
    int r = (v >> 8) & 0xF8, gg = (v >> 3) & 0xFC, b = (v << 3) & 0xF8;
    if (add) {  // light: brighten, capped at white
      r += c.r * a >> 8; gg += c.g * a >> 8; b += c.b * a >> 8;
      r = r > 251 ? 255 : r + 4; gg = gg > 253 ? 255 : gg + 2; b = b > 251 ? 255 : b + 4;
    } else {  // smoke: move toward c; +4/+2 rounds to the nearest 565 level so thin layers don't drift green
      r += ((c.r - r) * a >> 8) + 4; gg += ((c.g - gg) * a >> 8) + 2; b += ((c.b - b) * a >> 8) + 4;
      if (r > 255) r = 255; if (gg > 255) gg = 255; if (b > 255) b = 255;
    }
    uint16_t q = (uint16_t)(((r & 0xF8) << 8) | ((gg & 0xFC) << 3) | (b >> 3));
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
  void mixRect(int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = max(y, y0); yy < min(y + h, y1); yy++) mix(x, yy, w, c, a);
  }
  void addRect(int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = max(y, y0); yy < min(y + h, y1); yy++) add(x, yy, w, c, a);
  }
  // A soft disc of smoke.
  void disc(int cx, int cy, int r, Rgb c, int a) {
    if (a <= 2 || !hits(cx - r, cy - r, 2 * r + 1, 2 * r + 1)) return;
    for (int y = max(cy - r, y0); y <= min(cy + r, y1 - 1); y++) {
      int dy2 = (y - cy) * (y - cy), hw = (int)sqrtf((float)(r * r - dy2));
      mix(cx - hw, y, 2 * hw + 1, c, a);
    }
  }
  // Round glow fading out softly from the centre. No divisions per pixel: this chip has no divider.
  void glow(int cx, int cy, int r, Rgb c, int a) {
    if (a <= 0 || !hits(cx - r, cy - r, 2 * r, 2 * r)) return;
    int r2 = r * r, inv = (1 << 20) / r2;
    for (int y = max(cy - r + 1, y0); y < min(cy + r, y1); y++) {
      uint16_t* row = buf + (y + dy) * g.width() + dx;
      int rem = r2 - (y - cy) * (y - cy), hw = (int)sqrtf((float)rem);
      int xa = max(cx - hw, x0), xb = min(cx + hw + 1, x1), ex = xa - cx, d = rem - ex * ex;  // d = r2 - distance2
      for (int x = xa; x < xb; x++, d -= 2 * ex + 1, ex++) {
        if (d <= 0) continue;
        int t = d * inv >> 12, v = (a * t >> 8) * t >> 8;
        if (v > 1) mixPx(row[x], c, v, true);
      }
    }
  }
};

// A rotated frame: v across, u up from the pivot, turned clockwise by the angle.
struct Xf {
  float px, py, c, s;
  bool up;  // not turned: rectangles can be filled directly
  Xf(float x, float y, float a) : px(x), py(y), c(cosf(a)), s(sinf(a)), up(fabsf(a) < 0.002f) {}
  float fx(float v, float u) const { return px + v * c + u * s; }
  float fy(float v, float u) const { return py + v * s - u * c; }
  int X(float v, float u) const { return (int)lroundf(fx(v, u)); }
  int Y(float v, float u) const { return (int)lroundf(fy(v, u)); }
};

// Fill local [v0, v1) x [u0, u1).
void xrect(Canvas& cv, const Xf& f, float v0, float u0, float v1, float u1, uint16_t col) {
  if (f.up) {
    int x = (int)lroundf(f.px + v0), y = (int)lroundf(f.py - u1);
    cv.rect(x, y, (int)lroundf(v1 - v0), (int)lroundf(u1 - u0), col);
    return;
  }
  float xs[4] = {f.fx(v0, u0), f.fx(v1, u0), f.fx(v1, u1), f.fx(v0, u1)}, ys[4] = {f.fy(v0, u0), f.fy(v1, u0), f.fy(v1, u1), f.fy(v0, u1)};
  cv.quad(xs, ys, col);
}

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
        tft.fillCircle(x, y, 5, P_TIME);
        tft.fillCircle(x + 3, y - 2, 4, P_BG);
        break;
      }
      tft.fillCircle(x, y, 4, P_SUN);
      for (int i = 0; i < 8; i++) {
        float an = i * PIF / 4;
        tft.drawLine(x + (int)lroundf(cosf(an) * 6), y + (int)lroundf(sinf(an) * 6),
                     x + (int)lroundf(cosf(an) * 8), y + (int)lroundf(sinf(an) * 8), P_SUN);
      }
      break;
    case CNW_CLOUD:
      tft.fillCircle(x - 3, y - 4, 4, night ? P_TIME : P_SUN);
      cloud(P_CLOUD);
      break;
    case CNW_OVERCAST:
      tft.fillCircle(x - 4, y - 4, 3, P_CLOUDDK);
      tft.fillCircle(x, y - 6, 3, P_CLOUDDK);
      cloud(P_CLOUD);
      break;
    case CNW_FOG:
      for (int i = 0; i < 3; i++) tft.fillRect(x - 7 + (i & 1) * 2, y + i * 4 - 4, 14, 1, P_CLOUD);
      break;
    case CNW_RAIN:
      cloud(P_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.drawLine(x + d, y + 6, x + d - 1, y + 9, P_DROP);
      break;
    case CNW_SNOW:
      cloud(P_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.fillRect(x + d, y + 6 + ((d + 3) & 4 ? 1 : 0), 2, 2, TFT_WHITE);
      break;
    case CNW_THUNDER:
      cloud(P_CLOUDDK);
      tft.fillTriangle(x + 2, y + 4, x - 2, y + 8, x + 1, y + 8, P_SUN);
      tft.fillTriangle(x + 1, y + 7, x + 3, y + 7, x - 1, y + 11, P_SUN);
      break;
  }
}

// ---- the sky ----

int cloudY(int i) { return 56 + (int)(hsh(i * 9 + 5) % (pd->heavy ? 48 : 56)); }
int cloudW(int i) { return 30 + (int)(hsh(i * 9 + 1) % 70); }
int cloudH(int i) { return 2 + (int)(hsh(i * 9 + 2) % 3); }
int starX(int i) { return (int)(hsh(i * 5 + 1) % 240); }
int starY(int i) { return SCENE_Y0 + (int)(hsh(i * 5 + 2) % 80); }

void drawSky(Canvas& cv) {
  const Pad& r = *pd;
  if (cv.y0 >= HZ) return;
  for (int y = max(cv.y0, SCENE_Y0); y < min(cv.y1, HZ); y++) cv.rect(cv.x0, y, cv.x1 - cv.x0, 1, r.sky[y - SCENE_Y0]);
  if (r.starA > 12) {
    for (int i = 0; i < STARS; i++) {
      int a = r.starA * (77 + (int)(hsh(i * 5 + 3) % 154)) >> 8;
      if (i < TWINKLE && (r.fms / 400 + i * 3) % 4 == 0) a = a * 2 / 5;
      cv.mix(starX(i), starY(i), 1, {230, 236, 255}, a);
    }
  }
  if (r.moon && cv.hits(116, 50, 40, 40)) {
    cv.glow(136, 70, 18, {220, 230, 255}, 41);
    cv.dot(136, 70, 5, rgb(242, 240, 226));
    cv.dot(139, 68, 4, r.sky[68 - SCENE_Y0]);
  }
  if (r.daySunA > 8 && cv.hits(106, 40, 60, 60)) {
    cv.glow(136, 70, 30, {255, 244, 210}, 115 * r.daySunA >> 8);
    static const uint8_t HW[6] = {5, 5, 5, 4, 3, 2};
    for (int k = -5; k <= 5; k++) cv.mix(136 - HW[abs(k)], 70 + k, 2 * HW[abs(k)] + 1, {255, 252, 236}, r.daySunA);
  }
  if (r.lowSunA > 8 && cv.hits(0, HZ - 72, 100, 76)) {  // the sun on the horizon at dusk and dawn
    cv.glow(26, HZ - 2, 70, {255, 120, 90}, 90 * r.lowSunA >> 8);
    cv.glow(26, HZ - 2, 26, {255, 200, 120}, 102 * r.lowSunA >> 8);
    static const uint8_t HW[8] = {8, 8, 7, 7, 6, 5, 4, 2};
    for (int k = 0; k < 8; k++) cv.mix(26 - HW[k], HZ - k, 2 * HW[k] + 1, r.dawn ? Rgb{255, 236, 200} : Rgb{255, 196, 120}, r.lowSunA);
  }
  if (r.flash) cv.addRect(0, SCENE_Y0, 240, HZ - SCENE_Y0, {190, 200, 255}, 90);
}

void drawClouds(Canvas& cv) {
  const Pad& r = *pd;
  int a = r.kind == CNW_SUNNY ? 205 : 243;
  for (int i = 0; i < r.clouds; i++) {
    int x = r.cloudShown[i], y = cloudY(i), w = cloudW(i), h = cloudH(i);
    if (!cv.hits(x, y - 2, w, h + 4)) continue;
    cv.mix(x + w * 15 / 100, y, w * 60 / 100, r.cloudTop, a);
    cv.mixRect(x, y + 1, w, h, r.cloudTop, a);
    cv.mix(x + 5, y + 1 + h, w - 12, r.cloudUnd, a);
    if (r.kind != CNW_SUNNY) cv.mixRect(x + w * 3 / 10, y - 2, w * 3 / 10, 2, r.cloudTop, a);
  }
}

// The second stage and the booster, high up and far away.
void drawDots(Canvas& cv) {
  const Pad& r = *pd;
  if (!r.dotsOn) return;
  for (int i = 0; i < 3; i++) {
    if (!r.dotA[i]) continue;
    int x = r.dotX[i], y = r.dotY[i], rad = r.dotR[i];
    if (!cv.hits(x - rad, y - rad, 2 * rad + 1, 2 * rad + 1)) continue;
    cv.glow(x, y, rad, i == 2 ? Rgb{255, 255, 255} : Rgb{255, 190, 110}, r.dotA[i]);
    cv.add(x, y, 1, {255, 255, 240}, min(256, r.dotA[i] * 2));
  }
}

// ---- the ground ----

void drawSeaLand(Canvas& cv) {
  const Pad& r = *pd;
  if (cv.y1 <= HZ) return;
  for (int y = max(cv.y0, HZ); y < min(cv.y1, SEA_Y1); y++) cv.rect(cv.x0, y, cv.x1 - cv.x0, 1, r.sea[y - HZ]);
  if (cv.rows(HZ, SEA_Y1)) {
    float t = r.fms % 600000 / 1000.0f;
    for (int i = 0; i < 22; i++) {
      int x = imod((int)(frh(i * 7) * 260 + t * ((i & 1) ? 3 : -3)), 260) - 10, y = HZ + 1 + (int)(frh(i * 7 + 1) * 11);
      cv.mix(x, y, 2 + (int)(frh(i * 7 + 2) * 6), r.glintC, 90);
    }
    if (r.lowSunA > 8)
      for (int y = max(cv.y0, HZ + 1); y < min(cv.y1, SEA_Y1); y++)
        cv.add(22 - (y - HZ) * 6 / 10 + (int)lroundf(sinf(t / 0.3f + y) * 2), y, 8 + (y - HZ) * 12 / 10, {255, 170, 100},
               (90 * r.lowSunA >> 8) * (int)(hsh(y + r.fms / 250) & 255) >> 8);
  }
  if (cv.rows(150, 166))
    for (int x = max(cv.x0, 0); x < min(cv.x1, 240); x++) cv.rect(x, SEA_Y1 - r.scrubH[x], 1, r.scrubH[x] + 8, r.scrub);
  if (cv.rows(166, 204)) {
    cv.rect(cv.x0, 166, cv.x1 - cv.x0, 38, r.ground);
    for (int y = 168; y < 204; y += 5) cv.rect(cv.x0, y, cv.x1 - cv.x0, 1, r.groundLine);
    cv.rect(cv.x0, 201, cv.x1 - cv.x0, 1, r.road);
    cv.rect(104, 198, 2, 3, r.road);
  }
  if (cv.rows(201, HUD_Y)) {
    for (int x = max(cv.x0, 0); x < min(cv.x1, 240); x++) cv.rect(x, HUD_Y - r.duneH[x], 1, r.duneH[x], r.dune);
    for (int i = 0; i < 40; i++) {
      int x = (int)(frh(i * 13) * 240), y = 204 + (int)(frh(i * 13 + 1) * 6), h = 2 + hsh(i * 13 + 2) % 3;
      int sw = (int)lroundf(sinf(r.fms % 600000 / (500.0f * 10 / r.wind10) + i) * 0.6f * r.wind10 / 10);
      cv.rect(x, y - h, 1, h, r.grass);
      cv.rect(x + 1 + sw, y - h - 1, 1, h - 1, r.grass);
    }
  }
}

void drawStructures(Canvas& cv) {
  const Pad& r = *pd;
  if (cv.hits(13, 155, 14, 32)) {  // water tower
    cv.rect(15, 170, 1, 16, r.steel);
    cv.rect(25, 170, 1, 16, r.steel);
    cv.rect(15, 178, 11, 1, r.steel);
    cv.dot(20, 164, 6, r.tank);
    cv.rect(14, 164, 13, 1, r.tankBand);
    cv.rect(20, 157, 1, 2, r.steel);
    if ((r.fms / 700) & 1) cv.mix(20, 156, 1, {255, 40, 40}, 256 - r.d * 2 / 5);
  }
  if (cv.hits(120, 178, 40, 18)) {  // hangar
    cv.rect(120, 178, 40, 18, r.hangar);
    cv.rect(120, 178, 40, 1, r.hangarHi);
    cv.rect(158, 178, 2, 18, r.hangarSh);
    cv.rect(126, 186, 12, 10, r.door);
    for (int x = 142; x < 156; x += 4) cv.rect(x, 182, 2, 2, r.hangWin);
  }
  if (cv.hits(48, 190, 66, 10)) {  // pad deck, flame trench and launch mount
    cv.rect(48, 194, 58, 4, r.padC);
    cv.rect(48, 194, 58, 1, r.padHi);
    cv.rect(48, 198, 58, 2, r.padSh);
    cv.rect(104, 191, 10, 7, rgb(8, 8, 10));
    cv.rect(60, 190, 14, 4, r.steel);
  }
  static const uint8_t MX[2] = {42, 110};  // lightning masts and their wires
  for (int k = 0; k < 2; k++) {
    int mx = MX[k];
    cv.rect(mx, 82, 1, 112, r.steel);
    cv.rect(mx - 1, 80, 3, 2, r.steel);
    if ((r.fms / 600 + k) & 1) cv.px(mx, 79, rgb(255, 40, 40));
  }
  if (cv.rows(80, 92))
    for (int i = 0; i < 69; i++) cv.mix(42 + i, r.wireY[i], 1, un565(r.steel), 179);
  cv.line(42, 81, 22, 196, r.steel);
  cv.line(110, 81, 132, 196, r.steel);
  if (cv.hits(LZX - 26, LZY - 6, 52, 12)) {  // landing zone
    for (int k = -4; k <= 4; k++) {
      int hw = (int)lroundf(24 * sqrtf(1 - k * k / 16.0f));
      cv.rect(LZX - hw, LZY + k, 2 * hw + 1, 1, r.lz);
    }
    for (int k = -2; k <= 2; k++) {
      int hw = (int)lroundf(15 * sqrtf(max(0.0f, 1 - k * k / 6.25f)));
      cv.px(LZX - hw, LZY + k, r.lzMark);
      cv.px(LZX + hw, LZY + k, r.lzMark);
      if (abs(k) == 2) cv.rect(LZX - hw, LZY + k, 2 * hw + 1, 1, r.lzMark);
    }
    cv.rect(LZX - 3, LZY, 7, 1, r.lzMark);
    if (r.d < 154)
      for (int k = 0; k < 10; k++) {
        float a = k * 2 * PIF / 10;
        int x = LZX + (int)lroundf(cosf(a) * 24), y = LZY + (int)lroundf(sinf(a) * 4);
        cv.px(x, y, rgb(255, 190, 80));
        cv.addRect(x - 1, y - 1, 3, 3, {255, 170, 60}, 77);
      }
  }
}

// Floodlights on the rocket after dark.
void drawFloods(Canvas& cv) {
  const Pad& r = *pd;
  if (r.d > 128) return;
  int a = r.launchA ? 0 : (256 - r.d * 2) * 19 >> 8, ty = r.onTE && r.te < 0.1f ? 96 : 110;  // beams vanish in the launch glare
  for (const auto& L : LAMPS) {
    if (cv.hits(min((int)L.x, 70), ty, abs(L.x - 76) + 14, L.y - ty + 3) && a > 0) {
      for (int y = max(cv.y0, ty); y < min(cv.y1, (int)L.y); y++) {
        int f = (y - ty) * 256 / (L.y - ty), xl = 70 + (L.x - 70) * f / 256, xr = 82 + (L.x - 82) * f / 256;
        cv.add(xl, y, xr - xl + 1, {220, 232, 255}, a);
      }
    }
    cv.rect(L.x - 2, L.y, 5, 2, rgb(40, 42, 50));
    cv.rect(L.x - 1, L.y - 1, 3, 1, rgb(255, 255, 230));
  }
}

void drawSmoke(Canvas& cv) {
  const Pad& r = *pd;
  if (!r.puffN || !cv.hits(r.smokeBox.x, r.smokeBox.y, r.smokeBox.w, r.smokeBox.h)) return;
  for (int i = 0; i < r.puffN; i++) {
    const Puff& p = r.puff[i];
    cv.disc(p.x, p.y, p.r, p.c, p.a);
  }
}

// ---- the vehicles ----

void drawTower(Canvas& cv, float ang) {
  const Pad& r = *pd;
  Xf f(PX, PY, -ang);
  if (f.up && !cv.hits(PX - 6, PY - 105, 16, 106)) return;
  xrect(cv, f, -3, 0, -2, 102, r.teHi);
  xrect(cv, f, 3, 0, 4, 102, r.teSteel);
  xrect(cv, f, -5, 102, 6, 104, r.teSteel);
  for (int u = 0; u < 100; u += 8) {
    cv.line(f.X(-3, u), f.Y(-3, u), f.X(3, u + 8), f.Y(3, u + 8), r.teSteel);
    cv.line(f.X(3, u), f.Y(3, u), f.X(-3, u + 8), f.Y(-3, u + 8), r.teSteel);
    xrect(cv, f, -3, u, 4, u + 1, r.teSteel);
  }
  for (int u : {26, 58, 90}) xrect(cv, f, 4, u, 8, u + 2, r.teSteel);
}

// Rocket in its own frame: base at the origin, engines down. full adds the upper stage and
// fairing; soot darkens a booster that has flown; legs folds 0 to deployed 1.
void drawRocket(Canvas& cv, const Xf& f, bool full, bool soot, float legs, bool lit) {
  const Pad& r = *pd;
  uint16_t wh = lit ? r.rocketWLit : r.rocketW, sh = lit ? r.rocketShLit : r.rocketSh, hl = lit ? r.rocketHlLit : r.rocketHl;
  if (soot) {
    wh = c565(lerpRgb(un565(wh), {70, 64, 60}, 90));
    sh = c565(lerpRgb(un565(sh), {50, 46, 44}, 90));
  }
  xrect(cv, f, -3, 0, -1, 2, rgb(70, 70, 80));
  xrect(cv, f, 1, 0, 3, 2, rgb(70, 70, 80));
  xrect(cv, f, -3, 2, 4, 6, rgb(46, 46, 54));
  xrect(cv, f, -3, 6, 4, 58, wh);
  xrect(cv, f, -3, 6, -2, 58, hl);
  xrect(cv, f, 2, 6, 4, 58, sh);
  if (soot && f.up) {  // scorch, heaviest near the engines
    int bx = f.X(-3, 0);
    for (int u = 6; u < 30; u++) {
      int y = f.Y(0, u);
      if (y < cv.y0 || y >= cv.y1) continue;
      cv.mix(bx, y, 7, {50, 44, 42}, 140 * (30 - u) / 24);
      if ((hsh(u * 7) & 255) < 77) cv.px(bx + hsh(u) % 7, y, rgb(40, 36, 34));
    }
  }
  xrect(cv, f, -5, 55, -3, 57, rgb(84, 84, 92));
  xrect(cv, f, 4, 55, 6, 57, rgb(84, 84, 92));
  if (full) {
    xrect(cv, f, -3, 58, 4, 63, rgb(20, 20, 26));
    xrect(cv, f, -3, 63, 4, 90, wh);
    xrect(cv, f, -3, 63, -2, 90, hl);
    xrect(cv, f, 2, 63, 4, 90, sh);
    static const uint8_t OG[10] = {3, 3, 3, 2, 2, 2, 2, 1, 1, 1};  // fairing nose
    for (int k = 0; k < 10; k++) xrect(cv, f, -OG[k], 90 + k, OG[k] + 1, 91 + k, wh);
  } else {
    xrect(cv, f, -3, 58, 4, 61, rgb(20, 20, 26));
  }
  if (legs >= 0) {
    float phi = legs * 2.36f;
    for (int sd = -1; sd <= 1; sd += 2) {
      float tv = sd * (3 + 14 * sinf(phi)), tu = 5 + 14 * cosf(phi);
      cv.line(f.X(sd * 3, 5), f.Y(sd * 3, 5), f.X(tv, tu), f.Y(tv, tu), rgb(34, 34, 40));
      cv.line(f.X(sd * 3 + sd * 0.6f, 5), f.Y(sd * 3 + sd * 0.6f, 5), f.X(tv + sd * 0.6f, tu), f.Y(tv + sd * 0.6f, tu), rgb(34, 34, 40));
      if (legs > 0.9f) xrect(cv, f, tv - 1, tu - 1, tv + 2, tu, rgb(34, 34, 40));
    }
  }
}

// Exhaust below an engine at (x, y), pointing down.
void flame(Canvas& cv, int x, int y, int len, int wid, int a, int glowR) {
  if (a <= 2 || len <= 0) return;
  if (glowR) cv.glow(x, y + len * 35 / 100, glowR, {255, 140, 60}, 115 * a >> 8);
  if (!cv.hits(x - wid * 2, y, wid * 4, len)) return;
  for (int k = 0; k < len; k++) {
    float f = (float)k / len;
    int w = max(1, (int)(wid * (0.7f + 0.9f * f) * (1 - f * 0.4f)));
    cv.add(x - w / 2, y + k, w, lerpRgb({255, 214, 120}, {255, 80, 30}, (int)(f * 256)), (int)((1 - f) * a));
  }
  int core = len * 45 / 100;
  for (int k = 0; k < core; k++) {
    float f = (float)k / core;
    int w = max(1, (int)(wid * 0.55f * (1 - f)));
    cv.add(x - w / 2, y + k, w, {255, 255, 236}, (int)((1 - f) * a));
  }
}

void drawVehicles(Canvas& cv) {
  const Pad& r = *pd;
  drawTower(cv, r.te);
  if (r.onTE) {  // the next rocket, on the tower
    Xf t(PX, PY, -r.rk);  // its base sits RXO across and slide up the tower
    Xf f(t.px + RXO * t.c + r.slide * t.s, t.py + RXO * t.s - r.slide * t.c, -r.rk);
    drawRocket(cv, f, true, false, -1, r.d < 128);
  }
  if (r.ascentY > -200) {  // climbing away
    Xf f(PX + RXO, r.ascentY, 0);
    drawRocket(cv, f, true, false, -1, true);
    flame(cv, PX + RXO, r.ascentY, r.ascentLen, 6, 256, 0);
  }
  if (r.igA > 2) flame(cv, PX + RXO, PY, r.igLen, 5, r.igA, 0);
  if (r.launchA > 2) cv.glow(PX + RXO, (r.ascentY > -200 ? r.ascentY : PY) + 10, GLOW_LAUNCH, {255, 140, 60}, r.launchA);

  if (r.boosterDown) {  // coming home
    Xf f(LZX, r.boosterY, 0);
    drawRocket(cv, f, false, true, r.legs, false);
    flame(cv, LZX, r.boosterY, 14, 7, r.entryA, r.entryA ? GLOW_ENTRY : 0);
    flame(cv, LZX, r.boosterY, r.landLen, 4, r.landA, 0);
    if (r.landA) cv.glow(LZX, r.boosterY + 10, GLOW_LAND, {255, 150, 70}, 115 * (256 - r.d * 3 / 5) >> 8);
  }
  if (r.boosterLanded) {  // on the landing zone, then onto a truck
    if (r.truckX < 250) {
      int tx = r.truckX;
      cv.rect(tx - 30, 195, 52, 3, rgb(40, 40, 46));
      cv.rect(tx + 14, 188, 10, 8, r.truck);
      cv.rect(tx + 15, 189, 4, 3, rgb(40, 60, 80));
      for (int wx : {tx - 26, tx - 10, tx + 8, tx + 18}) cv.rect(wx, 198, 4, 2, rgb(16, 16, 18));
      if (r.s >= 4 && r.s < 14) {
        Xf b(LZX + r.bdx, 194, r.bang);
        cv.line(tx + 16, 188, b.X(0, 56), b.Y(0, 56), r.truck);
      }
    }
    Xf f(LZX + r.bdx, 194, r.bang);
    drawRocket(cv, f, false, true, r.legs, false);
  }
  if (r.dustAge >= 0 && r.dustAge < 8000) {
    float a = r.dustAge / 1000.0f;
    Rgb dc = K({60, 58, 66}, {226, 214, 190}, {200, 170, 170});
    for (int i = 0; i < 8; i++) {
      int x = LZX + (int)lroundf((i - 3.5f) * 4 + (i < 4 ? -1 : 1) * a * (4 + frh(i) * 3)), y = LZY - 1 - (int)(a * 0.8f * frh(i + 3));
      cv.disc(x, y, (int)(2 + a * 1.4f), dc, (int)(179 * sat(1 - a / 8)));
    }
  }
  float t = r.fms % 600000 / 1000.0f;
  if (r.boosterLanded && (r.s >= 50 || r.s < 4))  // steam off the hot booster
    for (int i = 0; i < 4; i++) {
      float a = fmodf(t + i * 0.7f, 2.8f);
      cv.disc(LZX + (int)lroundf((i - 1.5f) * 2 + r.wind10 * a / 5), 150 - (int)(a * 6), (int)(1 + a), {230, 232, 240}, (int)(115 * (1 - a / 2.8f)));
    }
  if (r.onTE && r.te < 0.1f && r.s >= 40 && !r.storm)  // venting before launch
    for (int i = 0; i < 5; i++) {
      float a = fmodf(t + i * 0.5f, 2.5f);
      cv.disc(PX + RXO + 4 + (int)lroundf(r.wind10 * a * 3 / 10 + a * 2), 134 + (int)(a * 1.5f), (int)(1 + a * 0.9f), {236, 240, 248}, (int)(128 * (1 - a / 2.5f)));
    }
}

void drawStorm(Canvas& cv) {
  const Pad& r = *pd;
  if (!r.flash) return;
  int mx = r.boltMast ? 110 : 42;
  for (int i = 0; i < 6; i++) {
    int ya = SCENE_Y0 + i * 28 / 6, yb = SCENE_Y0 + (i + 1) * 28 / 6, xa = r.bolt[i], xb = i == 5 ? mx : r.bolt[i + 1];
    cv.line(xa + 1, ya, xb + 1, yb, rgb(170, 180, 255));
    cv.line(xa - 1, ya, xb - 1, yb, rgb(170, 180, 255));
    cv.line(xa, ya, xb, yb, TFT_WHITE);
  }
  cv.glow(mx, 78, 24, {200, 210, 255}, 154);
}

void drawPrecip(Canvas& cv) {
  const Pad& r = *pd;
  if (r.wet) {
    int sl = r.storm ? -4 : -2;
    for (int i = 0; i < PARTS; i++) {
      int x = r.dropX[i], y = r.dropY[i];
      if (y < -50 || !cv.hits(min(x, x + sl), y, abs(sl) + 1, 8)) continue;
      for (int k = 0; k < 8; k++) cv.mix(x + sl * k / 7, y + k, 1, r.rainC, 97);
    }
  } else if (r.snow) {
    for (int i = 0; i < PARTS; i++) {
      int x = r.dropX[i], y = r.dropY[i];
      if (cv.hits(x, y, 2, 1)) cv.mix(x, y, (hsh(i) & 255) < 77 ? 2 : 1, {250, 250, 255}, 217);
    }
  }
  if (r.fog) {
    cv.mixRect(cv.x0, max(cv.y0, SCENE_Y0), cv.x1 - cv.x0, min(cv.y1, HUD_Y) - max(cv.y0, SCENE_Y0), r.fogC, 82);
    cv.mixRect(cv.x0, max(cv.y0, SCENE_Y0), cv.x1 - cv.x0, min(cv.y1, 110) - max(cv.y0, SCENE_Y0), r.fogC, 77);
  }
}

void drawAll(Canvas& cv) {
  drawSky(cv);
  drawClouds(cv);
  drawDots(cv);
  drawSeaLand(cv);
  drawStructures(cv);
  drawFloods(cv);
  drawSmoke(cv);
  drawVehicles(cv);
  drawStorm(cv);
  drawPrecip(cv);
}

// Union of two areas.
Area join(const Area& a, const Area& b) {
  if (!a.w) return b;
  if (!b.w) return a;
  int xa = min(a.x, b.x), ya = min(a.y, b.y), xb = max(a.x + a.w, b.x + b.w), yb = max(a.y + a.h, b.y + b.h);
  return {(int16_t)xa, (int16_t)ya, (int16_t)(xb - xa), (int16_t)(yb - ya)};
}

}  // namespace

class LaunchPad : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band = new TFT_eSprite(&tft);  // allocated while this face is up, so other faces get the RAM back
    box = new TFT_eSprite(&tft);
    band->setColorDepth(16);
    bandOk = band->createSprite(240, STRIP_H) != nullptr;
    box->setColorDepth(16);
    boxOk = box->createSprite(BOX_W, BOX_H) != nullptr;
    pd = (Pad*)calloc(1, sizeof(Pad));
    tft.fillScreen(P_BG);
    drawHud();
    shownFoot = -1;
    wxDrawn = false;
    if (!pd) return;

    Pad& r = *pd;
    for (int x = 0; x < 240; x++) {
      r.scrubH[x] = 2 + hsh(x >> 1) % 4 + hsh((x >> 3) + 50) % 3;
      r.duneH[x] = (uint8_t)(6 + lroundf(3 * sinf(x / 23.0f) + 2 * sinf(x / 9.0f + 1)));
    }
    for (int i = 0; i < 69; i++) {  // the wire between the masts sags 7 px in the middle
      float u = i / 68.0f;
      r.wireY[i] = (uint8_t)lroundf(81 + 4 * 7 * u * (1 - u) * 0.5f * 2);
    }
    for (int i = 0; i < CLOUDS; i++) r.cloudX[i] = hsh(i * 9 + 3) % 400;
    r.kind = CNW_CLOUD;
    r.d = 256;
    r.ascentY = -1000;
    r.dustAge = -1;
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
    free(pd);
    pd = nullptr;
  }

  void update(const FaceState& s, const FaceState& prev) override {
    bool minute = strcmp(s.time, prev.time) != 0;
    if (minute || s.second != prev.second) drawTime(s);
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 118, textWidth2);
      tft.fillRect(6, 32, 122, 16, P_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(P_CITY, P_BG);
      tft.drawString(city, 6, 32, 2);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate();
    bool wxChanged = weatherChanged(s, prev) || s.code != prev.code;
    if (!pd) {
      if (wxChanged) drawWeather(s);
      return;
    }
    bool wasNight = pd->night;
    if (wxChanged) pd->kind = s.weatherValid ? cnWeather(s.code) : CNW_CLOUD;
    if (minute) sunLight();
    if (wxChanged || minute) setLight(wxChanged);
    if (wxChanged || pd->night != wasNight || !wxDrawn) drawWeather(s);
    wxDrawn = true;
  }

  void tick(uint32_t ms) override {
    if (!pd || !bandOk) return;
    int mim = msIntoMin(ms);
    stepScene(ms, mim, false);
    flush();
    for (int i = 0; i < pd->qn; i++) paintArea(pd->q[i]);
    pd->qn = 0;
    drawFoot(mim, ms);
  }

 private:
  TFT_eSprite* band = nullptr;
  TFT_eSprite* box = nullptr;
  bool bandOk = false, boxOk = false, wxDrawn = false;
  bool dirty[STRIPS] = {};
  uint16_t flushed = 0;  // strips repainted this tick
  int shownFoot = -1, lightSig = -1, rollNext = 0, lastShakeKey = 0, lastTeSig = -1, lastBSig = -1;
  bool lastBeams = false;
  Area shownSmoke = {0, 0, 0, 0};
  uint32_t lastMs = 0, lastSea = 0, lastGrass = 0, lastSky = 0, lastStar = 0, lastMove = 0, lastPuffs = 0;
  uint32_t lastPrecip = 0, lastBlink = 0, nextFlash = 0, flashEnd = 0;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // Where the sun is over your city: daylight, how close it is to the horizon, and which side of noon.
  void sunLight() {
    Pad& r = *pd;
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

  // Every colour in the scene from the weather and the light; repaints all when they change.
  void setLight(bool force) {
    Pad& r = *pd;
    int k = r.kind;
    r.lit = 256 - r.d;
    r.storm = k == CNW_THUNDER;
    r.wet = k == CNW_RAIN || r.storm;
    r.snow = k == CNW_SNOW;
    r.fog = k == CNW_FOG;
    r.heavy = r.wet || r.snow || k == CNW_OVERCAST;
    r.dim = k == CNW_SUNNY ? 0 : k == CNW_CLOUD ? 90 : k == CNW_OVERCAST ? 128 : k == CNW_FOG ? 115 : 141;
    r.sa = k == CNW_SUNNY ? 256 : k == CNW_CLOUD ? 102 : 0;
    r.wind10 = r.storm ? 40 : k == CNW_RAIN ? 22 : (k == CNW_SNOW || k == CNW_OVERCAST) ? 15 : k == CNW_CLOUD ? 13 : 10;
    int sig = r.d * 7 + r.tw * 3 + (r.dawn ? 1 : 0) + k * 10000;
    if (!force && sig == lightSig) return;
    lightSig = sig;
    int d = r.d, tw = r.tw;
    r.grey = lerpRgb(lerpRgb({12, 14, 24}, r.storm ? Rgb{80, 84, 100} : Rgb{150, 156, 168}, d), {90, 70, 100}, tw / 2);
    r.fogC = lerpRgb(lerpRgb({50, 54, 70}, {206, 210, 218}, d), {180, 150, 170}, tw / 2);
    for (int y = 0; y < HZ - SCENE_Y0; y++) {
      int f = y * 256 / (HZ - SCENE_Y0);
      Rgb nt = lerpRgb({3, 6, 20}, {22, 30, 62}, f), dy = lerpRgb({56, 128, 214}, {186, 220, 246}, f);
      Rgb du = f < 128 ? lerpRgb({26, 24, 78}, {172, 78, 128}, f * 2) : lerpRgb({172, 78, 128}, {255, 172, 96}, (f - 128) * 2);
      Rgb da = f < 128 ? lerpRgb({40, 56, 120}, {200, 140, 172}, f * 2) : lerpRgb({200, 140, 172}, {255, 214, 160}, (f - 128) * 2);
      r.sky[y] = c565(lerpRgb(K(nt, dy, du, da), r.grey, r.dim));
    }
    r.starA = ((256 - d) * (256 - (tw * 218 >> 8)) >> 8) * (k == CNW_SUNNY ? 256 : k == CNW_CLOUD ? 90 : 0) >> 8;
    r.moon = k == CNW_SUNNY && d < 64 && tw < 64;
    r.daySunA = (d * max(0, 256 - tw * 2) >> 8) * r.sa >> 8;
    r.lowSunA = tw * r.sa >> 8;
    r.clouds = k == CNW_SUNNY ? 4 : k == CNW_CLOUD ? 10 : k == CNW_FOG ? 0 : k == CNW_SNOW ? 9 : 13;
    r.cloudTop = K({26, 30, 52}, {250, 250, 255}, {255, 160, 170}, {255, 206, 196});
    r.cloudUnd = K({34, 36, 60}, {214, 222, 236}, {255, 196, 120}, {255, 232, 196});
    if (r.heavy) {
      r.cloudTop = lerpRgb(r.cloudTop, r.grey, 154);
      r.cloudUnd = lerpRgb(r.cloudUnd, r.grey, 154);
    }
    Rgb st = K({8, 14, 32}, {44, 116, 170}, {96, 62, 112}, {150, 130, 170}), sb = K({4, 8, 20}, {30, 92, 150}, {60, 40, 86}, {110, 96, 140});
    for (int y = 0; y < SEA_Y1 - HZ; y++) r.sea[y] = c565(lerpRgb(st, sb, y * 256 / (SEA_Y1 - HZ)));
    r.glintC = K({60, 70, 110}, {200, 236, 250}, {255, 180, 150}, {255, 220, 200});
    r.scrub = c565(K({8, 12, 14}, {84, 110, 76}, {54, 36, 58}, {110, 110, 80}));
    Rgb gnd = K({16, 18, 26}, {198, 190, 168}, {118, 94, 104}, {220, 190, 160});
    r.ground = c565(gnd);
    r.groundLine = c565(lerpRgb(gnd, BLACK, 13));
    r.road = c565(lerpRgb(gnd, K({60, 60, 70}, {236, 232, 222}, {170, 140, 150}), 154));
    r.dune = c565(K({10, 10, 16}, {176, 160, 124}, {90, 64, 80}, {200, 160, 130}));
    if (r.snow) r.dune = c565(lerpRgb(K({10, 10, 16}, {176, 160, 124}, {90, 64, 80}, {200, 160, 130}), {232, 234, 244}, 102));
    r.grass = c565(K({6, 10, 8}, {90, 120, 60}, {48, 36, 52}, {110, 120, 70}));
    Rgb steel = K({28, 30, 40}, {108, 114, 126}, {66, 56, 78});
    r.steel = c565(steel);
    r.steelHi = c565(lerpRgb(steel, WHITE, 46));
    Rgb tank = K({40, 42, 54}, {220, 222, 226}, {150, 130, 150});
    r.tank = c565(tank);
    r.tankBand = c565(lerpRgb(tank, BLACK, 38));
    Rgb hg = K({30, 32, 42}, {200, 200, 206}, {120, 104, 124}, {220, 200, 190});
    r.hangar = c565(hg);
    r.hangarHi = c565(lerpRgb(hg, WHITE, 51));
    r.hangarSh = c565(lerpRgb(hg, BLACK, 51));
    Rgb door = K({10, 10, 14}, {90, 94, 104}, {60, 50, 70});
    r.door = c565(d < 128 ? lerpRgb(door, {255, 214, 150}, 204 * (256 - d * 2) >> 8) : door);
    r.hangWin = d < 128 ? rgb(255, 220, 160) : c565(lerpRgb(hg, {40, 60, 80}, 128));
    Rgb pad = K({40, 42, 52}, {214, 212, 206}, {150, 128, 140}, {226, 206, 190});
    r.padC = c565(pad);
    r.padHi = c565(lerpRgb(pad, WHITE, 51));
    r.padSh = c565(lerpRgb(pad, BLACK, 64));
    r.lz = c565(K({44, 46, 56}, {226, 224, 218}, {160, 138, 150}, {236, 214, 198}));
    r.lzMark = c565(K({150, 130, 40}, {250, 220, 60}, {230, 180, 80}));
    Rgb w0 = K({96, 100, 118}, {246, 246, 250}, {226, 200, 214}, {250, 226, 212});
    Rgb w1 = K({200, 206, 224}, {246, 246, 250}, {226, 200, 214}, {250, 226, 212});
    r.rocketW = c565(w0);
    r.rocketWLit = c565(w1);
    r.rocketSh = c565(lerpRgb(w0, BLACK, 51));
    r.rocketShLit = c565(lerpRgb(w1, BLACK, 51));
    r.rocketHl = c565(lerpRgb(w0, WHITE, 77));
    r.rocketHlLit = c565(lerpRgb(w1, WHITE, 77));
    Rgb te = K({30, 32, 44}, {96, 102, 116}, {64, 54, 76});
    r.teSteel = c565(te);
    r.teHi = c565(lerpRgb(te, WHITE, 46));
    r.truck = c565(K({60, 50, 24}, {244, 196, 40}, {200, 150, 70}));
    r.puffBase = K({54, 56, 66}, {240, 240, 242}, {214, 186, 198}, {236, 220, 212});
    r.rainC = lerpRgb({150, 156, 200}, {226, 234, 250}, d);
    mark(SCENE_Y0, HUD_Y);
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
    if (pd->qn < QMAX) pd->q[pd->qn++] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    else mark(y, y + h);
  }
  void push(const Area& a) { push(a.x, a.y, a.w, a.h); }
  // Something moved: repaint where it was and where it is now.
  void moved(Area& shown, Area now) {
    Area u = join(shown, now);
    if (shown.w && now.w && u.w * u.h <= (shown.w * shown.h + now.w * now.h) * 3 / 2) {
      push(u);
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

  // Bounding box of a rocket or tower drawn in a rotated frame.
  static Area xfBox(const Xf& f, float v0, float u0, float v1, float u1, int pad) {
    int xs[4] = {f.X(v0, u0), f.X(v1, u0), f.X(v0, u1), f.X(v1, u1)}, ys[4] = {f.Y(v0, u0), f.Y(v1, u0), f.Y(v0, u1), f.Y(v1, u1)};
    int xa = xs[0], xb = xs[0], ya = ys[0], yb = ys[0];
    for (int i = 1; i < 4; i++) {
      xa = min(xa, xs[i]); xb = max(xb, xs[i]); ya = min(ya, ys[i]); yb = max(yb, ys[i]);
    }
    return {(int16_t)(xa - pad), (int16_t)(ya - pad), (int16_t)(xb - xa + 2 * pad + 1), (int16_t)(yb - ya + 2 * pad + 1)};
  }

  // Where everything is at s seconds into the minute.
  void place(float s, uint32_t ms) {
    Pad& r = *pd;
    r.s = s;
    bool storm = r.storm;
    r.te = r.rk = r.slide = 0;
    r.onTE = true;
    if (!storm) {
      if (s < 14) { r.te = 0.07f; r.onTE = false; }
      else if (s < 18) { r.te = 0.07f + (QTR_TURN - 0.07f) * ease((s - 14) / 4); r.onTE = false; }
      else if (s < 22) { r.te = r.rk = QTR_TURN; r.slide = 110 * (1 - ease((s - 18) / 4)); }
      else if (s < 30) { r.te = r.rk = QTR_TURN * (1 - ease((s - 22) / 8)); }
      else if (s >= 56) { r.te = 0.07f * ease(s - 56); }
    }
    r.flick = 225 + (int)(hsh(ms / 40) % 62);
    r.ascentY = -1000;
    r.igA = r.launchA = 0;
    if (!storm && s < 13) {
      r.ascentY = (int)lroundf(PY - 1.7f * s * s);
      r.ascentLen = min(46, 10 + (PY - r.ascentY) / 4) * r.flick >> 8;
      r.launchA = (int)(141 * (256 - r.d * 3 / 5) / 256 * (0.85f + 0.3f * (hsh(ms / 50) & 255) / 256));
    }
    if (!storm && s >= 57) {
      float a = sat((s - 57) / 3);
      r.igLen = (int)(6 + (s - 57) * 2);
      r.igA = (int)(a * 205);
      r.launchA = (int)(a * 0.7f * 141 * (256 - r.d * 3 / 5) / 256);
    }
    r.boosterDown = !storm && s >= 31 && s < 50;
    r.entryA = r.landA = r.landLen = 0;
    if (r.boosterDown) {
      float u = (s - 31) / 19;
      r.boosterY = (int)lroundf(LZY - 6 - 214 * (1 - u) * (1 - u));
      r.legs = sat((s - 46) / 1.8f);
      if (s >= 34.5f && s < 37) r.entryA = (int)(256 * sat((37 - s) / 0.6f) * sat((s - 34.5f) / 0.3f));
      if (s >= 44) {
        r.landLen = max(4, (int)(18 - (s - 44) * 2.4f)) * r.flick >> 8;
        r.landA = s > 49.4f ? (int)(256 * sat((50 - s) / 0.6f)) : 256;
      }
    }
    r.boosterLanded = !storm && (s >= 50 || s < 19);
    r.bang = 0;
    r.bdx = 0;
    r.truckX = 250;
    if (r.boosterLanded) {
      r.legs = 1;
      if (s >= 6 && s < 8) r.legs = 1 - (s - 6) / 2;
      else if (s >= 8 && s < 19) r.legs = 0;
      if (s >= 8 && s < 14) r.bang = QTR_TURN * ease((s - 8) / 6);
      else if (s >= 14 && s < 19) r.bang = QTR_TURN;
      if (s >= 14 && s < 19) r.bdx = (int)lroundf(70 * ease((s - 14) / 5));
      if (s >= 4 && s < 8) r.truckX = (int)lroundf(250 - 34 * ease((s - 4) / 4));
      else if (s >= 8 && s < 19) r.truckX = 216 + r.bdx;
    }
    r.dustAge = !storm && s >= 50 && s < 58 ? (int)((s - 50) * 1000) : -1;
    r.dotsOn = !storm && s >= 12 && s < 31;
    memset(r.dotA, 0, sizeof(r.dotA));
    if (r.dotsOn) {
      if (s < 18) {
        float u = (s - 12) / 6;
        r.dotX[0] = (int)(75 + 75 * u); r.dotY[0] = (int)(56 - 2 * u); r.dotR[0] = 5; r.dotA[0] = 179;
      } else {
        float u = (s - 18) / 12, v = (s - 18) / 13;
        r.dotX[0] = (int)(150 + 80 * u); r.dotY[0] = (int)(54 - 2 * u); r.dotR[0] = 3; r.dotA[0] = (int)(154 * (1 - u * 0.5f));
        bool burn = s >= 22 && s < 28;
        r.dotX[1] = (int)(150 + 46 * ease(v)); r.dotY[1] = (int)(54 - 4 * v); r.dotR[1] = burn ? 6 : 2; r.dotA[1] = burn ? 205 : 102;
        if (s < 19) { r.dotX[2] = 150; r.dotY[2] = 54; r.dotR[2] = 7; r.dotA[2] = (int)(154 * (19 - s)); }
      }
    }
    r.sx = r.sy = 0;
    if (!storm && s >= 47 && s < 47.45f) {
      r.sx = (int)lroundf(sinf(ms / 23.0f) * 1.4f);
      r.sy = (int)lroundf(cosf(ms / 29.0f));
    }
  }

  // Smoke from the flame trench and the exhaust trail, as soft discs.
  void stepPuffs() {
    Pad& r = *pd;
    r.puffN = 0;
    r.smokeBox = {0, 0, 0, 0};
    if (r.storm) return;
    float s = r.s, sL = s >= 57 ? s - 60 : s, wind = r.wind10 / 10.0f;
    bool lit = r.launchA > 0;
    float fx = PX + RXO, fy = r.ascentY > -200 ? r.ascentY : PY, fi = r.launchA / 141.0f;
    auto tint = [&](int x, int y) {
      Rgb c = r.puffBase;
      if (lit) {
        float d = sqrtf((x - fx) * (x - fx) + (y - fy) * (y - fy));
        c = lerpRgb(c, {255, 150, 70}, (int)(sat(1 - d / 110) * 0.8f * fi * 256));
      }
      if (r.tw > 51 && y < 100) c = lerpRgb(c, r.dawn ? Rgb{200, 214, 255} : Rgb{150, 186, 255}, (int)(r.tw * 0.6f * sat((100 - y) / 40.0f)));
      return c;
    };
    auto addPuff = [&](float x, float y, float rad, float a) {
      if (a <= 0.02f || r.puffN >= PUFFS) return;
      Puff& p = r.puff[r.puffN++];
      p.x = (int16_t)lroundf(x);
      p.y = (int16_t)lroundf(y);
      p.r = (uint8_t)max(1, (int)lroundf(rad));
      p.a = (uint8_t)min(255, (int)(a * 256));
      p.c = tint(p.x, p.y);
      r.smokeBox = join(r.smokeBox, {(int16_t)(p.x - p.r), (int16_t)(p.y - p.r), (int16_t)(2 * p.r + 1), (int16_t)(2 * p.r + 1)});
    };
    for (int i = 0; i < BILLOWS; i++) {
      float te = -3 + i * 0.3f;
      if (te > sL) break;
      float a = sL - te;
      int side = i % 3 == 0 ? -1 : 1;
      float k1 = a / (a + 2.4f), k2 = a / (a + 3), k3 = a / (a + 3.5f);
      float x = (side > 0 ? 110 : 56) + side * (14 + frh(i) * 44) * k1 + wind * a * 0.8f + (frh(i + 21) - 0.5f) * 8;
      float y = 196 - (2 + frh(i + 9) * 22) * k2 - a * 0.15f * frh(i + 4);
      addPuff(x, y, 2 + (6 + frh(i + 3) * 10) * k3, 0.85f * sat(1 - a / 46));
    }
    for (int i = 0; i < TRAIL; i++) {
      float te = i * 0.28f;
      if (te > sL || te > 12.5f) break;
      float a = sL - te, by = PY - 1.7f * te * te + 2;
      if (by < 30) break;
      float x = PX + RXO + (frh(i) - 0.5f) * 2 + wind * a * 1.1f + (by > 186 ? (frh(i + 5) - 0.5f) * a * 3 : 0);
      addPuff(x, by - a * 0.18f, 2.5f + min(a, 30.0f) * 0.34f, 0.75f * sat(1 - a / 42) * (by < 60 ? 0.7f : 1));
    }
  }

  // Advance everything that moves and note what needs repainting.
  void stepScene(uint32_t ms, int mim, bool force) {
    Pad& r = *pd;
    r.fms = ms;
    r.m = mim;
    float s = mim / 1000.0f, t = ms % 600000 / 1000.0f;
    uint32_t el;
    place(s, ms);

    bool launch = !r.storm && (s >= 56.8f || s < 13.6f);
    bool shake = r.sx || r.sy;
    int shakeKey = shake ? r.sx * 3 + r.sy + 10 : 0;
    if (launch || shake || shakeKey != lastShakeKey) mark(SCENE_Y0, HUD_Y);  // everything moves: repaint it all
    lastShakeKey = shakeKey;

    if (r.flash && (int32_t)(ms - flashEnd) >= 0) {  // lightning strikes a mast
      r.flash = false;
      nextFlash = ms + 2500 + random(4500);
      mark(SCENE_Y0, HZ);
    } else if (!r.flash && r.storm && (int32_t)(ms - nextFlash) >= 0) {
      r.flash = true;
      flashEnd = ms + 130;
      r.boltMast = random(2);
      r.bolt[0] = (r.boltMast ? 110 : 42) + random(-20, 21);
      for (int i = 1; i < 7; i++) r.bolt[i] = r.bolt[i - 1] + random(-5, 6);
      mark(SCENE_Y0, HZ);
    }

    if (due(lastPuffs, ms, 50, force)) {
      stepPuffs();
      if (r.puffN) shownSmoke = join(shownSmoke, r.smokeBox);
      else if (shownSmoke.w) {  // the last of it has faded: clear whatever is left
        mark(shownSmoke.y, shownSmoke.y + shownSmoke.h);
        shownSmoke = {0, 0, 0, 0};
      }
    }
    // Smoke drifts slowly once the rocket is gone: refresh the strips it covers a few at a time.
    if (!launch && r.puffN) {
      int n = 1;
      for (int k = 0; k < STRIPS && n > 0; k++) {
        int i = (rollNext + k) % STRIPS, sy = SCENE_Y0 + i * STRIP_H;
        if (sy < r.smokeBox.y + r.smokeBox.h && sy + STRIP_H > r.smokeBox.y) {
          dirty[i] = true;
          n--;
          rollNext = (i + 1) % STRIPS;
        }
      }
    }

    if (due(lastMove, ms, 100, force)) {  // tower, boosters and truck
      int teSig = (int)(r.te * 1000) * 3 + (int)(r.rk * 1000) * 7 + (int)(r.slide * 10) * 11 + r.onTE;
      if (teSig != lastTeSig || force) {  // only when the tower or its rocket moved
        lastTeSig = teSig;
        Xf tf(PX, PY, -r.te);
        Area a = xfBox(tf, -6, 0, 9, 105, 2);
        if (r.onTE) {
          Xf rf(PX, PY, -r.rk);
          a = join(a, xfBox(rf, RXO - 6, r.slide, RXO + 7, r.slide + 101, 2));
        }
        moved(r.teShown, a);
      }
      Area b = {0, 0, 0, 0};
      if (r.boosterDown) {
        int top = r.boosterY - 62, bot = r.boosterY + max(20, r.landLen + 4);
        b = {LZX - 18, (int16_t)top, 37, (int16_t)(bot - top)};
        if (r.entryA) b = join(b, {(int16_t)(LZX - GLOW_ENTRY), (int16_t)(r.boosterY + 5 - GLOW_ENTRY), 2 * GLOW_ENTRY, 2 * GLOW_ENTRY});
        if (r.landA) b = join(b, {(int16_t)(LZX - GLOW_LAND), (int16_t)(r.boosterY + 10 - GLOW_LAND), 2 * GLOW_LAND, 2 * GLOW_LAND});
      }
      if (r.boosterLanded) {
        Xf bf(LZX + r.bdx, 194, r.bang);
        b = join(b, xfBox(bf, -18, -6, 19, 62, 2));
        if (r.truckX < 250) b = join(b, {(int16_t)(r.truckX - 32), 186, 58, 15});
        if (r.s >= 4 && r.s < 14) b = join(b, {(int16_t)min(r.truckX + 16, LZX - 2), 136, (int16_t)(abs(r.truckX + 16 - LZX) + 60), 54});
      }
      int bSig = r.boosterY * 31 + (int)(r.legs * 100) * 7 + (int)(r.bang * 100) * 13 + r.bdx * 17 + r.truckX * 19 +
                 r.landLen * 23 + r.landA * 29 + r.entryA * 37 + r.boosterDown + r.boosterLanded * 2;
      if (bSig != lastBSig || force) {
        lastBSig = bSig;
        moved(r.boosterShown, b);
      }
      for (int i = 0; i < 3; i++) {
        Area d = r.dotA[i] ? Area{(int16_t)(r.dotX[i] - r.dotR[i]), (int16_t)(r.dotY[i] - r.dotR[i]), (int16_t)(2 * r.dotR[i] + 1), (int16_t)(2 * r.dotR[i] + 1)} : Area{0, 0, 0, 0};
        if (d.w || r.dotShown[i].w) moved(r.dotShown[i], d);
      }
      Area dust = r.dustAge >= 0 ? Area{LZX - 52, 182, 104, 22} : Area{0, 0, 0, 0};
      if (dust.w || r.dustShown.w) moved(r.dustShown, dust);
      Area st = r.boosterLanded && (s >= 50 || s < 4) ? Area{LZX - 8, 128, 30, 26} : Area{0, 0, 0, 0};
      if (st.w || r.steamShown.w) moved(r.steamShown, st);
      Area vt = r.onTE && r.te < 0.1f && s >= 40 && !r.storm ? Area{PX + RXO, 128, 32, 18} : Area{0, 0, 0, 0};
      if (vt.w || r.ventShown.w) moved(r.ventShown, vt);
      bool beams = r.onTE && r.te < 0.1f;
      if (beams != lastBeams && r.d <= 128) mark(94, 206);
      lastBeams = beams;
    }

    if (due(lastSea, ms, 200, force)) mark(HZ, SEA_Y1);
    if (due(lastGrass, ms, 300, force)) mark(201, HUD_Y);

    if (due(lastSky, ms, 100, force, &el)) {  // clouds
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
    }
    if (r.starA > 12 && ms / 400 != lastStar) {
      lastStar = ms / 400;
      for (int i = 0; i < TWINKLE; i++)
        if ((lastStar + i * 3) % 4 <= 1) push(starX(i), starY(i), 1, 1);
    }
    if (ms / 100 != lastBlink) {  // warning lights
      lastBlink = ms / 100;
      if (ms % 600 < 100) { push(42, 79, 1, 1); push(110, 79, 1, 1); }
      if (ms % 700 < 100) push(20, 156, 1, 1);
    }
    if (due(lastPrecip, ms, r.snow ? 200 : 100, force)) stepPrecip(t);
  }

  void stepPrecip(float t) {
    Pad& r = *pd;
    if (r.wet) {
      int n = r.storm ? PARTS : PARTS * 2 / 3, sl = r.storm ? -4 : -2;
      for (int i = 0; i < PARTS; i++) {
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
      for (int i = 0; i < PARTS; i++) {
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
    const Pad& r = *pd;
    for (int i = 0; i < STRIPS; i++) {
      if (!dirty[i]) continue;
      int sy = SCENE_Y0 + i * STRIP_H, h = min(STRIP_H, HUD_Y - sy);
      // Screen rows [sy, sy + h) show the scene shifted by the shake.
      Canvas cv{*band, r.sx, -sy + r.sy, -r.sx, 240 - r.sx, sy - r.sy, sy + h - r.sy, (uint16_t*)band->getPointer()};
      if (r.sx || r.sy) band->fillSprite(P_BG);
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
    tft.setTextColor(P_TIME, P_BG);
    tft.setTextPadding(w);
    tft.drawString(s.time, 6, 5, 4);
    tft.setTextColor(P_AMBER, P_BG);
    tft.setTextPadding(tft.textWidth("88", 2));
    tft.drawString(sec, 10 + w, 13, 2);
    tft.setTextPadding(0);
  }

  void drawDate() {
    static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    tft.fillRect(140, 4, 96, 16, P_BG);
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, MONTHS[t.tm_mon]);
    tft.fillRect(212, 6, 23, 12, P_AMBER);  // 12 NOV, then the weekday boxed
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(P_ONBOX);
    tft.drawString(DAYS[t.tm_wday], 224, 12, 1);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(P_TIME);
    tft.drawString(buf, 207, 4, 2);
  }

  void drawHud() {
    tft.drawFastHLine(0, SCENE_Y0 - 1, 240, P_LINE);
    tft.drawFastHLine(0, HUD_Y, 240, P_LINE);
    tft.drawRect(4, HUD_Y + 6, 40, 14, P_AMBER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(P_AMBER);
    tft.drawString("PAD 7", 24, HUD_Y + 13, 1);
  }

  // Weather at the pad: icon, temperature, label.
  void drawWeather(const FaceState& s) {
    tft.fillRect(130, 21, 106, 27, P_BG);
    tft.setTextDatum(MR_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(P_DIM);
      tft.drawString("PAD NO SIGNAL", 235, 44, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    const char* lab = cnWeatherLabel(w);
    tft.setTextColor(P_AMBER);
    tft.drawString(lab, 235, 44, 1);
    tft.setTextColor(P_DIM);
    tft.drawString("PAD", 230 - tft.textWidth(lab, 1), 44, 1);
    char buf[8], u[2] = {s.unit, '\0'};
    snprintf(buf, sizeof(buf), "%d", s.temp);
    int tw = tft.textWidth(buf, 2), x = 235 - tft.textWidth(u, 2) - 5 - tw;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(P_AMBER);
    tft.drawString(buf, x, 22, 2);
    tft.drawCircle(x + tw + 2, 25, 1, P_AMBER);
    tft.drawString(u, x + tw + 5, 22, 2);
    weatherIcon(w, x - 15, 28, pd && pd->night);
  }

  // Mission phase and clock: T+00:12 after liftoff, T-00:08 before the next one.
  void drawFoot(int mim, uint32_t ms) {
    const Pad& r = *pd;
    int sec = mim / 1000;
    bool boom = !r.storm && mim >= 47000 && mim < 48600;
    int key = r.storm ? 1 : (boom ? 1000 + (int)(ms / 120) % 2 : 2000 + sec);
    if (key == shownFoot) return;
    shownFoot = key;
    tft.fillRect(48, HUD_Y + 5, 190, 16, P_BG);
    tft.setTextDatum(ML_DATUM);
    if (r.storm) {
      tft.setTextColor(P_RED);
      tft.drawString("SCRUB", 49, HUD_Y + 13, 1);
      tft.setTextDatum(MR_DATUM);
      tft.drawString("WX HOLD", 236, HUD_Y + 13, 1);
      return;
    }
    static const struct { uint8_t end; const char* name; } PH[] = {
        {3, "LIFTOFF"}, {9, "ASCENT"}, {14, "MAX-Q"}, {18, "MECO"}, {22, "STAGE SEP"}, {28, "BOOSTBACK"}, {34, "COAST"},
        {37, "ENTRY BURN"}, {44, "DESCENT"}, {50, "LANDING BURN"}, {53, "TOUCHDOWN"}, {57, "TERMINAL COUNT"}, {60, "IGNITION"}};
    const char* name = "IGNITION";
    for (const auto& p : PH)
      if (sec < p.end) {
        name = p.name;
        break;
      }
    bool hot = sec < 3 || (sec >= 44 && sec < 50) || sec >= 57;
    tft.setTextColor(boom ? ((ms / 120) & 1 ? TFT_WHITE : P_AMBER) : (hot ? P_AMBER : P_CYAN));
    tft.drawString(boom ? "SONIC BOOM" : name, 49, HUD_Y + 13, 1);
    char buf[12];
    if (sec < 50) snprintf(buf, sizeof(buf), "T+00:%02d", sec);
    else snprintf(buf, sizeof(buf), "T-00:%02d", 60 - sec);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(sec < 50 ? P_CYAN : P_AMBER);
    tft.drawString(buf, 236, HUD_Y + 13, 1);
  }
};

static LaunchPad launchPad;
Face* faceLaunchPad() { return &launchPad; }
