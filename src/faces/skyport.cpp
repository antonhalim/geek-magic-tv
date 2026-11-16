#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "../config.h"
#include "../display.h"
#include "../weather.h"

// Skyport: a rooftop air-taxi port above your city at the real time of day. An air taxi lands
// at the top of every minute and leaves at :48, cargo drones come and go, and the live wind
// blows the windsock and pushes the aircraft around. Storms and strong wind ground every flight.

namespace {

constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
struct Rgb { uint8_t r, g, b; };

// ---- HUD palette ----
const uint16_t P_BG = rgb(7, 9, 20), P_TIME = rgb(238, 244, 255), P_CYAN = rgb(0, 229, 255), P_MAG = rgb(255, 60, 200);
const uint16_t P_AMBER = rgb(255, 176, 64), P_CITY = rgb(120, 188, 255), P_DIM = rgb(79, 122, 166), P_LINE = rgb(29, 51, 80);
const uint16_t P_ONBOX = rgb(4, 20, 28), P_SUN = rgb(255, 210, 58), P_CLOUD = rgb(184, 200, 224), P_CLOUDDK = rgb(106, 114, 144);
const uint16_t P_DROP = rgb(95, 180, 255);
const Rgb CY = {0, 229, 255}, MG = {255, 60, 200}, AM = {255, 176, 64};

// ---- layout ----
const int SCENE_Y0 = 50, HUD_Y = 214, BASE = 168, DECK_Y = 166, RT_X = 188;
const int STRIP_H = 12, STRIPS = (HUD_Y - SCENE_Y0 + STRIP_H - 1) / STRIP_H;
const int BOX_W = 40, BOX_H = 44, QMAX = 128;
const int PAX = 62, PBX = 136, PADY = 176, MAST_X = 172, WALK_Y = 170;
const int STARS = 50, TWINKLE = 10, CLOUDS = 12, PARTS = 44, FARN = 26, CARS = 5, RINGN = 48, PEOPLE = 5, BEACONS = 16, RIPS = 8;
const int HOLD_KMH = 40;  // gusts like this ground the air taxis
const float PIF = 3.14159265f, DEG = PIF / 180;
const char* const DEST[10] = {"DOWNTOWN", "HARBOR", "AIRPORT", "UPTOWN", "ARENA", "MED CENTER", "CAMPUS", "SPACEPORT", "OLD TOWN", "MARINA"};
const uint8_t DEGREE[5] = {0x06, 0x09, 0x09, 0x06, 0x00};  // '~' in text()
// Chamfer clock digits 0-9 and '-': 5x7 cells of 3x3 px. '#' solid; a b c d cut the top-left,
// top-right, bottom-left or bottom-right corner on the diagonal.
const char CHAMFER[11][36] PROGMEM = {
    "a###b#...##...##...##...##...#c###d", "a##....#....#....#....#....#....#..",
    "a###b....#....#a###d#....#....#####", "a###b....#....#.####....#....#c###d",
    "#...##...##...#c####....#....#....#", "######....#....c###b....#....#c###d",
    "a#####....#....####b#...##...#c###d", "####b....#....#....#....#....#....#",
    "a###b#...##...#######...##...#c###d", "a###b#...##...#c####....#....#c###d",
    "...............###................."};

struct Area { int16_t x, y, w, h; };
struct FarT { int16_t x; uint8_t w, h, type, sp; };
struct Craft { float x, y, tilt, rot; bool vis, air, door; };

// Everything the scene reads, so every repaint agrees with what is on screen. Lives on the
// heap while this face is up.
struct Port {
  // light, of 256
  int d, tw, lit, sgn, dim, sa, starA, daySunA, lowSunA, litN;
  bool dawn, night, moon, wet, storm, snow, fog, heavy, flash, hold;
  uint8_t kind, clouds;
  // wind
  int windKmh, windDir, wf;  // wf: strength of 256, full at HOLD_KMH
  float ex, en;              // where it blows to: east and north parts
  int sl;                    // rain slant
  char wind[16], temp[8], label[10], marquee[128];
  int mqW;
  // colours
  uint16_t sky[BASE - SCENE_Y0];
  Rgb grey, fogC, cloudTop, cloudUnd, rainC, midRgb;
  uint16_t far, farRim, mid, midDk, midRim, glass, winA, winC, winM, nCy, nMg, nAm;
  uint16_t rt, rtDk, rtGlass, bbBg, ringC, ringHot;
  uint16_t deck, deckHi, deckWet, deckSnow, pad, padHeat, padRing, fascia, truss, trussLine;
  uint16_t cab, cabGlass, steel, steelHi, hull, hullSh, canopy, doorLit, droneC, cargo, carC, door;
  // geometry
  FarT farT[FARN];
  uint8_t farN, beaconN;
  struct { int16_t x, y; } beacon[BEACONS];
  int8_t rc[RINGN], rs[RINGN];
  // this frame
  float s;
  Craft taxi, drone;
  int16_t figX[PEOPLE];
  bool figOn[PEOPLE];
  int sweepX, sweepDir;
  float sockDX, sockDY, ripPh, steamPh;
  int ringPh, adPh, mq, dishW, modeA, modeB, chase, spin;
  bool blinkOn, signOn;
  uint32_t beaconOn;
  float cloudX[CLOUDS];
  int16_t cloudShown[CLOUDS];
  int16_t car[2][CARS];
  int16_t dropX[PARTS], dropY[PARTS];
  int16_t ripX[RIPS], ripY[RIPS];
  int16_t bolt[7], boltX, boltY;
  Area taxiShown, droneShown, figShown[PEOPLE], sweepShown;
  Area q[QMAX];  // areas to repaint this tick
  uint8_t qn;
};
Port* pt = nullptr;

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
  Rgb c = lerpRgb(n, dy, pt->d);
  return lerpRgb(c, pt->dawn ? da : du, pt->tw * 218 >> 8);
}
Rgb K(Rgb n, Rgb dy, Rgb du) { return K(n, dy, du, du); }

// Everything the scene draws goes through this, into the strip or box sprite, so the same
// code paints the whole scene and repaints small areas. Light is blended into the pixels
// already in the sprite.
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
    } else {  // haze: move toward c; +4/+2 rounds to the nearest 565 level so thin layers don't drift green
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

// A tilted frame: v across, u up from the origin, turned clockwise by the angle.
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
void xquad(Canvas& cv, const Xf& f, const float* v, const float* u, uint16_t col) {
  float xs[4], ys[4];
  for (int i = 0; i < 4; i++) {
    xs[i] = f.fx(v[i], u[i]);
    ys[i] = f.fy(v[i], u[i]);
  }
  cv.quad(xs, ys, col);
}
void xline(Canvas& cv, const Xf& f, float va, float ua, float vb, float ub, uint16_t col) {
  cv.line(f.X(va, ua), f.Y(va, ua), f.X(vb, ub), f.Y(vb, ub), col);
}

// The 5x7 font from TFT_eSPI, drawn pixel by pixel so it can be clipped to [cx0, cx1).
// '~' is a degree ring. sc scales it up; sq keeps every other row, for letters painted flat on the deck.
int text(Canvas& cv, const char* s, int x, int y, uint16_t c, int cx0 = 0, int cx1 = 240, int sc = 1, bool sq = false) {
  int n = strlen(s), w = n * 6 * sc - sc, h = (sq ? 4 : 8) * sc;
  int xa = max(x, cx0), xb = min(x + w, cx1);
  if (xb <= xa || !cv.hits(xa, y, xb - xa, h)) return w;
  for (int i = 0; i < n; i++, x += 6 * sc) {
    if (x + 5 * sc <= max(cx0, cv.x0) || x >= min(cx1, cv.x1)) continue;
    for (int col = 0; col < 5; col++) {
      int px = x + col * sc;
      if (px < cx0 || px + sc > cx1) continue;
      uint8_t bits = s[i] == '~' ? DEGREE[col] : pgm_read_byte(font + (uint8_t)s[i] * 5 + col);
      for (int row = 0; row < 8; row++) {
        if (!((bits >> row) & 1) || (sq && (row & 1))) continue;
        cv.rect(px, y + (sq ? row / 2 : row) * sc, sc, sc, c);
      }
    }
  }
  return w;
}

// A neon tube: painted on by day, glowing after dark.
void neon(Canvas& cv, int x, int y, int w, int h, uint16_t c, Rgb g) {
  if (!cv.hits(x - 1, y - 1, w + 2, h + 2)) return;
  cv.rect(x, y, w, h, c);
  if (pt->sgn > 20) cv.addRect(x - 1, y - 1, w + 2, h + 2, g, 56 * pt->sgn >> 8);
}

// Glass panes in a grid, some lit after dark.
void windows(Canvas& cv, int x0, int x1, int y0, int y1, int sx, int sy, int w, int h, uint32_t seed) {
  const Port& r = *pt;
  if (!cv.hits(x0, y0, x1 - x0, y1 - y0)) return;
  int xa = max(x0, cv.x0 - w - sx), xs = x0 + max(0, (xa - x0) / sx) * sx;
  for (int y = y0; y < y1; y += sy) {
    if (y + h <= cv.y0) continue;
    if (y >= cv.y1) break;
    for (int x = xs; x + w <= x1 && x < cv.x1; x += sx) {
      uint32_t k = hsh(seed + x * 131 + y * 7);
      uint16_t c = r.glass;
      if ((int)(k & 255) < r.litN) {
        int pick = (k >> 8) & 15;
        c = pick < 2 ? r.winC : pick < 3 ? r.winM : r.winA;
      }
      cv.rect(x, y, w, h, c);
    }
  }
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

int cloudY(int i) { return 54 + (int)(hsh(i * 9 + 5) % (pt->heavy ? 40 : 46)); }
int cloudW(int i) { return 30 + (int)(hsh(i * 9 + 1) % 70); }
int cloudH(int i) { return 2 + (int)(hsh(i * 9 + 2) % 3); }
int starX(int i) { return (int)(hsh(i * 5 + 1) % 240); }
int starY(int i) { return SCENE_Y0 + (int)(hsh(i * 5 + 2) % 60); }

void drawSky(Canvas& cv) {
  const Port& r = *pt;
  if (cv.y0 >= BASE) return;
  for (int y = max(cv.y0, SCENE_Y0); y < min(cv.y1, BASE); y++) cv.rect(cv.x0, y, cv.x1 - cv.x0, 1, r.sky[y - SCENE_Y0]);
  if (r.starA > 12) {
    for (int i = 0; i < STARS; i++) {
      int a = r.starA * (77 + (int)(hsh(i * 5 + 3) % 154)) >> 8;
      if (i < TWINKLE && (r.ringPh / 3 + i * 3) % 4 == 0) a = a * 2 / 5;
      cv.mix(starX(i), starY(i), 1, {230, 236, 255}, a);
    }
  }
  if (r.moon && cv.hits(46, 50, 36, 36)) {
    cv.glow(64, 66, 16, {220, 230, 255}, 41);
    cv.dot(64, 66, 5, rgb(242, 240, 226));
    cv.dot(67, 64, 4, r.sky[64 - SCENE_Y0]);
  }
  if (r.daySunA > 8 && cv.hits(126, 40, 60, 56)) {
    cv.glow(156, 66, 28, {255, 244, 210}, 115 * r.daySunA >> 8);
    static const uint8_t HW[6] = {5, 5, 5, 4, 3, 2};
    for (int k = -5; k <= 5; k++) cv.mix(156 - HW[abs(k)], 66 + k, 2 * HW[abs(k)] + 1, {255, 252, 236}, r.daySunA);
  }
  if (r.lowSunA > 8 && cv.hits(60, BASE - 50, 100, 50)) {  // the sun going down behind the towers
    cv.glow(110, BASE + 4, 50, {255, 120, 90}, 100 * r.lowSunA >> 8);
    cv.glow(110, BASE + 4, 22, {255, 200, 120}, 110 * r.lowSunA >> 8);
  }
  if (r.flash) cv.addRect(0, SCENE_Y0, 240, BASE - SCENE_Y0, {190, 200, 255}, 90);
}

void drawClouds(Canvas& cv) {
  const Port& r = *pt;
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

// ---- the skyline ----

int farTop(const FarT& f) { return BASE - f.h; }

// Slim towers far off: slabs with light ribbons, needles, setbacks and slanted roofs.
void drawFar(Canvas& cv) {
  const Port& r = *pt;
  if (!cv.rows(SCENE_Y0, BASE)) return;
  for (int i = 0; i < r.farN; i++) {
    const FarT& f = r.farT[i];
    int top = farTop(f);
    if (!cv.hits(f.x, top - f.sp - 6, f.w, f.h + f.sp + 6)) continue;
    switch (f.type) {
      case 1:  // needle
        cv.rect(f.x, top, f.w, f.h, r.far);
        for (int k = 1; k <= f.sp; k++) {
          int hw = f.w * (f.sp - k) / (2 * f.sp);
          cv.rect(f.x + f.w / 2 - hw, top - k, max(1, 2 * hw), 1, r.far);
        }
        cv.rect(f.x + f.w / 2, top - f.sp - 5, 1, 5, r.far);
        break;
      case 2: {  // setback
        int lo = f.h * 2 / 3;
        cv.rect(f.x, BASE - lo, f.w, lo, r.far);
        cv.rect(f.x + 2, top, f.w - 4, f.h - lo, r.far);
        cv.rect(f.x + 2, top, 1, f.h - lo, r.farRim);
        break;
      }
      case 3:  // slanted roof
        cv.rect(f.x, top + 5, f.w, f.h - 5, r.far);
        for (int k = 0; k < 5; k++) cv.rect(f.x + f.w * (4 - k) / 5, top + k, f.w - f.w * (4 - k) / 5, 1, r.far);
        break;
      default:  // slab with light ribbons
        cv.rect(f.x, top, f.w, f.h, r.far);
        if (r.sgn > 20)
          for (int y = top + 4; y < BASE; y += 9) cv.add(f.x, y, f.w, CY, 50 * r.sgn >> 8);
    }
    cv.rect(f.x, top + (f.type == 3 ? 5 : 0), 1, f.h, r.farRim);
    if (r.litN > 10)
      for (int k = 0; k < 4; k++) {
        uint32_t h = hsh(i * 17 + k);
        cv.add(f.x + 1 + (int)(h % max(1, f.w - 2)), top + 4 + (int)((h >> 8) % max(1, f.h - 10)), 1, k == 3 ? CY : Rgb{255, 190, 110},
               r.litN * 2);
      }
  }
  for (int i = 0; i < r.beaconN; i++)
    if ((r.beaconOn >> i) & 1) cv.px(r.beacon[i].x, r.beacon[i].y, rgb(255, 50, 50));
}

void drawStorm(Canvas& cv) {
  const Port& r = *pt;
  if (!r.flash) return;
  int ys = SCENE_Y0, ye = r.boltY;
  for (int i = 0; i < 6; i++) {
    int ya = ys + (ye - ys) * i / 6, yb = ys + (ye - ys) * (i + 1) / 6, xa = r.bolt[i], xb = i == 5 ? r.boltX : r.bolt[i + 1];
    cv.line(xa + 1, ya, xb + 1, yb, rgb(170, 180, 255));
    cv.line(xa, ya, xb, yb, TFT_WHITE);
  }
  cv.glow(r.boltX, r.boltY, 16, {200, 210, 255}, 154);
}

void haze(Canvas& cv, int y1, int fogA, int greyA) {
  const Port& r = *pt;
  int ya = max(cv.y0, SCENE_Y0), yb = min(cv.y1, y1);
  if (yb <= ya) return;
  if (r.fog) cv.mixRect(cv.x0, ya, cv.x1 - cv.x0, yb - ya, r.fogC, fogA);
  else if (r.heavy && greyA) cv.mixRect(cv.x0, ya, cv.x1 - cv.x0, yb - ya, r.grey, greyA);
}

// Sky lanes: traffic threading between the towers.
void drawLanes(Canvas& cv) {
  const Port& r = *pt;
  if (!cv.rows(99, 109)) return;
  if (r.sgn > 20)
    for (int x = 4; x < RT_X; x += 12) cv.add(x, 104, 1, CY, 90 * r.sgn >> 8);
  for (int l = 0; l < 2; l++) {
    int y = l ? 106 : 101;
    for (int i = 0; i < CARS; i++) {
      int x = r.car[l][i];
      if (!cv.hits(x - 1, y - 1, 8, 4)) continue;
      cv.rect(x, y, 6, 2, r.carC);
      cv.rect(l ? x : x + 5, y, 1, 1, rgb(255, 250, 230));  // headlight
      cv.rect(l ? x + 5 : x, y + 1, 1, 1, rgb(255, 40, 60));
      if (r.lit > 80) cv.add(l ? x - 1 : x + 6, y, 1, {255, 240, 210}, r.lit / 2);
    }
  }
}

// A holo ring around a tower: dots on an ellipse, a bright run sweeping round.
void ring(Canvas& cv, int cx, int cy, int rx, int ry, int ph, bool front) {
  const Port& r = *pt;
  if (!cv.hits(cx - rx - 2, cy - ry - 2, 2 * rx + 5, 2 * ry + 5)) return;
  for (int k = 0; k < RINGN; k++) {
    if ((r.rs[k] > 0) != front) continue;
    int x = cx + (r.rc[k] * rx >> 7), y = cy + (r.rs[k] * ry >> 7);
    bool hot = imod(k - ph, RINGN) < 7;
    cv.px(x, y, hot ? r.ringHot : r.ringC);
    if (hot && r.sgn > 20) cv.add(x - 1, y, 3, CY, 80 * r.sgn >> 8);
  }
}

// The near skyline, one building at a time, left to right.
void drawMid(Canvas& cv) {
  const Port& r = *pt;
  if (!cv.rows(SCENE_Y0, BASE)) return;
  int bot = BASE;
  if (cv.hits(0, 84, 21, bot - 84)) {  // crown tower
    static const uint8_t IN[4] = {4, 3, 2, 1};
    for (int k = 0; k < 4; k++) cv.rect(IN[k], 84 + k, 20 - 2 * IN[k], 1, r.mid);
    cv.rect(0, 88, 20, bot - 88, r.mid);
    cv.rect(0, 88, 1, bot - 88, r.midRim);
    for (int x = 2; x < 19; x += 3) neon(cv, x, 90, 1, 1, r.nCy, CY);
    windows(cv, 2, 19, 95, bot, 4, 4, 2, 2, 100);
  }
  if (cv.hits(27, 48, 14, bot - 48)) {  // needle with magenta ribbons
    cv.rect(28, 76, 12, bot - 76, r.mid);
    for (int k = 1; k <= 20; k++) {
      int hw = 6 * (20 - k) / 20;
      cv.rect(34 - hw, 76 - k, max(1, 2 * hw), 1, r.mid);
    }
    cv.rect(34, 50, 1, 6, r.steel);
    cv.rect(28, 76, 1, bot - 76, r.midRim);
    for (int y = 80; y < bot; y += 7) {
      neon(cv, 29, y, 11, 1, r.nMg, MG);
      windows(cv, 30, 39, y + 2, y + 6, 3, 4, 1, 3, 200 + y);
    }
  }
  if (cv.hits(44, 88, 37, bot - 88)) {  // arcology: terraced, wider at the base
    float xs[4] = {44, 80, 70, 54}, ys[4] = {(float)bot, (float)bot, 102, 102};
    cv.quad(xs, ys, r.mid);
    cv.line(44, bot - 1, 54, 102, r.midRim);
    cv.rect(58, 98, 8, 4, r.mid);
    cv.rect(59, 97, 6, 1, r.midRim);
    cv.rect(62, 89, 1, 8, r.steel);
    for (int y = 106; y < bot; y += 6) {
      int hw = 8 + (y - 102) * 10 / 66;
      cv.rect(62 - hw, y, 2 * hw, 1, r.midDk);
      windows(cv, 63 - hw, 61 + hw, y + 2, y + 4, 3, 6, 2, 1, 300 + y);
    }
  }
  if (cv.hits(76, 56, 34, bot - 56)) {  // ring tower
    ring(cv, 93, 92, 17, 3, r.ringPh, false);
    ring(cv, 93, 118, 12, 2, RINGN - r.ringPh, false);
    cv.rect(86, 68, 14, bot - 68, r.mid);
    for (int k = 1; k <= 8; k++) {
      int hw = 7 * (8 - k) / 8;
      cv.rect(93 - hw, 68 - k, max(1, 2 * hw), 1, r.mid);
    }
    cv.rect(86, 68, 1, bot - 68, r.midRim);
    for (int y = 72; y < bot; y += 3) {
      uint32_t k = hsh(y * 3 + 7);
      cv.rect(87, y, 12, 1, (int)(k & 255) < r.litN * 3 / 2 ? ((k >> 9) & 3 ? r.winC : r.winA) : r.glass);
    }
    ring(cv, 93, 92, 17, 3, r.ringPh, true);
    ring(cv, 93, 118, 12, 2, RINGN - r.ringPh, true);
  }
  if (cv.hits(105, 86, 33, bot - 86)) {  // twin towers and their sky bridges
    cv.rect(106, 88, 12, bot - 88, r.mid);
    cv.rect(124, 98, 12, bot - 98, r.mid);
    cv.rect(106, 88, 1, bot - 88, r.midRim);
    cv.rect(124, 98, 1, bot - 98, r.midRim);
    for (int x = 107; x < 118; x += 2) neon(cv, x, 89, 1, 1, r.nAm, AM);
    for (int x = 125; x < 136; x += 2) neon(cv, x, 99, 1, 1, r.nAm, AM);
    windows(cv, 108, 117, 92, bot, 3, 2, 1, 1, 400);
    windows(cv, 126, 135, 102, bot, 3, 2, 1, 1, 500);
    for (int y : {112, 132}) {
      cv.rect(118, y, 6, 3, r.midDk);
      neon(cv, 118, y + 3, 6, 1, r.nCy, CY);
    }
  }
  if (cv.hits(141, 76, 22, bot - 76)) {  // blade: slanted roof, magenta edge, holo ad
    float xs[4] = {142, 162, 162, 142}, ys[4] = {78, 94, (float)bot, (float)bot};
    cv.quad(xs, ys, r.mid);
    cv.line(142, 78, 161, 93, r.midRim);
    neon(cv, 142, 80, 1, bot - 80, r.nMg, MG);
    windows(cv, 145, 161, 98, bot, 4, 5, 2, 2, 600);
    if (cv.hits(146, 104, 13, 15)) {
      cv.rect(146, 104, 13, 15, r.bbBg);
      neon(cv, 146, 104, 13, 1, r.nCy, CY);
      for (int k = 0; k < 3; k++) cv.rect(148, 107 + k * 4, 2 + (int)(hsh(r.adPh * 3 + k) % 9), 2, k == 1 ? r.nMg : r.nCy);
    }
  }
  if (cv.hits(165, 100, 22, bot - 100)) {  // low block with a dish
    cv.rect(166, 112, 20, bot - 112, r.mid);
    cv.rect(166, 112, 1, bot - 112, r.midRim);
    cv.rect(171, 109, 7, 2, r.steel);
    cv.rect(172, 108, 5, 1, r.steel);
    cv.rect(181, 102, 1, 10, r.steel);
    windows(cv, 168, 185, 116, bot, 3, 4, 2, 2, 700);
  }
}

// The megatower on the right: glass crown, neon seams and the weather billboard.
void drawTower(Canvas& cv) {
  const Port& r = *pt;
  if (!cv.hits(RT_X, SCENE_Y0, 240 - RT_X, HUD_Y - SCENE_Y0)) return;
  float xs[4] = {188, 240, 240, 208}, ys[4] = {64, 64, 50, 50};
  cv.quad(xs, ys, r.rtGlass);
  for (int y = 53; y < 64; y += 3) cv.mix(max(188, 208 - (y - 50) * 20 / 14), y, 52, WHITE, 40);
  cv.line(208, 50, 188, 64, r.nCy);
  cv.rect(188, 64, 52, HUD_Y - 64, r.rt);
  neon(cv, 188, 64, 1, HUD_Y - 64, r.nCy, CY);
  for (int y = 110; y < HUD_Y; y += 4) cv.rect(190, y, 50, 1, r.rtDk);
  windows(cv, 191, 239, 111, 154, 4, 4, 2, 2, 800);
  windows(cv, 201, 239, 155, HUD_Y, 4, 4, 2, 2, 900);
  cv.rect(190, 155, 9, 11, r.rtDk);
  cv.rect(191, 156, 7, 10, r.door);
  if (!cv.hits(186, 66, 54, 44)) return;  // the billboard
  if (r.sgn > 20) cv.addRect(187, 68, 53, 40, MG, 24 * r.sgn >> 8);
  cv.rect(189, 70, 50, 36, r.bbBg);
  cv.rect(189, 70, 50, 1, r.nMg);
  cv.rect(189, 105, 50, 1, r.nMg);
  cv.rect(189, 70, 1, 36, r.nMg);
  cv.rect(238, 70, 1, 36, r.nMg);
  if (cv.rows(73, 81)) {
    text(cv, r.marquee, 191 - r.mq, 73, P_CYAN, 191, 237);
    text(cv, r.marquee, 191 - r.mq + r.mqW, 73, P_CYAN, 191, 237);
  }
  cv.rect(191, 82, 46, 1, rgb(90, 20, 80));
  int w = strlen(r.temp) * 12 - 2;
  text(cv, r.temp, 214 - w / 2, 85, P_TIME, 190, 238, 2);
  w = strlen(r.label) * 6 - 1;
  text(cv, r.label, 214 - w / 2, 97, P_AMBER, 190, 238);
}

// ---- the deck ----

void pad(Canvas& cv, int cx, int cy, int rx, int ry, const char* lab, int mode) {
  const Port& r = *pt;
  if (!cv.hits(cx - rx - 2, cy - ry - 2, 2 * rx + 5, 2 * ry + 9)) return;
  uint16_t fill = r.snow ? r.padHeat : r.pad;
  for (int k = -ry; k <= ry; k++) {
    int hw = (int)lroundf(rx * sqrtf(1 - (float)(k * k) / (ry * ry)));
    cv.rect(cx - hw, cy + k, 2 * hw + 1, 1, fill);
  }
  int ir = rx * 7 / 10, iry = ry * 7 / 10;
  for (int k = -iry; k <= iry; k++) {
    int hw = (int)lroundf(ir * sqrtf(1 - (float)(k * k) / (iry * iry)));
    if (abs(k) == iry) cv.rect(cx - hw, cy + k, 2 * hw + 1, 1, r.padRing);
    else {
      cv.px(cx - hw, cy + k, r.padRing);
      cv.px(cx + hw, cy + k, r.padRing);
    }
  }
  text(cv, lab, cx - 2, cy - 2, r.padRing, 0, 240, 1, true);
  int n = rx > 30 ? 14 : 10;
  for (int i = 0; i < n; i++) {
    float a = i * 2 * PIF / n;
    int x = cx + (int)lroundf(cosf(a) * rx), y = cy + (int)lroundf(sinf(a) * ry);
    Rgb c;
    if (mode == 3) {
      if (!r.blinkOn) continue;
      c = AM;
    } else if (mode == 1) {
      if (imod(i - r.chase * n / 14, n) >= 3) continue;
      c = CY;
    } else c = mode == 2 ? Rgb{60, 255, 140} : Rgb{200, 230, 255};
    cv.rect(x - 1, y, 2, 1, c565(lerpRgb(c, WHITE, 60)));
    if (r.lit > 60) cv.addRect(x - 2, y - 1, 4, 3, c, r.lit / 5);
    if (r.wet)
      for (int k = 1; k <= 4; k++) cv.add(x - 1 + (k & 1), y + k, 1, c, 110 - k * 22);
  }
}

void drawSock(Canvas& cv) {
  const Port& r = *pt;
  if (!cv.hits(MAST_X - 24, 135, 49, 30)) return;
  float len = sqrtf(r.sockDX * r.sockDX + r.sockDY * r.sockDY);
  if (len < 1) return;
  float nx = -r.sockDY / len, ny = r.sockDX / len;
  for (int i = 0; i < 3; i++) {
    float a0 = i / 3.0f, a1 = (i + 1) / 3.0f, w0 = 2.2f - a0 * 1.1f, w1 = 2.2f - a1 * 1.1f;
    float x0 = MAST_X + 0.5f + r.sockDX * a0, y0 = 141 + r.sockDY * a0, x1 = MAST_X + 0.5f + r.sockDX * a1, y1 = 141 + r.sockDY * a1;
    float xs[4] = {x0 + nx * w0, x1 + nx * w1, x1 - nx * w1, x0 - nx * w0}, ys[4] = {y0 + ny * w0, y1 + ny * w1, y1 - ny * w1, y0 - ny * w0};
    cv.quad(xs, ys, i & 1 ? rgb(240, 240, 240) : rgb(255, 110, 30));
  }
}

void drawDeck(Canvas& cv) {
  const Port& r = *pt;
  if (!cv.rows(126, HUD_Y) || cv.x0 >= RT_X + 4) return;
  if (cv.rows(DECK_Y, 186)) {
    cv.rect(0, DECK_Y, RT_X, 20, r.snow ? r.deckSnow : r.wet ? r.deckWet : r.deck);
    cv.rect(0, DECK_Y, RT_X, 1, r.deckHi);
    if (r.wet)
      for (int y = 170; y < 186; y += 4) cv.mix(0, y, RT_X, {200, 220, 240}, 26);
    if (r.snow)
      for (int i = 0; i < 40; i++) cv.px((int)(hsh(i * 3) % RT_X), 167 + (int)(hsh(i * 3 + 1) % 19), r.deck);
    else
      for (int x = 2; x < RT_X; x += 8) cv.rect(x, 184, 4, 1, r.nAm);
    pad(cv, PAX, PADY, 34, 8, "A", r.modeA);
    pad(cv, PBX, PADY, 22, 6, "B", r.modeB);
    if (r.wet)
      for (int i = 0; i < RIPS; i++) cv.mix(r.ripX[i], r.ripY[i], 2, {220, 236, 255}, 150);
  }
  if (cv.rows(186, HUD_Y)) {
    cv.rect(0, 186, RT_X, 5, r.fascia);
    neon(cv, 0, 188, RT_X, 1, r.nCy, CY);
    cv.rect(0, 191, RT_X, HUD_Y - 191, r.truss);
    for (int x = 0; x < RT_X; x += 16) {
      cv.line(x, 192, x + 8, 212, r.trussLine);
      cv.line(x + 8, 212, x + 16, 192, r.trussLine);
    }
    for (int x = 8; x < RT_X; x += 32) cv.px(x, 208, r.nAm);
  }
  if (cv.hits(0, 126, 26, 40)) {  // control cab with its radar
    cv.rect(2, 136, 22, 30, r.cab);
    cv.rect(1, 134, 24, 2, r.snow ? r.deckSnow : r.steel);
    cv.rect(4, 140, 18, 8, r.cabGlass);
    cv.rect(12, 143, 3, 5, rgb(6, 14, 22));
    cv.rect(2, 136, 1, 30, r.midRim);
    cv.rect(13, 128, 1, 6, r.steel);
    cv.rect(13 - r.dishW / 2, 127, r.dishW + 1, 2, r.steelHi);
  }
  if (cv.hits(MAST_X - 60, 126, 82, 40)) {  // windsock, with a holo tag of the wind
    cv.rect(MAST_X, 140, 1, 26, r.steel);
    cv.rect(MAST_X - 1, 165, 3, 1, r.steel);
    drawSock(cv);
    int w = strlen(r.wind) * 6 - 1, x = MAST_X + 14 - w;
    if (cv.hits(x - 2, 128, w + 4, 10)) {
      cv.mixRect(x - 2, 128, w + 4, 10, {4, 10, 20}, 190);
      text(cv, r.wind, x, 129, r.nCy);
    }
  }
}

// ---- things that move ----

void drawTaxi(Canvas& cv, const Craft& T) {
  const Port& r = *pt;
  if (!T.vis) return;
  Xf f(T.x, T.y, T.tilt);
  if (!cv.hits((int)T.x - 24, (int)T.y - 20, 49, 26)) return;
  if (r.lit > 90) cv.addRect((int)T.x - 13, (int)T.y - 1, 26, 2, CY, r.lit / 4);  // underglow
  xrect(cv, f, -12, 0, 11, 1, r.steel);
  xrect(cv, f, -8, 1, -7, 3, r.steel);
  xrect(cv, f, 6, 1, 7, 3, r.steel);
  xrect(cv, f, -14, 3, 14, 10, r.hull);
  static const float NV[4] = {14, 19, 18, 14}, NU[4] = {3, 5, 8, 10}, TV[4] = {-14, -14, -18, -18}, TU[4] = {3, 10, 9, 5};
  xquad(cv, f, NV, NU, r.hull);
  xquad(cv, f, TV, TU, r.hull);
  xrect(cv, f, -17, 3, 15, 4, r.hullSh);
  static const float CV[4] = {4, 17, 14, 4}, CU[4] = {7, 7, 10, 10};
  xquad(cv, f, CV, CU, r.canopy);
  cv.px(f.X(9, 9), f.Y(9, 9), rgb(150, 230, 255));
  xrect(cv, f, -16, 5, 16, 6, r.nCy);
  if (T.door) xrect(cv, f, -4, 4, 0, 9, r.doorLit);
  xrect(cv, f, -11, 10, -10, 12, r.steel);
  xrect(cv, f, 7, 10, 8, 12, r.steel);
  for (float rx : {-10.5f, 7.5f}) {
    if (T.rot > 0.25f) {
      xline(cv, f, rx - 7, 12, rx + 7, 12, r.spin & 1 ? rgb(170, 186, 204) : rgb(214, 226, 240));
      float b = (r.spin * 37 % 14) - 7;
      xline(cv, f, rx + b - 1, 12, rx + b + 1, 12, rgb(240, 246, 255));
    } else {
      xline(cv, f, rx - 6, 12, rx + 6, 12, r.steelHi);
    }
  }
  if (r.blinkOn) cv.px(f.X(-17, 9), f.Y(-17, 9), rgb(255, 50, 50));
  if (T.air && (r.spin % 20) == 0) cv.px(f.X(18, 6), f.Y(18, 6), TFT_WHITE);
}

void drawDrone(Canvas& cv, const Craft& D) {
  const Port& r = *pt;
  if (!D.vis) return;
  Xf f(D.x, D.y, D.tilt);
  if (!cv.hits((int)D.x - 15, (int)D.y - 11, 31, 13)) return;
  xline(cv, f, -4, 0, -3, 3, r.droneC);
  xline(cv, f, 4, 0, 3, 3, r.droneC);
  xrect(cv, f, -3, 1, 3, 4, r.cargo);
  xrect(cv, f, -5, 4, 5, 6, r.droneC);
  cv.px(f.X(0, 5), f.Y(0, 5), P_CYAN);
  xline(cv, f, -10, 6, 10, 6, r.droneC);
  for (int sd = -1; sd <= 1; sd += 2) {
    if (D.rot > 0.25f) xline(cv, f, sd * 10 - 3, 7, sd * 10 + 3, 7, r.spin & 1 ? rgb(170, 186, 204) : rgb(214, 226, 240));
    else xline(cv, f, sd * 10 - 2, 7, sd * 10 + 2, 7, r.steelHi);
  }
  if (r.blinkOn) {
    cv.px(f.X(-10, 6), f.Y(-10, 6), rgb(255, 50, 50));
    cv.px(f.X(10, 6), f.Y(10, 6), rgb(60, 255, 120));
  }
}

void drawActors(Canvas& cv) {
  const Port& r = *pt;
  if (!cv.rows(SCENE_Y0, HUD_Y)) return;
  const Craft& T = r.taxi;
  if (T.vis && T.rot > 0.5f && T.y > 150 && cv.hits(PAX - 40, 164, 80, 24))  // downwash rippling across the pad
    for (int k = 0; k < 3; k++) {
      float ph = fmodf(r.ripPh + k / 3.0f, 1);
      int rx = 10 + (int)(ph * 26), ry = 2 + (int)(ph * 5), a = (int)(120 * (1 - ph));
      for (int i = 0; i < 24; i++) {
        float an = i * PIF / 12;
        cv.mix(PAX + (int)lroundf(cosf(an) * rx), PADY + 1 + (int)lroundf(sinf(an) * ry), 1, {230, 240, 255}, a);
      }
    }
  static const uint16_t COAT[3] = {rgb(60, 70, 100), rgb(150, 40, 70), rgb(40, 120, 130)};
  for (int i = 0; i < PEOPLE; i++) {
    if (!r.figOn[i]) continue;
    int x = r.figX[i], y = WALK_Y;
    if (!cv.hits(x - 3, y - 13, 8, 14)) continue;
    bool st = (x >> 1) & 1;
    cv.rect(x, y - 8, 2, 2, rgb(230, 200, 170));
    cv.rect(x, y - 6, 2, 4, COAT[i % 3]);
    cv.rect(x + (st ? 0 : 1), y - 2, 1, 2, rgb(30, 34, 44));
    cv.rect(x + (st ? 1 : 0), y - 2, 1, 1, rgb(30, 34, 44));
    if (r.wet) {
      uint16_t u = i & 1 ? r.nMg : r.nCy;
      cv.rect(x - 2, y - 11, 6, 1, u);
      cv.rect(x - 1, y - 12, 4, 1, u);
      cv.px(x + 1, y - 10, rgb(30, 34, 44));
    }
  }
  drawDrone(cv, r.drone);
  drawTaxi(cv, T);
  if (r.hold && cv.rows(DECK_Y, 186)) {  // tied down
    cv.line(PAX - 12, PADY, PAX - 17, PADY + 4, rgb(220, 180, 40));
    cv.line(PAX + 11, PADY, PAX + 16, PADY + 4, rgb(220, 180, 40));
    cv.line(PBX - 4, PADY, PBX - 8, PADY + 3, rgb(220, 180, 40));
    cv.line(PBX + 4, PADY, PBX + 8, PADY + 3, rgb(220, 180, 40));
  }
  if (r.snow) {
    if (cv.hits(PAX - 30, 158, 60, 20) || cv.hits(PBX - 20, 158, 40, 20))  // heated pads steaming
      for (int i = 0; i < 9; i++) {
        float ph = fmodf(r.steamPh + i * 0.37f, 1);
        int x = (i < 5 ? PAX + (i - 2) * 11 : PBX + (i - 7) * 9) + (int)lroundf(sinf(ph * 6 + i) * 2);
        cv.mix(x, PADY - 3 - (int)(ph * 14), 1, {236, 240, 248}, (int)(170 * (1 - ph)));
      }
    int x = r.sweepX, y = 185;  // the robot clearing the walkway
    if (cv.hits(x - 9, y - 10, 19, 11)) {
      cv.rect(x - 5, y - 6, 10, 5, rgb(232, 178, 58));
      cv.rect(x - 2, y - 8, 5, 2, rgb(180, 130, 40));
      cv.rect(x - 4, y - 1, 3, 1, rgb(30, 34, 44));
      cv.rect(x + 1, y - 1, 3, 1, rgb(30, 34, 44));
      cv.rect(r.sweepDir > 0 ? x + 5 : x - 7, y - 5, 2, 5, r.steelHi);
      if (r.blinkOn) cv.px(x, y - 9, P_AMBER);
      for (int i = 0; i < 3; i++) cv.px(x + r.sweepDir * (8 + i * 2), y - 4 - (i & 1) * 2, TFT_WHITE);
    }
  }
}

void drawPrecip(Canvas& cv) {
  const Port& r = *pt;
  if (r.wet) {
    int sl = r.sl;
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
  if (r.fog) cv.mixRect(cv.x0, max(cv.y0, SCENE_Y0), cv.x1 - cv.x0, min(cv.y1, HUD_Y) - max(cv.y0, SCENE_Y0), r.fogC, 56);
  if (r.hold && r.signOn && cv.hits(56, 117, 82, 23)) {
    cv.mixRect(56, 117, 82, 23, {24, 12, 0}, 215);
    cv.rect(56, 117, 82, 1, r.nAm);
    cv.rect(56, 139, 82, 1, r.nAm);
    cv.rect(56, 117, 1, 23, r.nAm);
    cv.rect(137, 117, 1, 23, r.nAm);
    text(cv, "FLIGHTS HOLD", 62, 120, P_AMBER);
    const char* why = r.storm ? "THUNDERSTORM" : r.wind;
    int w = strlen(why) * 6 - 1;
    text(cv, why, 97 - w / 2, 130, rgb(255, 214, 160));
  }
}

void drawAll(Canvas& cv) {
  drawSky(cv);
  drawClouds(cv);
  drawFar(cv);
  drawStorm(cv);
  haze(cv, BASE, 170, 50);
  drawLanes(cv);
  drawMid(cv);
  haze(cv, BASE, 110, 0);
  drawTower(cv);
  drawDeck(cv);
  drawActors(cv);
  drawPrecip(cv);
}

// Union of two areas.
Area join(const Area& a, const Area& b) {
  if (!a.w) return b;
  if (!b.w) return a;
  int xa = min(a.x, b.x), ya = min(a.y, b.y), xb = max(a.x + a.w, b.x + b.w), yb = max(a.y + a.h, b.y + b.h);
  return {(int16_t)xa, (int16_t)ya, (int16_t)(xb - xa), (int16_t)(yb - ya)};
}
Area mk(int x, int y, int w, int h) { return {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h}; }

// Bounding box of something drawn in a tilted frame.
Area xfBox(const Xf& f, float v0, float u0, float v1, float u1, int pad) {
  int xs[4] = {f.X(v0, u0), f.X(v1, u0), f.X(v0, u1), f.X(v1, u1)}, ys[4] = {f.Y(v0, u0), f.Y(v1, u0), f.Y(v0, u1), f.Y(v1, u1)};
  int xa = xs[0], xb = xs[0], ya = ys[0], yb = ys[0];
  for (int i = 1; i < 4; i++) {
    xa = min(xa, xs[i]); xb = max(xb, xs[i]); ya = min(ya, ys[i]); yb = max(yb, ys[i]);
  }
  return mk(xa - pad, ya - pad, xb - xa + 2 * pad + 1, yb - ya + 2 * pad + 1);
}

}  // namespace

class Skyport : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band = new TFT_eSprite(&tft);  // allocated while this face is up, so other faces get the RAM back
    box = new TFT_eSprite(&tft);
    band->setColorDepth(16);
    bandOk = band->createSprite(240, STRIP_H) != nullptr;
    box->setColorDepth(16);
    boxOk = box->createSprite(BOX_W, BOX_H) != nullptr;
    pt = (Port*)calloc(1, sizeof(Port));
    tft.fillScreen(P_BG);
    drawHud();
    shownFoot = -1;
    shownTime[0] = '\0';
    washOn = false;
    wxDrawn = false;
    if (!pt) return;

    Port& r = *pt;
    int x = -3;
    while (x < RT_X && r.farN < FARN) {  // the far skyline
      FarT& f = r.farT[r.farN];
      uint32_t h = hsh(r.farN * 7 + 11);
      f.x = x;
      f.w = 6 + h % 7;
      f.h = 34 + (h >> 4) % 44;
      f.type = (h >> 10) % 4;
      f.sp = f.type == 1 ? 8 + (h >> 13) % 10 : 0;
      if (f.type == 1 && r.beaconN < BEACONS) r.beacon[r.beaconN++] = {(int16_t)(f.x + f.w / 2), (int16_t)(farTop(f) - f.sp - 6)};
      x += f.w + 1 + (h >> 16) % 5;
      r.farN++;
    }
    static const int16_t NEAR[4][2] = {{34, 49}, {93, 59}, {181, 101}, {23, 133}};
    for (const auto& b : NEAR)
      if (r.beaconN < BEACONS) r.beacon[r.beaconN++] = {b[0], b[1]};
    for (int k = 0; k < RINGN; k++) {
      r.rc[k] = (int8_t)lroundf(cosf(k * 2 * PIF / RINGN) * 127);
      r.rs[k] = (int8_t)lroundf(sinf(k * 2 * PIF / RINGN) * 127);
    }
    for (int i = 0; i < CLOUDS; i++) r.cloudX[i] = hsh(i * 9 + 3) % 400;
    r.kind = CNW_CLOUD;
    r.d = 256;
    r.windDir = -1;
    r.mqW = 6;
    r.dishW = 4;
    strcpy(r.temp, "--");
    setLight(true);
    uint32_t ms = millis();
    nextFlash = ms + 2000;
    stepScene(ms, msIntoMin(ms), true);
    lastCars -= 50;  // spread the 100 ms updates over alternate ticks
    lastDish -= 50;
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
    free(pt);
    pt = nullptr;
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
    bool wxChanged = weatherChanged(s, prev) || s.code != prev.code || strcmp(s.city, prev.city) != 0;
    if (!pt) {
      if (wxChanged) drawWeather(s);
      return;
    }
    Port& r = *pt;
    bool wasNight = r.night;
    if (wxChanged) {
      r.kind = s.weatherValid ? cnWeather(s.code) : CNW_CLOUD;
      r.windKmh = s.weatherValid ? s.windKmh : 0;
      r.windDir = s.weatherValid ? s.windDir : -1;
      snprintf(r.wind, sizeof(r.wind), "%s", strncmp(s.wind, "WIND ", 5) == 0 ? s.wind + 5 : s.wind);
      if (s.weatherValid) {
        snprintf(r.temp, sizeof(r.temp), "%d~", s.temp);
        snprintf(r.label, sizeof(r.label), "%s", cnWeatherLabel(cnWeather(s.code)));
        snprintf(r.marquee, sizeof(r.marquee), "%s // %d~%c %s // WIND %s // HI %d LO %d // ", s.city, s.temp, s.unit,
                 r.label, r.wind, s.hi, s.lo);
      } else {
        strcpy(r.temp, "--");
        strcpy(r.label, "OFFLINE");
        snprintf(r.marquee, sizeof(r.marquee), "%s // NO WEATHER SIGNAL // ", s.city);
      }
      r.mqW = max(6, (int)strlen(r.marquee) * 6);
      r.mq %= r.mqW;
    }
    if (minute) sunLight();
    if (wxChanged || minute) setLight(wxChanged);
    if (wxChanged || r.night != wasNight || !wxDrawn) drawWeather(s);
    wxDrawn = true;
  }

  void tick(uint32_t ms) override {
    if (!pt || !bandOk) return;
    int mim = msIntoMin(ms);
    stepScene(ms, mim, false);
    flush();
    for (int i = 0; i < pt->qn; i++) paintArea(pt->q[i]);
    pt->qn = 0;
    drawFoot(mim, ms);
  }

 private:
  TFT_eSprite* band = nullptr;
  TFT_eSprite* box = nullptr;
  bool bandOk = false, boxOk = false, wxDrawn = false;
  bool dirty[STRIPS] = {};
  uint16_t flushed = 0;  // strips repainted this tick
  char shownTime[9] = "";
  int shownFoot = -1, lightSig = -1, lastTaxiSig = -1, lastDroneSig = -1, lastSweep = -1;
  bool washOn = false;
  int16_t lastFig[PEOPLE] = {};
  uint32_t lastMove = 0, lastSock = 0, lastRing = 0, lastAd = 0, lastMq = 0, lastDish = 0, lastCars = 0, lastPads = 0;
  uint32_t lastBeacon = 0, lastSky = 0, lastStar = 0, lastPrecip = 0, lastRip = 0, lastSteam = 0, lastSign = 0;
  uint32_t nextFlash = 0, flashEnd = 0;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // Where the sun is over your city: daylight, how close it is to the horizon, and which side of noon.
  void sunLight() {
    Port& r = *pt;
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

  // Every colour in the scene from the weather, the wind and the light; repaints all when they change.
  void setLight(bool force) {
    Port& r = *pt;
    int k = r.kind;
    r.lit = 256 - r.d;
    r.sgn = constrain((r.lit - 77) * 256 / 102, 0, 256);  // neon from dusk to dawn
    r.litN = r.lit * 110 >> 8;
    r.storm = k == CNW_THUNDER;
    r.wet = k == CNW_RAIN || r.storm;
    r.snow = k == CNW_SNOW;
    r.fog = k == CNW_FOG;
    r.heavy = r.wet || r.snow || k == CNW_OVERCAST;
    r.dim = k == CNW_SUNNY ? 0 : k == CNW_CLOUD ? 90 : k == CNW_OVERCAST ? 128 : k == CNW_FOG ? 115 : 141;
    r.sa = k == CNW_SUNNY ? 256 : k == CNW_CLOUD ? 102 : 0;
    r.hold = r.storm || r.windKmh >= HOLD_KMH;
    if (r.windDir >= 0) {
      float to = (r.windDir + 180) * DEG;
      r.ex = sinf(to);
      r.en = cosf(to);
      r.wf = min(256, r.windKmh * 256 / HOLD_KMH);
    } else {
      r.ex = r.en = 0;
      r.wf = 0;
    }
    r.sl = constrain((int)lroundf(r.ex * (1 + 4 * r.wf / 256.0f)), -5, 5);
    int sig = r.d * 7 + r.tw * 3 + (r.dawn ? 1 : 0) + k * 10000 + r.hold * 100000;
    if (!force && sig == lightSig) return;
    lightSig = sig;
    int d = r.d, tw = r.tw;
    r.grey = lerpRgb(lerpRgb({12, 14, 24}, r.storm ? Rgb{80, 84, 100} : Rgb{150, 156, 168}, d), {90, 70, 100}, tw / 2);
    r.fogC = lerpRgb(lerpRgb({50, 54, 70}, {206, 210, 218}, d), {180, 150, 170}, tw / 2);
    for (int y = 0; y < BASE - SCENE_Y0; y++) {
      int f = y * 256 / (BASE - SCENE_Y0);
      Rgb nt = lerpRgb({4, 6, 22}, {46, 22, 76}, f * f >> 8), dy = lerpRgb({56, 128, 214}, {186, 220, 246}, f);  // city glow at night
      Rgb du = f < 128 ? lerpRgb({26, 24, 78}, {172, 78, 128}, f * 2) : lerpRgb({172, 78, 128}, {255, 172, 96}, (f - 128) * 2);
      Rgb da = f < 128 ? lerpRgb({40, 56, 120}, {200, 140, 172}, f * 2) : lerpRgb({200, 140, 172}, {255, 214, 160}, (f - 128) * 2);
      r.sky[y] = c565(lerpRgb(K(nt, dy, du, da), r.grey, r.dim));
    }
    r.starA = ((256 - d) * (256 - (tw * 218 >> 8)) >> 8) * (k == CNW_SUNNY ? 256 : k == CNW_CLOUD ? 90 : 0) >> 8;
    r.moon = k == CNW_SUNNY && d < 64 && tw < 64;
    r.daySunA = (d * max(0, 256 - tw * 2) >> 8) * r.sa >> 8;
    r.lowSunA = tw * r.sa >> 8;
    r.clouds = k == CNW_SUNNY ? 3 : k == CNW_CLOUD ? 9 : k == CNW_FOG ? 0 : k == CNW_SNOW ? 8 : 12;
    r.cloudTop = K({26, 30, 52}, {250, 250, 255}, {255, 160, 170}, {255, 206, 196});
    r.cloudUnd = K({34, 36, 60}, {214, 222, 236}, {255, 196, 120}, {255, 232, 196});
    if (r.heavy) {
      r.cloudTop = lerpRgb(r.cloudTop, r.grey, 154);
      r.cloudUnd = lerpRgb(r.cloudUnd, r.grey, 154);
    }
    Rgb far = lerpRgb(K({24, 26, 58}, {150, 168, 198}, {94, 62, 112}, {178, 164, 196}), r.grey, r.dim / 2);
    r.far = c565(far);
    r.farRim = c565(lerpRgb(far, K({70, 130, 190}, {230, 238, 248}, {255, 170, 190}), 110));
    Rgb mid = lerpRgb(K({12, 16, 34}, {92, 110, 140}, {48, 32, 70}, {120, 112, 150}), r.grey, r.dim / 4);
    r.midRgb = mid;
    r.mid = c565(mid);
    r.midDk = c565(lerpRgb(mid, BLACK, 70));
    r.midRim = c565(lerpRgb(mid, K({60, 170, 220}, {220, 232, 246}, {255, 150, 170}), 120));
    Rgb glass = K({18, 24, 44}, {170, 196, 224}, {130, 96, 140}, {190, 176, 200});
    r.glass = c565(lerpRgb(glass, r.grey, r.dim / 3));
    int lit = min(256, r.lit * 3 / 2);
    r.winA = c565(lerpRgb(glass, {255, 196, 120}, lit));
    r.winC = c565(lerpRgb(glass, {130, 232, 255}, lit));
    r.winM = c565(lerpRgb(glass, {255, 120, 220}, lit));
    r.nCy = c565(lerpRgb(lerpRgb(mid, CY, 110), CY, r.sgn));
    r.nMg = c565(lerpRgb(lerpRgb(mid, MG, 110), MG, r.sgn));
    r.nAm = c565(lerpRgb(lerpRgb(mid, AM, 110), AM, r.sgn));
    r.ringC = c565(lerpRgb(lerpRgb(mid, CY, 80), {60, 160, 200}, r.sgn));
    r.ringHot = c565(lerpRgb(lerpRgb(mid, CY, 170), {200, 250, 255}, r.sgn));
    r.carC = c565(K({20, 22, 34}, {40, 44, 56}, {30, 26, 40}));
    Rgb rt = K({14, 18, 38}, {78, 92, 122}, {40, 28, 62}, {110, 100, 132});
    r.rt = c565(rt);
    r.rtDk = c565(lerpRgb(rt, BLACK, 70));
    r.rtGlass = c565(K({26, 44, 86}, {176, 208, 236}, {200, 120, 160}, {220, 190, 200}));
    r.bbBg = rgb(16, 6, 26);
    r.door = d < 128 ? c565(lerpRgb(K({10, 10, 14}, {60, 64, 74}, {40, 30, 50}), {255, 214, 150}, 204 * (256 - d * 2) >> 8))
                     : c565(K({10, 10, 14}, {60, 64, 74}, {40, 30, 50}));
    Rgb deck = K({30, 34, 50}, {150, 154, 164}, {86, 70, 98}, {170, 160, 162});
    r.deck = c565(deck);
    r.deckHi = c565(lerpRgb(deck, WHITE, 40));
    r.deckWet = c565(lerpRgb(deck, BLACK, 60));
    r.deckSnow = c565(K({120, 128, 152}, {236, 240, 248}, {210, 188, 210}, {232, 222, 228}));
    r.pad = c565(lerpRgb(deck, BLACK, r.wet ? 110 : 70));
    r.padHeat = c565(lerpRgb(deck, BLACK, 110));
    r.padRing = c565(K({150, 160, 182}, {246, 246, 250}, {222, 202, 214}));
    r.fascia = c565(lerpRgb(deck, BLACK, 120));
    r.truss = c565(K({6, 8, 14}, {40, 46, 58}, {20, 16, 28}));
    r.trussLine = c565(K({28, 34, 50}, {92, 100, 114}, {52, 42, 62}));
    r.cab = c565(K({18, 24, 40}, {120, 128, 142}, {60, 50, 76}));
    r.cabGlass = c565(K({40, 130, 160}, {70, 110, 140}, {110, 100, 150}));
    Rgb steel = K({52, 58, 74}, {150, 156, 168}, {100, 88, 112});
    r.steel = c565(steel);
    r.steelHi = c565(lerpRgb(steel, WHITE, 70));
    Rgb hull = K({178, 190, 210}, {238, 242, 248}, {226, 200, 214}, {246, 228, 220});
    r.hull = c565(hull);
    r.hullSh = c565(lerpRgb(hull, BLACK, 64));
    r.canopy = rgb(14, 30, 46);
    r.doorLit = rgb(255, 214, 140);
    r.droneC = c565(K({36, 40, 52}, {64, 70, 84}, {48, 44, 60}));
    r.cargo = c565(K({120, 70, 30}, {240, 140, 60}, {220, 120, 70}));
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
    if (pt->qn < QMAX) pt->q[pt->qn++] = mk(x, y, w, h);
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

  // Where the aircraft, the passengers and the sweeper are at s seconds into the minute.
  void place(float s, float t) {
    Port& r = *pt;
    Craft& T = r.taxi;
    T = {(float)PAX, (float)PADY, 0, 0, true, false, false};
    Craft& D = r.drone;
    D = {(float)PBX, (float)PADY, 0, 0, true, false, false};
    for (bool& on : r.figOn) on = false;
    if (!r.hold) {
      if (s < 4) {  // in from the left, flaring over the pad
        float u = 1 - (1 - s / 4) * (1 - s / 4);
        T.x = -30 + (PAX + 30) * u; T.y = 84 + 62 * u; T.tilt = 0.1f - 0.2f * u; T.rot = 1; T.air = true;
      } else if (s < 7) {
        float u = ease((s - 4) / 3);
        T.y = 146 + 30 * u; T.rot = 1; T.air = u < 0.98f; T.tilt = -0.1f * (1 - u);
      } else if (s < 9) T.rot = 1 - (s - 7) / 2;
      else if (s < 44) T.door = s > 10 && s < 43;
      else if (s < 48) T.rot = (s - 44) / 4;
      else if (s < 52) {
        float u = ease((s - 48) / 4);
        T.y = PADY - 30 * u; T.rot = 1; T.air = true;
      } else if (s < 58) {  // off to the right, nose down
        float u = (s - 52) / 6;
        u *= u;
        T.x = PAX + 222 * u; T.y = 146 - 80 * u; T.tilt = 0.18f * sat((s - 52) / 1.5f); T.rot = 1; T.air = true;
      } else T.vis = false;

      if (s >= 18 && s < 20) D.rot = (s - 18) / 2;
      else if (s >= 20 && s < 23) {
        float u = ease((s - 20) / 3);
        D.y = PADY - 36 * u; D.rot = 1; D.air = true;
      } else if (s >= 23 && s < 30) {
        float u = powf((s - 23) / 7, 1.6f);
        D.x = PBX + 140 * u; D.y = 140 - 90 * u; D.tilt = 0.25f; D.rot = 1; D.air = true;
      } else if (s >= 30 && s < 34) D.vis = false;
      else if (s >= 34 && s < 40) {
        float u = 1 - (1 - (s - 34) / 6) * (1 - (s - 34) / 6);
        D.x = 276 - 140 * u; D.y = 50 + 90 * u; D.tilt = -0.2f * (1 - u); D.rot = 1; D.air = true;
      } else if (s >= 40 && s < 43) {
        float u = ease((s - 40) / 3);
        D.y = 140 + 36 * u; D.rot = 1; D.air = true;
      } else if (s >= 43 && s < 45) D.rot = 1 - (s - 43) / 2;

      for (int i = 0; i < 2; i++) {  // off the taxi and into the tower
        float u = (s - 10 - i * 2.5f) / 13;
        r.figOn[i] = u > 0 && u < 1;
        r.figX[i] = (int16_t)lroundf(PAX + 6 + (RT_X - 2 - PAX - 6) * u);
      }
      for (int i = 0; i < 3; i++) {  // boarding
        float u = (s - 28 - i * 2.2f) / 12;
        r.figOn[2 + i] = u > 0 && u < 1;
        r.figX[2 + i] = (int16_t)lroundf(RT_X - 2 - (RT_X - 2 - PAX - 6) * u);
      }
    }
    float g = r.wf / 256.0f;  // gusts push the aircraft about and tip them into the wind
    if (T.air) {
      T.x += sinf(t * 2.1f) * g * 2.5f;
      T.y += sinf(t * 3.3f) * g;
      T.tilt -= r.ex * g * 0.14f;
    }
    if (D.air) {
      D.x += sinf(t * 2.7f) * g * 3;
      D.y += sinf(t * 3.9f) * g * 1.5f;
      D.tilt -= r.ex * g * 0.25f;
    }
    if (r.snow) {
      float u = fmodf(t, 24) / 24;
      r.sweepDir = u < 0.5f ? 1 : -1;
      r.sweepX = (int)lroundf(u < 0.5f ? 30 + u * 2 * 136 : 166 - (u - 0.5f) * 2 * 136);
    }
  }

  // Advance everything that moves and note what needs repainting.
  void stepScene(uint32_t ms, int mim, bool force) {
    Port& r = *pt;
    float s = mim / 1000.0f, t = ms % 600000 / 1000.0f;
    uint32_t el;
    r.s = s;

    if (r.flash && (int32_t)(ms - flashEnd) >= 0) {  // lightning strikes a far spire
      r.flash = false;
      nextFlash = ms + 2500 + random(4500);
      mark(SCENE_Y0, BASE);
    } else if (!r.flash && r.storm && (int32_t)(ms - nextFlash) >= 0) {
      r.flash = true;
      flashEnd = ms + 130;
      int n = 0, pick = random(8);
      r.boltX = 120;
      r.boltY = 100;
      for (int i = 0; i < r.farN; i++) {
        const FarT& f = r.farT[i];
        if (f.type != 1) continue;
        r.boltX = f.x + f.w / 2;
        r.boltY = farTop(f) - f.sp - 5;
        if (n++ >= pick) break;
      }
      r.bolt[0] = r.boltX + random(-20, 21);
      for (int i = 1; i < 6; i++) r.bolt[i] = r.bolt[i - 1] + random(-5, 6);
      mark(SCENE_Y0, BASE);
    }

    if (due(lastMove, ms, 50, force)) {  // aircraft, people, the sweeper
      r.spin = ms / 50;
      r.ripPh = (ms % 670) / 670.0f;
      place(s, t);
      const Craft& T = r.taxi;
      Area a = {0, 0, 0, 0};
      bool wash = T.vis && T.rot > 0.5f && T.y > 150;
      if (T.vis) a = xfBox(Xf(T.x, T.y, T.tilt), -20, -2, 21, 15, 2);
      if (wash || washOn) push(PAX - 40, 164, 80, 28);  // downwash ripples, and once more to clear them
      washOn = wash;
      int sig = (int)(T.x * 4) * 7 + (int)(T.y * 4) * 13 + (int)(T.tilt * 200) * 17 + T.vis + T.door * 2 +
                (T.rot > 0.25f ? (int)(r.spin & 3) * 4 + 16 : 0);
      if (sig != lastTaxiSig || force) {
        lastTaxiSig = sig;
        moved(r.taxiShown, a);
      }
      const Craft& D = r.drone;
      a = D.vis ? xfBox(Xf(D.x, D.y, D.tilt), -14, -1, 15, 9, 2) : Area{0, 0, 0, 0};
      sig = (int)(D.x * 4) * 7 + (int)(D.y * 4) * 13 + (int)(D.tilt * 200) * 17 + D.vis + (D.rot > 0.25f ? (int)(r.spin & 3) * 4 + 16 : 0);
      if (sig != lastDroneSig || force) {
        lastDroneSig = sig;
        moved(r.droneShown, a);
      }
      for (int i = 0; i < PEOPLE; i++) {
        int16_t key = r.figOn[i] ? r.figX[i] : -1;
        if (key == lastFig[i] && !force) continue;
        lastFig[i] = key;
        moved(r.figShown[i], r.figOn[i] ? mk(r.figX[i] - 3, WALK_Y - 13, 8, 14) : Area{0, 0, 0, 0});
      }
      int sw = r.snow ? r.sweepX * 2 + (r.sweepDir > 0) : -1;
      if (sw != lastSweep || force) {
        lastSweep = sw;
        moved(r.sweepShown, r.snow ? mk(r.sweepX - 16, 174, 33, 12) : Area{0, 0, 0, 0});
      }
    }
    if (due(lastSock, ms, 120, force)) {
      float g = r.wf / 256.0f, len = 9 + 13 * g, droop = (1 - min(1.0f, r.windKmh / 30.0f)) * 1.25f, lh = len * cosf(droop);
      float fl = sinf(t * (4 + 8 * g)) * (0.4f + g);
      r.sockDX = r.ex * lh;
      r.sockDY = len * sinf(droop) - r.en * lh * 0.3f + fl * 0.5f;
      push(MAST_X - 24, 135, 49, 30);
    }
    if (due(lastRing, ms, 120, force)) {
      r.ringPh = (r.ringPh + 1) % RINGN;
      push(74, 87, 38, 9);
      push(79, 114, 28, 7);
      if (r.starA > 12)
        for (int i = 0; i < TWINKLE; i++)
          if ((r.ringPh / 3 + i * 3) % 4 <= 1 && r.ringPh % 3 == 0) push(starX(i), starY(i), 1, 1);
    }
    if (due(lastAd, ms, 600, force)) {
      r.adPh++;
      push(147, 106, 11, 12);
    }
    if (due(lastMq, ms, 70, force)) {
      r.mq = (r.mq + 1) % r.mqW;
      push(191, 73, 46, 8);
    }
    if (due(lastDish, ms, 100, force)) {
      r.dishW = 1 + (int)lroundf(5 * fabsf(cosf(t * 1.5f)));
      push(9, 127, 10, 2);
    }
    if (due(lastCars, ms, 100, force)) {
      for (int l = 0; l < 2; l++)
        for (int i = 0; i < CARS; i++) {
          int x = imod(i * 53 + (int)(t * (l ? -26 : 18)) + l * 30, 262) - 12, o = r.car[l][i], y = l ? 106 : 101;
          if (x == o && !force) continue;
          r.car[l][i] = (int16_t)x;
          push(min(x, o) - 1, y - 1, abs(x - o) + 8, 4);
        }
    }
    if (due(lastPads, ms, 80, force)) {
      int mA = r.hold ? 3 : (s >= 56 || s < 7) ? 1 : s < 48 ? 2 : 0;
      int mB = r.hold ? 3 : (s < 20 || s >= 42) ? 2 : s >= 34 ? 1 : 0;
      bool blink = (ms / 500) & 1;
      bool pa = force || mA != r.modeA || mA == 1 || blink != r.blinkOn;
      bool pb = force || mB != r.modeB || mB == 1 || blink != r.blinkOn;
      r.modeA = mA;
      r.modeB = mB;
      r.chase = (ms / 80) % 14;
      if (blink != r.blinkOn) {  // the nav lights blink with the pads
        r.blinkOn = blink;
        push(r.taxiShown);
        push(r.droneShown);
        push(r.sweepShown);
      }
      if (pa && !washOn) push(PAX - 37, PADY - 9, 75, 24);  // the downwash repaint covers the pad
      if (pb) push(PBX - 25, PADY - 7, 51, 20);
    }
    if (due(lastBeacon, ms, 100, force)) {
      for (int i = 0; i < r.beaconN; i++) {
        bool on = ((ms + i * 233) / 700) & 1;
        if (on == (bool)((r.beaconOn >> i) & 1) && !force) continue;
        r.beaconOn = (r.beaconOn & ~(1u << i)) | ((uint32_t)on << i);
        push(r.beacon[i].x, r.beacon[i].y, 1, 1);
      }
    }
    if (due(lastSky, ms, 100, force, &el)) {  // clouds drift with the wind
      float v = (r.windDir >= 0 ? r.ex : 0.4f) * (0.6f + 3 * r.wf / 256.0f);
      for (int i = 0; i < r.clouds; i++) {
        r.cloudX[i] += (1.5f + (hsh(i * 9 + 4) & 255) / 102.0f) * v * el / 1000;
        if (r.cloudX[i] >= 400) r.cloudX[i] -= 400;
        if (r.cloudX[i] < 0) r.cloudX[i] += 400;
        int x = (int)r.cloudX[i] - 80, o = r.cloudShown[i];
        if (x != o || force) {
          r.cloudShown[i] = x;
          push(min(x, o), cloudY(i) - 2, cloudW(i) + abs(x - o), cloudH(i) + 4);
        }
      }
    }
    if (r.wet && due(lastRip, ms, 150, force)) {  // raindrops landing on the deck
      for (int i = 0; i < RIPS; i++) {
        push(r.ripX[i], r.ripY[i], 2, 1);
        r.ripX[i] = (int16_t)random(0, RT_X - 2);
        r.ripY[i] = (int16_t)random(168, 186);
        push(r.ripX[i], r.ripY[i], 2, 1);
      }
    }
    if (r.snow && due(lastSteam, ms, 150, force)) {
      r.steamPh = fmodf(t / 2.2f, 1);
      push(PAX - 26, 157, 52, 18);
      push(PBX - 20, 157, 40, 18);
    }
    if (r.hold && due(lastSign, ms, 700, force)) {
      bool on = (ms / 700) % 3 != 0;
      if (on != r.signOn || force) {
        r.signOn = on;
        push(56, 117, 82, 23);
      }
    }
    if (due(lastPrecip, ms, r.snow ? 200 : 100, force)) stepPrecip(t);
  }

  void stepPrecip(float t) {
    Port& r = *pt;
    if (r.wet) {
      int n = r.storm ? PARTS : PARTS * 2 / 3, sl = r.sl;
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
      float drift = r.ex * (2 + 10 * r.wf / 256.0f);
      for (int i = 0; i < PARTS; i++) {
        int ox = r.dropX[i], oy = r.dropY[i];
        float sp = 14 + (hsh(i * 3) & 255) * 12 / 256.0f;
        int y = SCENE_Y0 + (int)fmodf((hsh(i * 3 + 1) % 164) + t * sp, 164);
        int x = imod((int)(hsh(i * 3 + 2) % 250) + (int)(t * drift) + (int)lroundf(sinf(t / 0.8f + i) * 3), 250) - 5;
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

  // HH:MM:SS in the chamfer digits; only the characters that changed are redrawn.
  void drawTime(const FaceState& s) {
    char t[9];
    if (s.time[0] == '-') strcpy(t, "--:--:--");
    else snprintf(t, sizeof(t), "%s:%02d", s.time, s.second);
    const int Y = 7;
    tft.startWrite();
    for (int i = 0, x = 6; i < 8; x += t[i] == ':' ? 7 : 18, i++) {
      if (shownTime[0] && t[i] == shownTime[i]) continue;
      if (t[i] == ':') {
        tft.fillRect(x + 1, Y + 5, 3, 3, P_TIME);
        tft.fillRect(x + 1, Y + 13, 3, 3, P_TIME);
        continue;
      }
      const char* g = CHAMFER[t[i] == '-' ? 10 : t[i] - '0'];
      for (int py = 0; py < 21; py++) {  // each pixel row as runs of lit and unlit
        int run = 0;
        bool on = false;
        for (int px = 0; px <= 15; px++) {
          bool lit = false;
          if (px < 15) {
            char c = pgm_read_byte(g + py / 3 * 5 + px / 3);
            int u = px % 3, v = py % 3;
            lit = c == '#' || (c == 'a' && u + v >= 2) || (c == 'b' && v >= u) || (c == 'c' && u >= v) ||
                  (c == 'd' && u + v <= 2);
          }
          if (px == 15 || lit != on) {
            if (run) tft.drawFastHLine(x + px - run, Y + py, run, on ? P_TIME : P_BG);
            on = lit;
            run = 0;
          }
          run++;
        }
      }
    }
    tft.endWrite();
    memcpy(shownTime, t, sizeof(t));
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
    tft.fillRect(212, 6, 23, 12, P_CYAN);  // 13 NOV, then the weekday boxed
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
    tft.drawRect(4, HUD_Y + 6, 40, 14, P_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(P_CYAN);
    tft.drawString("PAD A", 24, HUD_Y + 13, 1);
  }

  // Weather over the port: icon, temperature, label.
  void drawWeather(const FaceState& s) {
    tft.fillRect(130, 21, 106, 27, P_BG);
    tft.setTextDatum(MR_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(P_DIM);
      tft.drawString("SKY NO SIGNAL", 235, 44, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    const char* lab = cnWeatherLabel(w);
    tft.setTextColor(P_CYAN);
    tft.drawString(lab, 235, 44, 1);
    tft.setTextColor(P_DIM);
    tft.drawString("SKY", 230 - tft.textWidth(lab, 1), 44, 1);
    char buf[8], u[2] = {s.unit, '\0'};
    snprintf(buf, sizeof(buf), "%d", s.temp);
    int tw = tft.textWidth(buf, 2), x = 235 - tft.textWidth(u, 2) - 5 - tw;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(P_CYAN);
    tft.drawString(buf, x, 22, 2);
    tft.drawCircle(x + tw + 2, 25, 1, P_CYAN);
    tft.drawString(u, x + tw + 5, 22, 2);
    weatherIcon(w, x - 15, 28, pt && pt->night);
  }

  // The departure board: this minute's flight and where it stands.
  void drawFoot(int mim, uint32_t ms) {
    const Port& r = *pt;
    int sec = mim / 1000, mod = (int)(ms / 60000);
    if (timeSynced()) {
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      mod = t.tm_hour * 60 + t.tm_min;
    }
    if (sec >= 58) mod++;  // the next flight is inbound
    int phase = r.hold ? 0 : sec < 7 ? 1 : sec < 44 ? 2 : sec < 48 ? 3 : sec < 58 ? 4 : 5;
    int key = r.hold ? 1 + r.storm : 1000 + mod * 1000 + phase * 100 + (phase == 2 ? sec : 0);
    if (key == shownFoot) return;
    shownFoot = key;
    tft.fillRect(48, HUD_Y + 5, 190, 16, P_BG);
    tft.setTextDatum(ML_DATUM);
    if (r.hold) {
      tft.setTextColor(P_AMBER);
      tft.drawString("FLIGHTS HOLD", 49, HUD_Y + 13, 1);
      tft.setTextDatum(MR_DATUM);
      char buf[24];
      snprintf(buf, sizeof(buf), "%s", r.storm ? "STORM" : r.wind);
      tft.drawString(buf, 236, HUD_Y + 13, 1);
      return;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "AT-%02d %s", 10 + (mod * 7) % 90, DEST[(mod * 3) % 10]);
    tft.setTextColor(P_CYAN);
    tft.drawString(buf, 49, HUD_Y + 13, 1);
    static const char* const ST[6] = {"", "ARRIVING", "", "DOORS CLOSED", "DEPARTED", "INBOUND"};
    if (phase == 2) snprintf(buf, sizeof(buf), "DEP 0:%02d", 48 - sec);
    else snprintf(buf, sizeof(buf), "%s", ST[phase]);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(phase == 2 ? P_AMBER : phase >= 3 && phase <= 4 ? P_MAG : P_CYAN);
    tft.drawString(buf, 236, HUD_Y + 13, 1);
  }
};

static Skyport skyport;
Face* faceSkyport() { return &skyport; }
