#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "../config.h"
#include "../display.h"
#include "../weather.h"

// Maglev: the view from a train window. The city slides past in layers and follows the
// weather and the real day and night over your city. The train dives into a tunnel at the
// top of every minute and another train passes at :30.

namespace {

constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
struct Rgb { uint8_t r, g, b; };

// ---- palette (the scene's colours depend on the light, see setLight) ----
const uint16_t M_BG = rgb(7, 10, 16), M_TIME = rgb(214, 241, 255), M_AMBER = rgb(255, 170, 60);
const uint16_t M_CITY = rgb(79, 182, 214), M_DIM = rgb(47, 127, 153), M_LINE = rgb(27, 58, 74);
const uint16_t M_ONBOX = rgb(42, 18, 4), M_CYAN = rgb(60, 230, 255), M_TRACK = rgb(27, 74, 92);
// weather icon
const uint16_t M_SUN = rgb(255, 210, 58), M_CLOUD = rgb(168, 200, 232), M_CLOUDDK = rgb(100, 118, 154);
const uint16_t M_DROP = rgb(95, 180, 255);

// ---- layout ----
const int SCENE_Y0 = 50, HUD_Y = 214;
const int W0 = 12, W1 = 228, WW = W1 - W0, Y0 = 66, Y1 = 172;  // the window
const int GY0 = Y0 + 10;                                        // first row of glass below the blind
const int HOR = 146, GND_Y = HOR + 12, SKY_H = GND_Y - Y0;
const int STRIP_H = 16;
const int PARTS = 44, DRIPS = 12, STARS = 34, STEAM_H = 15;
const int TUNNEL_MS = 7000, PASS_MS = 30000, PASS_LEN = 2200;  // tunnel at :00, passing train at :30
const float PIF = 3.14159265f, DEG = PIF / 180;

const uint8_t CORNER[6] = {6, 4, 3, 2, 1, 1};          // glass corner inset per row
const uint8_t PANEL[9] = {9, 6, 5, 4, 3, 2, 1, 1, 1};  // window panel corner inset per row

// Cabin colours: brighter by day, dark again inside the tunnel.
struct Cab {
  uint16_t wall, surr, surrHi, blind, blindEdge, outline, sill, lower, seam, table, tableHi, leg, patch;
  uint8_t patchOn, reflA;
};

// Everything the scene reads, so every repaint agrees with what is on screen. Lives on the
// heap while this face is up.
struct Rail {
  uint8_t hill[256];  // hill height per column, repeating
  uint8_t sag[180];   // how far the overhead wire hangs between two pylons
  // light, of 256
  int d, tw, lit, sgn;  // daylight, twilight, lit windows, lit signs
  bool dawn, night, flash;
  uint8_t kind;  // CnWeather
  // colours outside
  uint16_t sky[SKY_H];
  uint16_t cHill, cFar, cFarDk, cFarWarm, cFarCool, cNeon[2], cBeacon;
  uint16_t cNear, cNearEdge, cNearDk, cNearRefl, cNearLit, cBoard, cBoardTxt, cSign[3][2], cSignGlow[3], cSignTxt[3];
  uint16_t cGround, cRail, cPool, cPole, cLampHead, cWire, cPylon;
  uint16_t cTrain, cTrainRoof, cTrainWin, cTrainWinHot, cRain, cDrip;
  Rgb cloudC, lowSunC;
  int cloudA, clouds, starA, sunA, lowSunA, lampA;
  bool moon;
  Cab cab;
  // this frame
  uint32_t fms;
  int mim;  // milliseconds into the minute
  int32_t scHill, scFar, scNear, scRail, scWire, scCloud;  // how far each layer has scrolled, px
  int16_t tunA, tunB;  // the tunnel covers columns [tunA, tunB)
  bool pass;
  int16_t passFront;
  uint8_t parts;  // rain streaks or snowflakes on the glass
  int16_t px[PARTS], py[PARTS];  // 1/16 px
  uint8_t pspd[PARTS];
  uint8_t dripX[DRIPS], dripY[DRIPS];
  int8_t steam[2][STEAM_H];
  char msg[128];  // LED ticker
  int16_t msgLen, tickOff;
  uint8_t markX;
  bool markOn;
};
Rail* rail = nullptr;

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

Rgb lerpRgb(Rgb a, Rgb b, int t) {  // t of 256
  return {(uint8_t)(a.r + (b.r - a.r) * t / 256), (uint8_t)(a.g + (b.g - a.g) * t / 256),
          (uint8_t)(a.b + (b.b - a.b) * t / 256)};
}
uint16_t c565(Rgb c) { return rgb(c.r, c.g, c.b); }
Rgb un565(uint16_t v) { return {(uint8_t)((v >> 8) & 0xF8), (uint8_t)((v >> 3) & 0xFC), (uint8_t)((v << 3) & 0xF8)}; }

// Everything the scene draws goes through this, into the strip sprite, so the same code
// paints the whole scene and repaints small areas. Light and glow are blended into the
// pixels already in the sprite.
struct Canvas {
  TFT_eSprite& g;
  int dx, dy;          // screen point (x, y) maps to (x + dx, y + dy)
  int x0, x1, y0, y1;  // the screen area being painted
  uint16_t* buf;
  bool hits(int x, int y, int w, int h) const { return y < y1 && y + h > y0 && x < x1 && x + w > x0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (hits(x, y, w, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void frame(int x, int y, int w, int h, uint16_t c) {
    if (hits(x, y, w, h)) g.drawRect(x + dx, y + dy, w, h, c);
  }
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
        tft.fillCircle(x, y, 5, M_TIME);
        tft.fillCircle(x + 3, y - 2, 4, M_BG);
        break;
      }
      tft.fillCircle(x, y, 4, M_SUN);
      for (int i = 0; i < 8; i++) {
        float an = i * PIF / 4;
        tft.drawLine(x + (int)lroundf(cosf(an) * 6), y + (int)lroundf(sinf(an) * 6),
                     x + (int)lroundf(cosf(an) * 8), y + (int)lroundf(sinf(an) * 8), M_SUN);
      }
      break;
    case CNW_CLOUD:
      tft.fillCircle(x - 3, y - 4, 4, night ? M_TIME : M_SUN);
      cloud(M_CLOUD);
      break;
    case CNW_OVERCAST:
      tft.fillCircle(x - 4, y - 4, 3, M_CLOUDDK);
      tft.fillCircle(x, y - 6, 3, M_CLOUDDK);
      cloud(M_CLOUD);
      break;
    case CNW_FOG:
      for (int i = 0; i < 3; i++) tft.fillRect(x - 7 + (i & 1) * 2, y + i * 4 - 4, 14, 1, M_CLOUD);
      break;
    case CNW_RAIN:
      cloud(M_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.drawLine(x + d, y + 6, x + d - 1, y + 9, M_DROP);
      break;
    case CNW_SNOW:
      cloud(M_CLOUD);
      for (int d = -3; d <= 5; d += 4) tft.fillRect(x + d, y + 6 + ((d + 3) & 4 ? 1 : 0), 2, 2, TFT_WHITE);
      break;
    case CNW_THUNDER:
      cloud(M_CLOUDDK);
      tft.fillTriangle(x + 2, y + 4, x - 2, y + 8, x + 1, y + 8, M_SUN);
      tft.fillTriangle(x + 1, y + 7, x + 3, y + 7, x - 1, y + 11, M_SUN);
      break;
  }
}

// ---- outside the window ----

void drawSky(Canvas& cv) {
  const Rail& r = *rail;
  for (int y = max(cv.y0, GY0); y < min(cv.y1, GND_Y); y++) cv.rect(W0, y, WW, 1, r.sky[y - Y0]);
  if (r.starA > 12) {
    uint32_t tw = r.fms / 500 * 37;
    for (int i = 0; i < STARS; i++) {
      int x = W0 + (int)((hsh(i * 5 + 1) & 0xffff) * WW >> 16), y = Y0 + (int)((hsh(i * 5 + 2) & 0xffff) * 52 >> 16);
      int a = r.starA * (77 + (int)(hsh(i * 5 + 3) & 127)) >> 8;
      if ((hsh(i + tw) & 3) == 0) a = a * 3 / 5;  // twinkle
      cv.mix(x, y, 1, {220, 230, 255}, a);
    }
  }
  if (r.moon) {
    cv.dot(186, 86, 6, rgb(242, 240, 224));
    cv.dot(189, 84, 5, r.sky[84 - Y0]);
  }
  if (r.lowSunA > 12) {  // the sun on the horizon at dusk and dawn
    cv.glow(150, HOR - 7, 22, r.lowSunC, 154 * r.lowSunA >> 8);
    cv.dot(150, HOR - 7, 7, c565(lerpRgb(un565(r.sky[HOR - 7 - Y0]), r.lowSunC, r.lowSunA)));
  }
  if (r.sunA > 12) {
    cv.glow(186, 84, 14, {255, 250, 222}, 141 * r.sunA >> 8);
    cv.dot(186, 84, 5, c565(lerpRgb(un565(r.sky[84 - Y0]), {255, 251, 228}, r.sunA)));
  }
  for (int i = 0; i < r.clouds; i++) {
    int w = 18 + (int)(hsh(i * 13 + 3) % 22), y = Y0 + 12 + (int)(hsh(i * 13 + 2) % 38);
    int x = W0 - 40 + imod((int)(hsh(i * 13 + 1) % 300) - (int)(r.scCloud % 300), 300);
    if (!cv.hits(x, y - 2, w, 9)) continue;
    uint16_t c = c565(lerpRgb(un565(r.sky[y + 3 - Y0]), r.cloudC, r.cloudA));
    cv.rect(x, y + 3, w, 4, c);
    cv.rect(x + 4, y, w - 10, 4, c);
    cv.rect(x + w * 3 / 10, y - 2, w * 35 / 100, 3, c);
  }
}

void drawHills(Canvas& cv) {
  const Rail& r = *rail;
  if (cv.y1 <= HOR - 36) return;
  for (int x = max(cv.x0, W0); x < min(cv.x1, W1); x++) {
    int h = r.hill[(x + r.scHill) & 255];
    cv.rect(x, HOR - h, 1, h + 12, r.cHill);
  }
}

// Distant skyline: towers in clusters, lit windows after dark, beacons on the tall ones.
void drawFar(Canvas& cv) {
  const Rail& r = *rail;
  if (cv.y1 <= HOR - 54) return;
  bool day = r.d > 12, lit = r.lit > 12;
  for (int32_t c = r.scFar / 14 - 1; c <= (r.scFar + WW) / 14 + 1; c++) {
    if ((hsh(c * 7 + 1) & 255) > 210) continue;
    int bx = W0 + (int)(c * 14 - r.scFar), w = 8 + (int)(hsh(c * 7 + 2) % 6);
    int ph = imod(c, 57), cl = ph < 29 ? ph * 9 : (57 - ph) * 9;
    int h = 8 + (cl * 42 * (int)(hsh(c * 7 + 3) & 255) >> 16);
    if (!cv.hits(bx, HOR - h - 2, w, h + 14)) continue;
    cv.rect(bx, HOR - h, w, h + 12, r.cFar);
    for (int wy = HOR - h + 2; wy < HOR - 1; wy += 4) {
      if (wy + 2 <= cv.y0 || wy >= cv.y1) continue;
      for (int k = 1; k < w - 2; k += 3) {
        int q = hsh(c * 131 + wy * 17 + k) & 255;
        if (q < 84 && lit) cv.rect(bx + k, wy, 1, 2, q < 51 ? r.cFarWarm : r.cFarCool);
        else if (q < 128 && day) cv.rect(bx + k, wy, 1, 2, r.cFarDk);
      }
    }
    if (h > 36 && ((r.fms / 700 + c) & 1)) cv.rect(bx + w / 2, HOR - h - 2, 1, 2, r.cBeacon);
    if (r.sgn > 12 && (hsh(c * 7 + 5) & 255) < 31) cv.rect(bx + 1, HOR - h + 3, w - 3, 2, r.cNeon[hsh(c * 7 + 6) & 1]);
  }
}

// Closer blocks; some carry a sign that is lit from dusk to dawn.
void drawNear(Canvas& cv) {
  const Rail& r = *rail;
  if (cv.y1 <= GND_Y - 74) return;
  bool day = r.d > 12, lit = r.lit > 12, sgn = r.sgn > 12;
  uint32_t fl = r.fms / 130;
  for (int32_t c = r.scNear / 52 - 1; c <= (r.scNear + WW) / 52 + 1; c++) {
    if ((hsh(c * 11 + 5) & 255) > 179) continue;
    int bx = W0 + (int)(c * 52 - r.scNear), w = 26 + (int)(hsh(c * 11 + 6) % 16), h = 30 + (int)(hsh(c * 11 + 7) % 44);
    int top = GND_Y - h;
    if (!cv.hits(bx, top, w, h)) continue;
    bool sign = hsh(c * 11 + 8) & 1;
    int col = hsh(c * 11 + 9) % 3;
    cv.rect(bx, top, w, h, r.cNear);
    cv.rect(bx, top, 1, h, r.cNearEdge);
    cv.rect(bx, top, w, 1, r.cNearEdge);
    if (sign && sgn) cv.rect(bx + 2, top + 4, 22, 13, r.cSignGlow[col]);
    for (int wy = top + 5; wy < HOR + 6; wy += 7) {
      if (wy + 3 <= cv.y0 || wy >= cv.y1) continue;
      for (int k = 3; k < w - 4; k += 6) {
        int q = hsh(c * 977 + wy * 31 + k) & 255;
        if (q < 41 && lit) cv.rect(bx + k, wy, 2, 3, r.cNearLit);
        else if (q < 192 && day) cv.rect(bx + k, wy, 2, 3, q > 168 ? r.cNearRefl : r.cNearDk);
      }
    }
    if (!sign) continue;
    bool on = hsh(fl + c * 77) % 11 != 0;  // the odd flicker
    cv.rect(bx + 4, top + 6, 18, 9, sgn ? r.cSign[col][on] : r.cBoard);
    uint16_t tc = sgn ? r.cSignTxt[col] : r.cBoardTxt;
    cv.rect(bx + 6, top + 8, 10, 1, tc);
    cv.rect(bx + 6, top + 11, 14, 1, tc);
  }
}

// Track bed, sleepers and the lamps beside the track.
void drawGround(Canvas& cv) {
  const Rail& r = *rail;
  bool lamps = r.lampA > 12;
  int first = -(int)(r.scRail % 96);
  if (cv.y1 > GND_Y) {
    cv.rect(W0, GND_Y, WW, Y1 - GND_Y, r.cGround);
    if (lamps)
      for (int x = first; x < WW + 20; x += 96) cv.rect(W0 + x - 12, HOR + 15, 20, 8, r.cPool);
    for (int x = -(int)(r.scRail % 12); x < WW; x += 12) cv.rect(W0 + x, HOR + 18, 8, 1, r.cRail);
  }
  for (int x = first; x < WW + 20; x += 96) {
    int lx = W0 + x;
    if (lamps) cv.glow(lx - 2, HOR - 5, 11, {255, 170, 70}, r.lampA);
    cv.rect(lx, HOR - 6, 1, 24, r.cPole);
    cv.rect(lx - 3, HOR - 7, 4, 1, r.cPole);
    cv.rect(lx - 3, HOR - 6, 2, 1, r.cLampHead);
  }
}

// Overhead wires and the pylons that whip past.
void drawWires(Canvas& cv) {
  const Rail& r = *rail;
  int ph = r.scWire % 360;
  if (cv.y0 < Y0 + 23) {
    for (int x = W0; x < W1;) {
      int s = r.sag[(x - W0 + ph) % 360 / 2], x2 = x + 2;
      while (x2 < W1 && r.sag[(x2 - W0 + ph) % 360 / 2] == s) x2 += 2;
      cv.rect(x, Y0 + 13 + s, x2 - x, 1, r.cWire);
      cv.rect(x, Y0 + 18 + s, x2 - x, 1, r.cWire);
      x = x2;
    }
  }
  for (int x = -ph; x < WW + 30; x += 360) {
    cv.rect(W0 + x, Y0, 7, Y1 - Y0, r.cPylon);
    cv.rect(W0 + x - 8, Y0 + 11, 24, 2, r.cPylon);
  }
}

// The other train: a blur of windows going the other way.
void drawPass(Canvas& cv) {
  const Rail& r = *rail;
  int front = r.passFront, a = max(front, W0), b = min(front + 900, W1);
  if (b <= a || !cv.hits(a, 100, b - a, 46)) return;
  cv.rect(a, 100, b - a, 46, r.cTrain);
  cv.rect(a, 100, b - a, 2, r.cTrainRoof);
  cv.rect(a, 139, b - a, 1, M_CYAN);
  int wa = max(front + 20, a);
  if (b > wa) cv.rect(wa, 108, b - wa, 16, r.cTrainWin);
  for (int x = front + 20; x < b; x += 22) {
    int xa = max(x, a), xb = min(x + 13, b);
    if (xb > xa) cv.rect(xa, 108, xb - xa, 16, r.cTrainWinHot);
    xa = max(x + 18, a), xb = min(x + 21, b);  // pillar between windows
    if (xb > xa) cv.rect(xa, 108, xb - xa, 16, r.cTrain);
  }
}

// Tunnel wall with ribs and orange lights streaking past.
void drawTunnel(Canvas& cv) {
  const Rail& r = *rail;
  int xa = r.tunA, xb = r.tunB;
  if (xb <= xa) return;
  auto seg = [&](int x, int y, int w, int h, uint16_t c) {
    int a = max(x, xa), b = min(x + w, xb);
    if (b > a) cv.rect(a, y, b - a, h, c);
  };
  seg(xa, Y0, xb - xa, Y1 - Y0, rgb(6, 6, 9));
  seg(xa, Y0 + 42, xb - xa, 1, rgb(21, 22, 28));
  seg(xa, Y0 + 46, xb - xa, 1, rgb(21, 22, 28));
  seg(xa, Y0 + 84, xb - xa, 2, rgb(21, 22, 28));
  for (int x = -(int)(r.scWire % 40); x < WW; x += 40) seg(W0 + x, Y0, 2, Y1 - Y0, rgb(16, 17, 22));
  for (int x = -(int)(r.scWire % 70); x < WW + 40; x += 70) {
    seg(W0 + x, Y0 + 16, 34, 7, rgb(60, 37, 16));
    seg(W0 + x, Y0 + 18, 14, 3, rgb(255, 180, 90));
  }
}

// Rain streaking sideways across the glass, drops running down it, or snow.
void drawPrecip(Canvas& cv, bool inTunnel) {
  const Rail& r = *rail;
  bool snow = r.kind == CNW_SNOW;
  if (!inTunnel) {
    for (int i = 0; i < r.parts; i++) {
      int x = r.px[i] >> 4, y = r.py[i] >> 4;
      if (snow) cv.rect(x, y, 3, 1, rgb(248, 250, 255));
      else cv.line(x, y, x + 8, y - 2, r.cRain);
    }
  }
  if (r.parts && !snow)
    for (int i = 0; i < DRIPS; i++) cv.rect(r.dripX[i], r.dripY[i], 1, 2, r.cDrip);
}

void drawGlass(Canvas& cv) {
  const Rail& r = *rail;
  bool inTunnel = r.tunB - r.tunA >= WW;
  if (!inTunnel) {
    drawSky(cv);
    drawHills(cv);
    drawFar(cv);
    drawNear(cv);
    drawGround(cv);
    drawWires(cv);
    if (r.pass) drawPass(cv);
  }
  drawTunnel(cv);
  drawPrecip(cv, inTunnel);
  for (int y = max(cv.y0, GY0); y < min(cv.y1, Y0 + 80); y++) {  // reflection on the glass
    if (y < Y0 + 60) cv.add(W0 + 10 + (Y0 + 60 - y) * 52 / 60, y, 4, {215, 235, 255}, r.cab.reflA);
    cv.add(W0 + 10 + (Y0 + 80 - y) * 74 / 80, y, 4, {215, 235, 255}, r.cab.reflA);
  }
}

// ---- the cabin ----

// Route display: a marker crosses the line once a minute, the tunnel is marked at the start.
void drawRoute(Canvas& cv) {
  if (!cv.hits(10, 185, 82, 24)) return;
  const Rail& r = *rail;
  cv.rect(10, 185, 82, 24, rgb(5, 8, 12));
  cv.frame(10, 185, 82, 24, M_LINE);
  if (cv.y0 < 197) {
    cv.g.setTextDatum(TL_DATUM);
    cv.g.setTextColor(M_DIM);
    cv.g.drawString("LINE 7", 15 + cv.dx, 188 + cv.dy, 1);
    cv.g.setTextDatum(TR_DATUM);
    cv.g.setTextColor(M_CITY);
    cv.g.drawString("EXP", 87 + cv.dx, 188 + cv.dy, 1);
  }
  cv.rect(16, 201, 70, 1, M_TRACK);
  cv.rect(16, 200, 8, 3, rgb(138, 84, 22));
  for (int k = 1; k <= 4; k++) cv.rect(15 + 70 * k / 4, 199, 2, 5, r.mim >= k * 15000 ? M_CITY : M_TRACK);
  cv.rect(r.markX - 2, 200, 5, 3, r.markOn ? TFT_WHITE : M_AMBER);
}

// Wall, LED ticker, window panel and blind, table with a steaming cup, route display.
void drawCabin(Canvas& cv) {
  const Rail& r = *rail;
  const Cab& c = r.cab;
  if (cv.y0 < Y0) {
    cv.rect(0, SCENE_Y0, 240, Y0 - SCENE_Y0, c.wall);
    if (cv.hits(W0, 52, WW, 9)) {
      cv.rect(W0, 52, WW, 9, TFT_BLACK);
      int i = r.tickOff / 6;
      for (int x = W0 + 1 - r.tickOff % 6; x < W1; x += 6, i++)
        cv.g.drawChar(x + cv.dx, 53 + cv.dy, r.msg[i % r.msgLen], M_AMBER, M_AMBER, 1);
      cv.rect(0, 52, W0, 9, c.wall);
      cv.rect(W1, 52, 240 - W1, 9, c.wall);
    }
    cv.rect(8, 63, 224, 3, c.surr);
    cv.rect(18, 63, 204, 1, c.surrHi);
  }
  if (cv.y1 > Y0 && cv.y0 < Y1 + 6) {
    cv.rect(0, Y0, 8, Y1 - Y0, c.wall);
    cv.rect(232, Y0, 8, Y1 - Y0, c.wall);
    cv.rect(8, Y0, 4, Y1 - Y0, c.surr);
    cv.rect(W1, Y0, 4, Y1 - Y0, c.surr);
    cv.rect(0, Y1, 240, 6, c.wall);
    cv.rect(8, Y1, 224, 3, c.surr);
    cv.rect(W0, Y0, WW, 9, c.blind);
    cv.rect(W0, Y0 + 9, WW, 1, c.blindEdge);
    cv.rect(116, GY0, 8, 2, rgb(85, 97, 111));
    cv.rect(W0 - 1, Y0 + 6, 1, Y1 - Y0 - 12, c.outline);
    cv.rect(W1, Y0 + 6, 1, Y1 - Y0 - 12, c.outline);
    cv.rect(W0 + 6, Y1, WW - 12, 1, c.outline);
  }
  cv.rect(W0 + 6, Y0 - 1, WW - 12, 1, c.outline);
  for (int k = 0; k < 9; k++) {  // rounded corners of the panel
    for (int s = 0; s < 2; s++) {
      int y = s ? 174 - k : 63 + k;
      cv.rect(8, y, PANEL[k], 1, c.wall);
      cv.rect(232 - PANEL[k], y, PANEL[k], 1, c.wall);
    }
  }
  for (int k = 0; k < 6; k++) {  // and of the glass
    int in = CORNER[k], a = in - 1, b = k ? max(in - 1, CORNER[k - 1] - 2) : in - 1;
    for (int s = 0; s < 2; s++) {
      int y = s ? Y1 - 1 - k : Y0 + k;
      cv.rect(W0, y, in, 1, c.surr);
      cv.rect(W1 - in, y, in, 1, c.surr);
      cv.rect(W0 + a, y, b - a + 1, 1, c.outline);
      cv.rect(W1 - 1 - b, y, b - a + 1, 1, c.outline);
    }
  }
  if (cv.y1 > 178) {
    cv.rect(0, 178, 240, 2, c.sill);
    cv.rect(0, 180, 240, HUD_Y - 180, c.lower);
    cv.rect(104, 180, 1, HUD_Y - 180, c.seam);
    for (int k = 0; k < 6; k++) cv.rect(126 - k * 2 / 3, 196 + k, 114 + k * 2 / 3, 1, c.table);
    if (c.patchOn)  // daylight falling on the table
      for (int k = 1; k < 6; k++) cv.rect(136 - k * 2, 196 + k, 64 + k * 2 / 3, 1, c.patch);
    cv.rect(126, 196, 114, 1, c.tableHi);
    cv.rect(180, 202, 5, 12, c.leg);
    cv.rect(204, 193, 24, 3, rgb(16, 20, 26));  // tablet
    cv.rect(204, 193, 24, 1, M_CYAN);
    cv.rect(160, 185, 10, 11, rgb(227, 231, 236));  // cup
    cv.rect(160, 189, 10, 4, rgb(180, 98, 44));
    cv.rect(159, 183, 12, 2, rgb(42, 46, 54));
    drawRoute(cv);
  }
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < STEAM_H; i++) cv.mix(163 + k * 4 + r.steam[k][i], 182 - i, 1, {220, 230, 240}, 90);
}

void drawAll(Canvas& cv) {
  if (cv.y1 > GY0 && cv.y0 < Y1 && cv.x1 > W0 && cv.x0 < W1) drawGlass(cv);
  drawCabin(cv);
}

}  // namespace

class Maglev : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band = new TFT_eSprite(&tft);  // allocated while this face is up, so other faces get the RAM back
    band->setColorDepth(16);
    bandOk = band->createSprite(240, STRIP_H) != nullptr;
    rail = (Rail*)calloc(1, sizeof(Rail));
    tft.fillScreen(M_BG);
    drawHud();
    shownCount = -1;
    shownSpeed = 0;
    wxDrawn = false;
    if (!rail) return;

    for (int i = 0; i < 256; i++) {
      float a = i * 2 * PIF / 256;
      rail->hill[i] = (uint8_t)(20 + 8 * sinf(a) + 5 * sinf(3 * a + 1) + 2 * sinf(7 * a));
    }
    for (int i = 0; i < 180; i++) rail->sag[i] = (uint8_t)lroundf(3 * sinf(PIF * i / 180));
    rail->kind = CNW_CLOUD;
    rail->d = 256;
    strcpy(rail->msg, " ");
    rail->msgLen = 1;
    initParts();
    setLight();
    lastFrame = lastSteam = lastDrip = millis();
    nextFlash = lastFrame + 2000;
    dirtyAll = true;
  }

  void end() override {
    band->deleteSprite();
    delete band;
    band = nullptr;
    bandOk = false;
    free(rail);
    rail = nullptr;
  }

  void update(const FaceState& s, const FaceState& prev) override {
    bool minute = strcmp(s.time, prev.time) != 0, cityChanged = strcmp(s.city, prev.city) != 0;
    if (minute || s.second != prev.second) drawTime(s);
    if (cityChanged) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 118, textWidth2);
      tft.fillRect(6, 32, 122, 16, M_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(M_CITY, M_BG);
      tft.drawString(city, 6, 32, 2);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate();
    bool wxChanged = weatherChanged(s, prev) || s.code != prev.code;
    if (!rail) {
      if (wxChanged) drawWeather(s);
      return;
    }
    bool wasNight = rail->night;
    if (wxChanged) {
      rail->kind = s.weatherValid ? cnWeather(s.code) : CNW_CLOUD;
      initParts();
    }
    if (minute) sunLight();
    if (wxChanged || minute) setLight();
    if (wxChanged || cityChanged) buildMsg(s);
    if (wxChanged || rail->night != wasNight || !wxDrawn) drawWeather(s);
    wxDrawn = true;
  }

  void tick(uint32_t ms) override {
    if (!rail || !bandOk) return;
    int mim = msIntoMin(ms);
    step(ms, mim);
    if (dirtyAll) {
      for (int sy = SCENE_Y0; sy < HUD_Y; sy += STRIP_H) paint(0, sy, 240, min(STRIP_H, HUD_Y - sy));
    } else {
      for (int sy = GY0; sy < Y1; sy += STRIP_H) paint(W0, sy, WW, min(STRIP_H, Y1 - sy));
      if (tickDirty) paint(W0, 52, WW, 9);
      if (steamDirty) paint(160, 183 - STEAM_H, 12, STEAM_H);
      if (routeDirty) paint(14, 199, 74, 5);
    }
    dirtyAll = tickDirty = steamDirty = routeDirty = false;
    drawFoot(mim, ms);
  }

 private:
  TFT_eSprite* band = nullptr;
  bool bandOk = false, wxDrawn = false, dirtyAll = false, tickDirty = false, steamDirty = false, routeDirty = false;
  int shownCount = -1, shownSpeed = 0;
  uint32_t lastFrame = 0, lastSteam = 0, lastDrip = 0, nextFlash = 0, flashEnd = 0;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // Where the sun is over your city: daylight, how close it is to the horizon, and which side of noon.
  void sunLight() {
    Rail& r = *rail;
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

  // Colours of everything outside from the weather and the light.
  void setLight() {
    Rail& r = *rail;
    int d = r.d, tw = r.tw, lit = 256 - d, k = r.kind;
    r.lit = lit;
    r.sgn = constrain((lit - 77) * 256 / 102, 0, 256);
    bool clear = k == CNW_SUNNY, cloudy = k == CNW_CLOUD, fog = k == CNW_FOG;
    int dim = clear ? 0 : (cloudy ? 90 : 141), sa = clear ? 256 : (cloudy ? 115 : 0);
    Rgb grey = lerpRgb(lerpRgb({12, 13, 22}, k == CNW_THUNDER ? Rgb{78, 84, 98} : Rgb{150, 157, 168}, d), {66, 56, 78}, tw / 2);
    Rgb fogc = lerpRgb({112, 118, 134}, {205, 210, 216}, d);
    int foga = fog ? 102 + (38 * d >> 8) : 0, fade = fog ? 115 : 256;
    auto K = [&](Rgb n, Rgb dy, Rgb t) { return lerpRgb(lerpRgb(n, dy, d), t, tw * 205 >> 8); };
    auto haze = [&](Rgb c) { return c565(lerpRgb(c, fogc, foga)); };

    Rgb t0 = r.dawn ? Rgb{52, 70, 130} : Rgb{34, 36, 92}, t1 = r.dawn ? Rgb{190, 130, 150} : Rgb{150, 78, 128};
    Rgb t2 = r.dawn ? Rgb{255, 200, 150} : Rgb{255, 158, 72}, low = {0, 0, 0};
    for (int y = 0; y < SKY_H; y++) {
      int f = y * 256 / SKY_H;
      Rgb nt = f < 154 ? lerpRgb({4, 6, 18}, {26, 18, 52}, f * 256 / 154) : lerpRgb({26, 18, 52}, {74, 42, 82}, (f - 154) * 256 / 102);
      Rgb ts = f < 141 ? lerpRgb(t0, t1, f * 256 / 141) : lerpRgb(t1, t2, (f - 141) * 256 / 115);
      Rgb c = lerpRgb(lerpRgb(lerpRgb(nt, lerpRgb({58, 128, 210}, {176, 214, 240}, f), d), ts, tw * 230 >> 8), grey, dim);
      if (y == 70) low = c;
      c = lerpRgb(c, fogc, foga);
      if (r.flash) c = lerpRgb(c, {205, 215, 255}, 108);  // lightning
      r.sky[y] = c565(c);
    }
    r.starA = ((256 - d) * (256 - (tw * 205 >> 8)) >> 8) * (clear ? 256 : (cloudy ? 102 : 0)) >> 8;
    r.moon = clear && d < 38 && tw < 128;
    r.sunA = (d * (256 - tw) >> 8) * sa >> 8;
    r.lowSunA = tw * sa >> 8;
    r.lowSunC = r.dawn ? Rgb{255, 228, 194} : Rgb{255, 198, 122};
    r.clouds = clear ? (d > 26 || tw > 26 ? 4 : 0) : (cloudy ? 9 : 0);
    r.cloudC = K({44, 48, 72}, {246, 249, 253}, r.dawn ? Rgb{255, 196, 176} : Rgb{255, 152, 112});
    r.cloudA = 90 + (102 * max(d, tw) >> 8);
    r.cHill = haze(K({10, 10, 28}, {92, 124, 138}, {46, 30, 66}));

    Rgb far = lerpRgb(low, K({14, 14, 32}, {128, 148, 172}, {56, 38, 78}), fade);
    Rgb farDk = lerpRgb(far, {70, 92, 122}, (141 * d >> 8) * fade >> 8), base = d > 12 ? farDk : far;
    r.cFar = haze(far);
    r.cFarDk = haze(farDk);
    r.cFarWarm = haze(lerpRgb(base, {255, 200, 120}, (192 * lit >> 8) * fade >> 8));
    r.cFarCool = haze(lerpRgb(base, {140, 200, 255}, (179 * lit >> 8) * fade >> 8));
    r.cNeon[0] = haze(lerpRgb(far, {255, 60, 180}, r.sgn * fade >> 8));
    r.cNeon[1] = haze(lerpRgb(far, {60, 230, 255}, r.sgn * fade >> 8));
    r.cBeacon = haze(lerpRgb(far, {255, 48, 64}, (256 - d / 2) * fade >> 8));

    Rgb nearC = K({7, 7, 16}, {84, 94, 112}, {26, 20, 38}), board = K({12, 10, 22}, {56, 64, 78}, {30, 24, 40});
    Rgb nearDk = lerpRgb(nearC, {44, 60, 84}, 179 * d >> 8);
    r.cNear = c565(nearC);
    r.cNearEdge = c565(K({20, 20, 42}, {134, 146, 164}, {96, 62, 72}));
    r.cNearDk = c565(nearDk);
    r.cNearRefl = c565(lerpRgb(nearC, {170, 205, 235}, 154 * d >> 8));
    r.cNearLit = c565(lerpRgb(d > 12 ? nearDk : nearC, {255, 214, 150}, 205 * lit >> 8));
    r.cBoard = c565(board);
    r.cBoardTxt = c565(lerpRgb(board, lerpRgb({40, 40, 60}, {120, 130, 146}, d), 205));
    const Rgb sign[3] = {{255, 60, 180}, {60, 230, 255}, {255, 170, 60}};
    for (int i = 0; i < 3; i++) {
      Rgb on = lerpRgb(board, sign[i], 230 * r.sgn >> 8);
      r.cSign[i][1] = c565(on);
      r.cSign[i][0] = c565(lerpRgb(board, sign[i], 64 * r.sgn >> 8));
      r.cSignGlow[i] = c565(lerpRgb(nearC, sign[i], 41 * r.sgn >> 8));
      r.cSignTxt[i] = r.sgn > 128 ? c565(lerpRgb(on, {10, 6, 20}, 141)) : r.cBoardTxt;
    }

    Rgb ground = K({5, 5, 10}, {92, 94, 98}, {34, 26, 38});
    r.cGround = c565(ground);
    r.cRail = c565(K({42, 46, 68}, {56, 60, 68}, {72, 58, 72}));
    r.cPool = c565(lerpRgb(ground, {255, 170, 70}, 36 * lit >> 8));
    r.cPole = c565(lerpRgb({34, 38, 58}, {58, 64, 76}, d));
    r.cLampHead = lit > 77 ? rgb(255, 217, 160) : rgb(170, 178, 190);
    r.lampA = 141 * lit >> 8;
    r.cWire = c565(K({22, 26, 44}, {44, 50, 62}, {30, 24, 40}));
    r.cPylon = c565(lerpRgb({4, 4, 8}, {34, 38, 46}, d));

    Rgb train = K({22, 26, 36}, {196, 202, 210}, {96, 76, 96});
    Rgb win = lerpRgb(lerpRgb(train, {40, 52, 70}, d), {255, 236, 190}, 72 * lit >> 8);
    r.cTrain = c565(train);
    r.cTrainRoof = c565(K({35, 41, 54}, {228, 232, 238}, {130, 100, 110}));
    r.cTrainWin = c565(win);
    r.cTrainWinHot = c565(lerpRgb(win, {255, 236, 205}, lit));
    r.cRain = c565(lerpRgb({72, 88, 104}, {196, 208, 222}, d));
    r.cDrip = c565(lerpRgb({90, 110, 128}, {210, 225, 240}, d));
  }

  // Cabin colours for how much daylight gets in; tc is how much of the window the tunnel covers.
  void setCabin(int tc) {
    Rail& r = *rail;
    int lc = ((r.d * (256 - tc) >> 8) * 179 >> 8) & ~7;
    auto C = [&](Rgb n, Rgb dy) { return c565(lerpRgb(n, dy, lc)); };
    Cab c;
    memset(&c, 0, sizeof(c));
    c.wall = C({10, 14, 21}, {44, 54, 66});
    c.surr = C({28, 36, 48}, {92, 106, 122});
    c.surrHi = C({53, 66, 79}, {150, 164, 178});
    c.blind = C({42, 50, 62}, {140, 150, 160});
    c.blindEdge = C({58, 69, 82}, {172, 182, 192});
    c.outline = C({10, 14, 21}, {30, 36, 44});
    c.sill = C({35, 45, 58}, {84, 98, 114});
    c.lower = C({7, 10, 15}, {36, 44, 54});
    c.seam = C({16, 21, 29}, {52, 62, 74});
    Rgb table = lerpRgb({43, 52, 64}, {110, 122, 136}, lc);
    c.table = c565(table);
    c.tableHi = C({70, 82, 95}, {172, 184, 196});
    c.leg = C({20, 26, 34}, {50, 58, 68});
    int sa = r.kind == CNW_SUNNY ? 256 : (r.kind == CNW_CLOUD ? 102 : 0);
    int sun = ((sa * max(r.d, r.tw * 179 >> 8) >> 8) * (256 - tc) >> 8) & ~15;
    c.patchOn = sun > 12;
    c.patch = c565(lerpRgb(table, lerpRgb({255, 252, 236}, {255, 176, 96}, r.tw), 41 * sun >> 8));
    c.reflA = tc > 128 ? 23 : 13 - (lc >> 5);
    if (memcmp(&c, &r.cab, sizeof(c)) == 0) return;
    r.cab = c;
    dirtyAll = true;
  }

  void initParts() {
    Rail& r = *rail;
    r.parts = r.kind == CNW_SNOW ? 34 : (r.kind == CNW_THUNDER ? PARTS : (r.kind == CNW_RAIN ? 28 : 0));
    for (int i = 0; i < r.parts; i++) {
      r.px[i] = (int16_t)(random(W0 - 10, W0 + 240) * 16);
      r.py[i] = (int16_t)(random(GY0, Y1) * 16);
      r.pspd[i] = (uint8_t)random(256);
    }
    for (int i = 0; i < DRIPS; i++) {
      r.dripX[i] = (uint8_t)random(W0 + 2, W1 - 2);
      r.dripY[i] = (uint8_t)random(GY0, Y1 - 2);
    }
  }

  // LED ticker: NEXT STOP <city> /// 54F RAIN /// CAR 07
  void buildMsg(const FaceState& s) {
    Rail& r = *rail;
    char city[48];
    int n = 0;
    for (; s.city[n] && n < 47; n++) city[n] = (char)toupper((unsigned char)s.city[n]);
    city[n] = '\0';
    if (s.weatherValid)
      snprintf(r.msg, sizeof(r.msg), "NEXT STOP  %s   ///   %d%c %s   ///   CAR 07   ///   ", city, s.temp, s.unit,
               cnWeatherLabel(cnWeather(s.code)));
    else
      snprintf(r.msg, sizeof(r.msg), "NEXT STOP  %s   ///   CAR 07   ///   ", city);
    r.msgLen = (int16_t)strlen(r.msg);
  }

  // Advance everything for this frame and note what besides the window needs repainting.
  void step(uint32_t ms, int mim) {
    Rail& r = *rail;
    int dt = (int)min(ms - lastFrame, (uint32_t)250);
    lastFrame = ms;
    r.fms = ms;
    r.mim = mim;
    r.scHill = ms / 500;  // 2, 8, 40, 150 and 420 px a second
    r.scFar = ms / 125;
    r.scNear = ms / 25;
    r.scRail = ms / 20 * 3 + ms % 20 * 3 / 20;
    r.scWire = ms / 50 * 21 + ms % 50 * 21 / 50;
    r.scCloud = ms / 333;

    int tc = 0;
    r.tunA = r.tunB = W0;
    if (mim < TUNNEL_MS) {  // the tunnel mouth sweeps across in a quarter second, and out again
      r.tunA = mim < 250 ? W1 - WW * mim / 250 : W0;
      r.tunB = mim > TUNNEL_MS - 250 ? W1 - WW * (mim - (TUNNEL_MS - 250)) / 250 : W1;
      tc = (r.tunB - r.tunA) * 256 / WW;
    }
    r.pass = mim >= PASS_MS && mim < PASS_MS + PASS_LEN;
    r.passFront = (int16_t)(W1 - (mim - PASS_MS) * 640 / 1000);

    if (r.flash && (int32_t)(ms - flashEnd) >= 0) {
      r.flash = false;
      nextFlash = ms + 3000 + random(5000);
      setLight();
    } else if (!r.flash && r.kind == CNW_THUNDER && (int32_t)(ms - nextFlash) >= 0) {
      r.flash = true;
      flashEnd = ms + 120;
      setLight();
    }

    bool snow = r.kind == CNW_SNOW;
    for (int i = 0; i < r.parts; i++) {
      int sp = snow ? 90 + r.pspd[i] * 70 / 256 : 170 + r.pspd[i] * 110 / 256;  // px a second
      r.px[i] -= sp * dt * 16 / 1000;
      if (snow) {
        r.py[i] += 14 * dt * 16 / 1000;
        if (r.py[i] >= Y1 * 16) r.py[i] -= (Y1 - GY0) * 16;
      }
      if (r.px[i] < (W0 - 10) * 16) {
        r.px[i] += 250 * 16;
        if (!snow) r.py[i] = (int16_t)(random(GY0, Y1) * 16);
      }
    }
    if (ms - lastDrip >= 400) {
      lastDrip = ms;
      for (int i = 0; i < DRIPS; i++) {
        if (random(3)) continue;
        if (++r.dripY[i] >= Y1 - 2) {
          r.dripX[i] = (uint8_t)random(W0 + 2, W1 - 2);
          r.dripY[i] = GY0;
        }
      }
    }

    if (ms - lastSteam >= 150) {
      lastSteam = ms;
      float t = ms % 600000 / 1000.0f;
      for (int k = 0; k < 2; k++)
        for (int i = 0; i < STEAM_H; i++)
          r.steam[k][i] = (int8_t)lroundf(sinf(t * 2 + i * 0.3f + k * 2) * 1.8f * min(i, 4) / 4);
      steamDirty = true;
    }
    int off = (int)((ms / 1000 * 22 + ms % 1000 * 22 / 1000) % (uint32_t)(r.msgLen * 6));
    if (off != r.tickOff) {
      r.tickOff = (int16_t)off;
      tickDirty = true;
    }
    uint8_t mx = (uint8_t)(16 + 70 * mim / 60000);
    bool on = (ms / 300) & 1;
    if (mx != r.markX || on != r.markOn) {
      r.markX = mx;
      r.markOn = on;
      routeDirty = true;
    }
    setCabin(tc);
  }

  // Draw one area of the scene, at most a strip high, and push it to the screen.
  void paint(int x, int y, int w, int h) {
    Canvas cv{*band, 0, -y, x, x + w, y, y + h, (uint16_t*)band->getPointer()};
    drawAll(cv);
    band->pushSprite(x, y, x, 0, w, h);
  }

  void drawTime(const FaceState& s) {
    char sec[4] = "--";
    if (s.time[0] != '-') snprintf(sec, sizeof(sec), "%02d", s.second);
    int w = tft.textWidth("88:88", 4);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(M_TIME, M_BG);
    tft.setTextPadding(w);
    tft.drawString(s.time, 6, 5, 4);
    tft.setTextColor(M_AMBER, M_BG);
    tft.setTextPadding(tft.textWidth("88", 2));
    tft.drawString(sec, 10 + w, 13, 2);
    tft.setTextPadding(0);
  }

  void drawDate() {
    static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    tft.fillRect(140, 4, 96, 16, M_BG);
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, MONTHS[t.tm_mon]);
    tft.fillRect(212, 6, 23, 12, M_AMBER);  // 11 NOV, then the weekday boxed
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(M_ONBOX);
    tft.drawString(DAYS[t.tm_wday], 224, 12, 1);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(M_TIME);
    tft.drawString(buf, 207, 4, 2);
  }

  void drawHud() {
    tft.fillRect(0, HUD_Y, 240, 240 - HUD_Y, M_BG);
    tft.drawFastHLine(0, HUD_Y, 240, M_LINE);
    tft.drawRect(4, HUD_Y + 6, 42, 14, M_AMBER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(M_AMBER);
    tft.drawString("MAGLEV", 25, HUD_Y + 13, 1);
  }

  // Weather outside: icon, temperature, label.
  void drawWeather(const FaceState& s) {
    tft.fillRect(130, 21, 106, 28, M_BG);
    tft.setTextDatum(MR_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(M_DIM);
      tft.drawString("OUTSIDE NO SIGNAL", 235, 44, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    const char* lab = cnWeatherLabel(w);
    tft.setTextColor(M_AMBER);
    tft.drawString(lab, 235, 44, 1);
    tft.setTextColor(M_DIM);
    tft.drawString("OUTSIDE", 230 - tft.textWidth(lab, 1), 44, 1);
    char buf[8], u[2] = {s.unit, '\0'};
    snprintf(buf, sizeof(buf), "%d", s.temp);
    int tw = tft.textWidth(buf, 2), x = 235 - tft.textWidth(u, 2) - 5 - tw;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(M_AMBER);
    tft.drawString(buf, x, 22, 2);
    tft.drawCircle(x + tw + 2, 25, 1, M_AMBER);
    tft.drawString(u, x + tw + 5, 22, 2);
    weatherIcon(w, x - 15, 28, rail && rail->night);
  }

  // Speed, then TUNNEL 00:12 until the next tunnel, IN TUNNEL blinking inside it.
  void drawFoot(int mim, uint32_t ms) {
    int kmh = 431 + (int)lroundf(sinf(ms % 600000 / 2300.0f) * 3);
    if (kmh != shownSpeed) {
      shownSpeed = kmh;
      char buf[12];
      snprintf(buf, sizeof(buf), "%d KM/H", kmh);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(M_CITY, M_BG);
      tft.drawString(buf, 52, HUD_Y + 13, 1);
    }
    bool live = mim < TUNNEL_MS;
    int key = live ? 1000 + (ms / 400) % 2 : 60 - mim / 1000;
    if (key == shownCount) return;
    shownCount = key;
    char buf[16] = " ";
    if (!live) snprintf(buf, sizeof(buf), "TUNNEL 00:%02d", key);
    else if (key == 1001) strcpy(buf, "IN TUNNEL");
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(live ? M_AMBER : M_DIM, M_BG);
    tft.setTextPadding(tft.textWidth("TUNNEL 00:00", 1));
    tft.drawString(buf, 236, HUD_Y + 13, 1);
    tft.setTextPadding(0);
  }
};

static Maglev maglev;
Face* faceMaglev() { return &maglev; }
