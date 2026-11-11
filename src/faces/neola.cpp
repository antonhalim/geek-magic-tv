#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "../display.h"
#include "../weather.h"

// Neo LA: Los Angeles at night. Searchlights sweep the smog over downtown, freeway
// traffic streams past the palms, an ad blimp drifts by, a flying car crosses at :15
// and :45, and a taco truck serves the late crowd.

static constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
struct Rgb { uint8_t r, g, b; };

// ---- palette ----
static const uint16_t A_BG = rgb(6, 5, 12);
static const uint16_t A_TEAL = rgb(42, 209, 201);
static const uint16_t A_ORANGE = rgb(255, 138, 42);
static const uint16_t A_AMBER = rgb(255, 179, 90);
static const uint16_t A_CREAM = rgb(255, 233, 214);
static const uint16_t A_ON_ORANGE = rgb(42, 18, 6);
static const uint16_t A_LABEL = rgb(127, 232, 224);
static const uint16_t A_RED = rgb(255, 42, 58);
static const uint16_t A_HILL = rgb(22, 14, 28);
static const uint16_t A_TOWER = rgb(18, 18, 31), A_TOWER2 = rgb(21, 22, 41), A_TOWEREDGE = rgb(36, 41, 63);
static const uint16_t A_WINCOOL = rgb(126, 132, 149), A_WINWARM = rgb(148, 121, 91);
static const uint16_t A_DECK = rgb(24, 22, 36), A_DECKTOP = rgb(44, 40, 56), A_DECKLOW = rgb(12, 10, 20);
static const uint16_t A_PILLAR = rgb(16, 14, 24), A_POST = rgb(42, 38, 54);
static const uint16_t A_PALM = rgb(8, 6, 13);
static const uint16_t A_FACADE = rgb(13, 11, 23);
static const uint16_t A_MOTEL = rgb(255, 59, 74), A_VACANCY = rgb(58, 255, 138), A_VACANCYOFF = rgb(26, 90, 50);
static const uint16_t A_PINK = rgb(255, 106, 213);
static const uint16_t A_RAIN = rgb(90, 104, 128), A_SNOW = rgb(220, 226, 240);
// weather icon
static const uint16_t A_SUN = rgb(255, 210, 122), A_CLOUD = rgb(168, 200, 232), A_CLOUDDK = rgb(100, 118, 154);
static const uint16_t A_DROP = rgb(95, 180, 255), A_YELLOW = rgb(255, 210, 58);

// ---- layout and timing ----
static const int SCENE_Y0 = 50, HUD_Y = 214, STREET_Y = 176;
static const int STRIP_H = 12, STRIPS = (HUD_Y - SCENE_Y0 + STRIP_H - 1) / STRIP_H;
static const int BOX_W = 32, BOX_H = 36;
static const int DROPS = 16;
static const int NO_CAR = -9999;
static const int CAR_MS = 3000;  // flying car crosses from :15 (left to right) and :45 (right to left)
static const int BLIMP_Y = 84;
static const int PALMS = 4;      // three on the street, one on the billboard
static const float PIF = 3.14159265f;

// Scene state the drawing reads, so every repaint agrees with what is on screen.
static uint32_t sceneMs = 0;
static float beamA[2] = {-PIF / 2, -PIF / 2};
static int blimpX = 120, blimpScroll = 0;
static char blimpMsg[48] = " NEO LA // ";
static int carX = NO_CAR, carY = 118, carDir = 1;
static uint8_t reflSeed = 0, bulbSeed = 0, slide = 0;
static bool motelE = true, vacancyOn = true;
static bool mastOn = false, towerOn = false;
static int planeX[3], planeY[3];
static uint8_t precip = 0;  // 0 none, 1 rain, 2 snow
static bool fog = false, storm = false, cloudy = false;
static bool flashOn = false;
static uint32_t boltSeed = 1;
static float dropX[DROPS], dropY[DROPS];
static uint8_t wxKind = CNW_CLOUD;
static int wxTemp = 0;
static bool wxValid = false;
static const char* wxLabel = "";

// Shapes worked out once in begin(), freed in end().
struct Precomp {
  uint8_t hill[240];          // top of the hills per column
  int16_t trunk[PALMS][9][2];  // palm trunk polyline
  int16_t frond[PALMS][11][4]; // control point and tip of each frond
};
static Precomp* pre = nullptr;

static uint32_t hsh(uint32_t i) {
  i ^= 0x9e3779b9u;
  i *= 2654435761u;
  i ^= i >> 15;
  i *= 2246822519u;
  i ^= i >> 13;
  return i;
}
static float frac(uint32_t i) { return (hsh(i) & 0xFFFF) / 65536.0f; }

static int isqrtSmall(int n) {
  int r = 0;
  while ((r + 1) * (r + 1) <= n) r++;
  return r;
}

namespace {  // file-local: other faces have their own Canvas

// Everything the scene draws goes through this, into the strip or box sprite, so the
// same code paints the whole scene and repaints small areas. Light (beams, glows,
// reflections) is added to the pixels already in the sprite.
struct Canvas {
  TFT_eSprite& g;
  int dx, dy, y0, y1;  // screen point (x, y) maps to (x + dx, y + dy); rows outside [y0, y1) skipped
  uint16_t* buf;
  bool hits(int y, int h) const { return y < y1 && y + h > y0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (hits(y, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void px(int x, int y, uint16_t c) { rect(x, y, 1, 1, c); }
  void line(int xa, int ya, int xb, int yb, uint16_t c) {
    if (hits(min(ya, yb), abs(yb - ya) + 1)) g.drawLine(xa + dx, ya + dy, xb + dx, yb + dy, c);
  }
  void dot(int x, int y, int r, uint16_t c) {
    if (hits(y - r, 2 * r + 1)) g.fillCircle(x + dx, y + dy, r, c);
  }
  void ellipse(int x, int y, int rx, int ry, uint16_t c) {
    if (hits(y - ry, 2 * ry + 1)) g.fillEllipse(x + dx, y + dy, rx, ry, c);
  }
  void tri(int xa, int ya, int xb, int yb, int xc, int yc, uint16_t c) {
    int lo = min(ya, min(yb, yc)), hi = max(ya, max(yb, yc));
    if (hits(lo, hi - lo + 1)) g.fillTriangle(xa + dx, ya + dy, xb + dx, yb + dy, xc + dx, yc + dy, c);
  }
  void text(const char* s, int x, int y, int font, uint16_t c, uint8_t datum) {
    if (!hits(y - 16, 32)) return;
    g.setTextDatum(datum);
    g.setTextColor(c);
    g.drawString(s, x + dx, y + dy, font);
  }
  // Blend (r, g, b) into pixels [x, x + w) of row y: added as light, or mixed in with weight a/256.
  void blend(int x, int y, int w, Rgb c, int a, bool add) {
    if (a <= 0 || y < y0 || y >= y1) return;
    int sy = y + dy;
    if (sy < 0 || sy >= g.height()) return;
    int sx0 = max(0, x + dx), sx1 = min((int)g.width(), x + dx + w);
    uint16_t* row = buf + sy * g.width();
    for (int sx = sx0; sx < sx1; sx++) {
      uint16_t p = (uint16_t)((row[sx] >> 8) | (row[sx] << 8));
      int r = (p >> 8) & 0xF8, gg = (p >> 3) & 0xFC, b = (p << 3) & 0xF8;
      if (add) {
        r += c.r * a >> 8; gg += c.g * a >> 8; b += c.b * a >> 8;
      } else {
        r += (c.r - r) * a >> 8; gg += (c.g - gg) * a >> 8; b += (c.b - b) * a >> 8;
      }
      uint16_t q = rgb(min(r, 255), min(gg, 255), min(b, 255));
      row[sx] = (uint16_t)((q >> 8) | (q << 8));
    }
  }
  void add(int x, int y, int w, Rgb c, int a) { blend(x, y, w, c, a, true); }
  // Round glow fading out from the centre.
  void glow(int cx, int cy, int r, Rgb c, int a) {
    for (int y = cy - r; y <= cy + r; y++) {
      if (y < y0 || y >= y1) continue;
      for (int x = cx - r; x <= cx + r; x++) {
        int d = isqrtSmall((x - cx) * (x - cx) + (y - cy) * (y - cy));
        if (d < r) add(x, y, 1, c, a * (r - d) / r);
      }
    }
  }
};

}  // namespace

static Rgb lerpRgb(Rgb a, Rgb b, int t) {  // t of 256
  return {(uint8_t)(a.r + (b.r - a.r) * t / 256), (uint8_t)(a.g + (b.g - a.g) * t / 256),
          (uint8_t)(a.b + (b.b - a.b) * t / 256)};
}
static uint16_t c565(Rgb c) { return rgb(c.r, c.g, c.b); }

// Smoggy orange glow towards the horizon.
static Rgb skyAt(int y) {
  static const int16_t SY[5] = {50, 100, 141, 163, 176};
  static const Rgb SC[5] = {{6, 7, 15}, {25, 19, 48}, {85, 42, 58}, {140, 68, 48}, {168, 90, 46}};
  int i = 0;
  while (i < 3 && y > SY[i + 1]) i++;
  int t = constrain((y - SY[i]) * 256 / (SY[i + 1] - SY[i]), 0, 256);
  Rgb c = lerpRgb(SC[i], SC[i + 1], t);
  if (cloudy && y < 106) c = lerpRgb(c, {22, 14, 30}, 115);
  if (flashOn && y < 172) {
    c.r = min(255, c.r + 56); c.g = min(255, c.g + 59); c.b = min(255, c.b + 72);
  }
  return c;
}

static void buildPrecomp() {
  for (int x = 0; x < 240; x++) {
    float h = 134 - 9 * sinf(x * 0.03f + 1) - 5 * sinf(x * 0.071f) - (x < 70 ? 6 * sinf(x * 0.045f) : 0);
    pre->hill[x] = (uint8_t)h;
  }
  static const int16_t P[PALMS][4] = {{50, 196, 48, 66}, {62, 196, 61, 86}, {160, 196, 158, 74}, {214, 130, 212, 104}};
  static const float S[PALMS] = {1.05f, 0.85f, 1.0f, 0.7f};
  for (int p = 0; p < PALMS; p++) {
    float bx = P[p][0], by = P[p][1], tx = P[p][2], ty = P[p][3];
    float cx = bx + (tx - bx) * 0.15f, cy = (by + ty) / 2;
    for (int i = 0; i <= 8; i++) {
      float t = i / 8.0f, u = 1 - t;
      pre->trunk[p][i][0] = (int16_t)lroundf(u * u * bx + 2 * u * t * cx + t * t * tx);
      pre->trunk[p][i][1] = (int16_t)lroundf(u * u * by + 2 * u * t * cy + t * t * ty);
    }
    for (int i = 0; i < 11; i++) {
      float th = PIF + i * PIF / 10 + (frac(i + P[p][0] * 3) - 0.5f) * 0.2f;
      float L = (10 + frac(i + P[p][0]) * 5) * S[p];
      pre->frond[p][i][0] = (int16_t)lroundf(tx + cosf(th) * L * 0.5f);
      pre->frond[p][i][1] = (int16_t)lroundf(ty - L * 0.3f);
      pre->frond[p][i][2] = (int16_t)lroundf(tx + cosf(th) * L);
      pre->frond[p][i][3] = (int16_t)lroundf(ty + sinf(th) * L * 0.35f + L * 0.55f);
    }
  }
}

static void palm(Canvas& cv, int p) {
  for (int i = 0; i < 8; i++) {
    const int16_t* a = pre->trunk[p][i];
    const int16_t* b = pre->trunk[p][i + 1];
    cv.line(a[0], a[1], b[0], b[1], A_PALM);
    cv.line(a[0] + 1, a[1], b[0] + 1, b[1], A_PALM);
  }
  int tx = pre->trunk[p][8][0], ty = pre->trunk[p][8][1];
  for (int i = 0; i < 11; i++) {
    const int16_t* f = pre->frond[p][i];
    int lx = tx, ly = ty;
    for (int s = 1; s <= 4; s++) {  // quadratic curve from the crown through the control point to the tip
      int t = s * 64, u = 256 - t;
      int qx = (u * u * tx + 2 * u * t * f[0] + t * t * f[2]) >> 16;
      int qy = (u * u * ty + 2 * u * t * f[1] + t * t * f[3]) >> 16;
      cv.line(lx, ly, qx, qy, A_PALM);
      if (i % 3 == 0) cv.line(lx, ly + 1, qx, qy + 1, A_PALM);
      lx = qx;
      ly = qy;
    }
  }
  cv.dot(tx, ty + 1, 2, A_PALM);
}

static void weatherIcon(TFT_eSPI& g, int w, int x, int y, int k) {
  auto cloud = [&](uint16_t c) {
    g.fillCircle(x - 3 * k, y, 3 * k, c);
    g.fillCircle(x + k, y - 2 * k, 4 * k, c);
    g.fillCircle(x + 5 * k, y + k, 3 * k, c);
    g.fillRect(x - 3 * k, y + k, 9 * k, 3 * k, c);
  };
  switch (w) {
    case CNW_SUNNY:
      g.fillCircle(x, y, 4 * k, A_SUN);
      for (int i = 0; i < 8; i++) {
        float an = i * PIF / 4;
        g.drawLine(x + (int)lroundf(cosf(an) * 6 * k), y + (int)lroundf(sinf(an) * 6 * k),
                   x + (int)lroundf(cosf(an) * 8 * k), y + (int)lroundf(sinf(an) * 8 * k), A_SUN);
      }
      break;
    case CNW_CLOUD:
      g.fillCircle(x - 3 * k, y - 4 * k, 4 * k, A_SUN);
      cloud(A_CLOUD);
      break;
    case CNW_OVERCAST:
      g.fillCircle(x - 4 * k, y - 4 * k, 3 * k, A_CLOUDDK);
      g.fillCircle(x, y - 6 * k, 3 * k, A_CLOUDDK);
      cloud(A_CLOUD);
      break;
    case CNW_FOG:
      for (int i = 0; i < 3; i++) g.fillRect(x - 7 * k + (i & 1) * 2 * k, y + (i * 4 - 4) * k, 14 * k, k, A_CLOUD);
      break;
    case CNW_RAIN:
      cloud(A_CLOUD);
      for (int d = -3; d <= 5; d += 4)
        for (int j = 0; j < k; j++) g.drawLine(x + d * k + j, y + 6 * k, x + (d - 1) * k + j, y + 9 * k, A_DROP);
      break;
    case CNW_SNOW:
      cloud(A_CLOUD);
      for (int d = -3; d <= 5; d += 4) g.fillRect(x + d * k, y + (6 + ((d + 3) & 4 ? 1 : 0)) * k, 2 * k, 2 * k, TFT_WHITE);
      break;
    case CNW_THUNDER:
      cloud(A_CLOUDDK);
      g.fillTriangle(x + 2 * k, y + 4 * k, x - 2 * k, y + 8 * k, x + k, y + 8 * k, A_YELLOW);
      g.fillTriangle(x + k, y + 7 * k, x + 3 * k, y + 7 * k, x - k, y + 11 * k, A_YELLOW);
      break;
  }
}

// ---- scene ----

static void drawSky(Canvas& cv) {
  for (int y = max(cv.y0, SCENE_Y0); y < min(cv.y1, STREET_Y); y++) cv.rect(0, y, 240, 1, c565(skyAt(y)));
  if (cv.hits(104, STREET_Y - 104)) {
    for (int x = 0; x < 240; x++) cv.rect(x, pre->hill[x], 1, STREET_Y - pre->hill[x], A_HILL);
    for (int i = 0; i < 60; i++) {  // houses up in the hills
      uint32_t h = hsh(i + 1300);
      int x = h % 240, y = 138 + (h >> 9) % 20;
      if (y > pre->hill[x] + 1) cv.px(x, y, (h >> 20) % 10 < 7 ? rgb(160, 124, 84) : rgb(120, 124, 140));
    }
    const uint16_t mast = rgb(42, 32, 48);  // TV mast
    cv.rect(28, 104, 1, 24, mast);
    cv.line(26, 128, 28, 110, mast);
    cv.line(28, 110, 31, 128, mast);
    cv.px(28, 103, mastOn ? A_RED : mast);
    cv.px(28, 114, mastOn ? mast : A_RED);
  }
  if (flashOn) {  // lightning far off, behind the towers
    int x = 150 + boltSeed % 60, y = SCENE_Y0;
    for (int i = 0; y < 120; i++) {
      int nx = x + (int)(hsh(boltSeed + i) % 11) - 5;
      cv.line(x, y, nx, y + 6, rgb(235, 240, 255));
      x = nx;
      y += 6;
    }
  }
}

// Premiere searchlights from behind the towers, sweeping the smog.
static void drawBeams(Canvas& cv) {
  static const int OX[2] = {88, 142};
  const int oy = 160, a0 = fog ? 128 : 82;
  for (int k = 0; k < 2; k++) {
    float s = sinf(beamA[k]), c = cosf(beamA[k]);
    if (s > -0.2f) continue;
    for (int y = max(cv.y0, SCENE_Y0); y < min(cv.y1, oy); y++) {
      float dist = (oy - y) / -s;
      if (dist >= 200) continue;
      float xc = OX[k] + c * dist, hw = dist * 0.045f + 0.5f;
      int a = (int)(a0 * (1 - dist / 200));
      int xa = (int)lroundf(xc - hw), xb = (int)lroundf(xc + hw);
      cv.add(xa, y, xb - xa + 1, {220, 235, 255}, a);
    }
  }
}

static void drawPlanes(Canvas& cv) {  // landing lights on approach
  for (int i = 0; i < 3; i++) {
    cv.px(planeX[i], planeY[i], rgb(255, 250, 235));
    if ((sceneMs / 100 + i * 4) % 8 < 2) cv.px(planeX[i] + 2, planeY[i], A_RED);
  }
}

static void windows(Canvas& cv, int x0, int top, int w, int seed) {
  for (int y = top + 2; y < 169; y += 3) {
    if (!cv.hits(y, 1)) continue;
    for (int x = x0 + 1; x < x0 + w - 1; x += 2) {
      float r = frac((uint32_t)(seed * 7919 + y * 131 + x));
      if (r < 0.32f) cv.px(x, y, r < 0.19f ? A_WINCOOL : A_WINWARM);
    }
  }
}

static void drawDowntown(Canvas& cv) {
  static const int16_t T[8][3] = {{64, 104, 14}, {78, 88, 16}, {96, 66, 18}, {118, 80, 16},
                                  {136, 96, 14}, {150, 110, 12}, {56, 116, 10}, {166, 118, 10}};
  for (int i = 0; i < 8; i++) {
    int x0 = T[i][0], top = T[i][1], w = T[i][2];
    if (i == 3) {  // sloped roof with a lit edge and a spire
      for (int c = 0; c < w; c++) cv.rect(x0 + c, 81 - c * 7 / 16, 1, STREET_Y - 81 + c * 7 / 16, A_TOWER);
    } else {
      cv.rect(x0, top, w, STREET_Y - top, i == 2 ? A_TOWER2 : A_TOWER);
    }
    windows(cv, x0, top, w, i + 10);
    cv.rect(x0, top, 1, STREET_Y - top, A_TOWEREDGE);
  }
  cv.rect(99, 60, 12, 6, A_TOWER2);  // crown on the tallest tower
  cv.rect(101, 57, 8, 3, rgb(27, 29, 51));
  for (int x = 97; x < 114; x += 2) cv.px(x, 66, rgb(232, 240, 255));
  cv.rect(96, 67, 18, 1, rgb(95, 98, 116));
  for (int i = 0; i < 9; i++) cv.px(118 + i * 2, 81 - i * 7 / 8, rgb(87, 214, 255));
  cv.rect(130, 58, 1, 16, rgb(138, 143, 168));
  cv.px(130, 57, towerOn ? A_RED : A_TOWER);
  cv.px(79, 87, towerOn ? A_RED : A_TOWER);
  cv.px(92, 87, towerOn ? A_RED : A_TOWER);

  // elevated freeway
  cv.rect(0, 156, 240, 1, A_DECKTOP);
  cv.rect(0, 157, 240, 6, A_DECK);
  cv.rect(0, 163, 240, 1, A_DECKLOW);
  for (int x = 14; x < 240; x += 44) cv.rect(x, 164, 5, 14, A_PILLAR);
  for (int x = 8; x < 240; x += 32) {
    cv.rect(x, 146, 1, 10, A_POST);
    cv.rect(x, 146, 4, 1, A_POST);
  }
}

static void drawBlimp(Canvas& cv) {
  int bx = blimpX, by = BLIMP_Y;
  if (!cv.hits(by - 10, 22) || bx < -40 || bx > 280) return;
  cv.ellipse(bx, by, 31, 8, rgb(29, 28, 40));
  if (cv.hits(by - 8, 1)) cv.g.drawEllipse(bx + cv.dx, by + cv.dy, 31, 8, rgb(44, 45, 62));
  cv.tri(bx + 24, by - 2, bx + 35, by - 9, bx + 33, by - 1, rgb(20, 19, 29));
  cv.tri(bx + 24, by + 2, bx + 35, by + 9, bx + 33, by + 1, rgb(20, 19, 29));
  cv.rect(bx - 6, by + 7, 12, 3, rgb(14, 13, 22));
  cv.rect(bx - 23, by - 3, 42, 7, rgb(5, 4, 9));
  // scrolling LED band: whole characters only, inside the band
  if (cv.hits(by - 3, 8)) {
    int len = strlen(blimpMsg), w = len * 6, left = bx - 22, right = bx + 19;
    int off = blimpScroll % w;
    cv.g.setTextColor(A_AMBER);
    for (int rep = 0; rep < 3; rep++) {
      for (int j = 0; j < len; j++) {
        int x = left - off + rep * w + j * 6;
        if (x >= left && x + 5 <= right) cv.g.drawChar(blimpMsg[j], x + cv.dx, by - 3 + cv.dy, 1);
      }
    }
  }
  if (sceneMs % 1200 < 150) cv.px(bx - 31, by, A_RED);
  if ((sceneMs + 600) % 1200 < 150) cv.px(bx + 33, by - 8, rgb(58, 255, 138));
}

static void drawCar(Canvas& cv) {
  if (carX == NO_CAR) return;
  int cx = carX, cy = carY, d = carDir;
  if (!cv.hits(cy - 8, 19)) return;
  for (int y = cy - 7; y <= cy + 9; y++) {  // headlight beam
    for (int i = 10; i < 62; i++) {
      float half = (i - 10) * 8.0f / 52;
      float mid = cy + 1 + (i - 10) * 1.0f / 52;
      if (fabsf(y - mid) > half) continue;
      cv.add(cx + d * i, y, 1, {225, 240, 255}, 90 * (62 - i) / 52);
    }
  }
  for (int i = 11; i < 34; i++) cv.add(cx - d * i, cy, 1, {255, 40, 60}, 154 * (34 - i) / 23);  // tail streak
  for (int i = -10; i <= 10; i++) cv.add(cx + i, cy + 4, 1, {255, 138, 42}, 102 * (10 - abs(i)) / 10);
  static const int8_t P[6][2] = {{10, 1}, {6, -2}, {-4, -3}, {-11, -1}, {-11, 2}, {8, 3}};
  for (int i = 1; i < 5; i++)
    cv.tri(cx + P[0][0] * d, cy + P[0][1], cx + P[i][0] * d, cy + P[i][1], cx + P[i + 1][0] * d, cy + P[i + 1][1], rgb(29, 33, 51));
  cv.rect(d > 0 ? cx - 3 : cx - 2, cy - 2, 5, 1, rgb(40, 180, 174));
  cv.px(cx - 11 * d, cy, A_RED);
  cv.px(cx - 11 * d, cy + 1, A_RED);
  cv.px(cx + 10 * d, cy, rgb(232, 246, 255));
}

static void drawTraffic(Canvas& cv) {
  for (int x = 8; x < 240; x += 32) cv.glow(x + 3, 147, 9, {255, 160, 60}, 140);  // freeway lamps
  if (!cv.hits(158, 4)) return;
  float t = sceneMs / 1000.0f;
  for (int i = 0; i < 16; i++) {  // headlights coming, taillights going
    float q = fmodf(frac(i + 600) * 260 - t * (26 + frac(i + 610) * 8), 260);
    if (q < 0) q += 260;
    int x = (int)q - 10;
    cv.px(x, 158, rgb(255, 248, 225));
    cv.px(x + 2, 158, rgb(255, 248, 225));
    x = (int)fmodf(frac(i + 620) * 260 + t * (24 + frac(i + 630) * 8), 260) - 10;
    cv.px(x, 161, A_RED);
    cv.px(x + 2, 161, A_RED);
  }
}

static Rgb slideColor() {
  static const Rgb C[3] = {{42, 209, 201}, {255, 110, 80}, {255, 43, 214}};
  return C[slide];
}

static void drawBillboard(Canvas& cv) {
  if (!cv.hits(64, 100)) return;
  for (int y = 64; y < 160; y++) cv.add(170, y, 64, slideColor(), 26);  // light spilling on the wall
  const int x0 = 176, y0 = 70, w = 52, h = 84;
  for (int y = y0; y < y0 + h; y++) {
    if (!cv.hits(y, 1)) continue;
    int t = (y - y0) * 256 / h;
    Rgb c;
    if (slide == 0) c = lerpRgb({4, 32, 42}, {12, 90, 102}, t);
    else if (slide == 1) c = t < 154 ? lerpRgb({42, 10, 58}, {194, 48, 90}, t * 256 / 154) : lerpRgb({194, 48, 90}, {255, 138, 58}, (t - 154) * 256 / 102);
    else c = {20, 4, 15};
    cv.rect(x0, y, w, 1, c565(c));
  }
  if (slide == 0) {  // the weather
    if (cv.hits(70, 40)) weatherIcon(cv.g, wxKind, 202 + cv.dx, 94 + cv.dy, 2);
    char buf[8];
    if (wxValid) snprintf(buf, sizeof(buf), "%d", wxTemp);
    else strcpy(buf, "--");
    if (cv.hits(112, 30)) {
      cv.g.setTextDatum(MC_DATUM);
      cv.g.setTextColor(TFT_WHITE);
      int tw = cv.g.textWidth(buf, 4);
      cv.g.drawString(buf, 200 + cv.dx, 126 + cv.dy, 4);
      cv.g.drawCircle(200 + tw / 2 + 4 + cv.dx, 117 + cv.dy, 2, TFT_WHITE);
    }
    cv.text(wxLabel, 202, 143, 1, rgb(191, 244, 255), MC_DATUM);
  } else if (slide == 1) {  // NEO LA sunset
    for (int y = 109; y < 130; y++) {
      int dy = y - 124, hw = isqrtSmall(max(0, 225 - dy * dy));
      cv.rect(202 - hw, y, 2 * hw + 1, 1, rgb(255, 211, 90));
    }
    for (int y = 112; y < 130; y += 4) cv.rect(176, y, 52, 1 + (y - 112) / 6, rgb(42, 10, 58));
    cv.rect(176, 129, 52, 25, rgb(26, 8, 32));
    palm(cv, 3);
    cv.text("NEO LA", 202, 143, 2, rgb(255, 233, 242), MC_DATUM);
  } else {  // late show
    for (int y = 72; y < 152; y += 6) {
      cv.rect(178, y, 2, 3, rgb(255, 43, 214));
      cv.rect(224, y, 2, 3, rgb(255, 43, 214));
    }
    cv.text("LATE", 202, 97, 2, rgb(255, 233, 246), MC_DATUM);
    cv.text("SHOW", 202, 113, 2, rgb(255, 233, 246), MC_DATUM);
    cv.text("MIDNIGHT", 202, 131, 1, rgb(255, 138, 232), MC_DATUM);
    cv.text("PREMIERE", 202, 141, 1, rgb(255, 138, 232), MC_DATUM);
  }
  for (int y = y0; y < y0 + h; y += 2) cv.blend(x0, y, w, {0, 0, 0}, 56, false);  // scanlines
}

static void drawRightBuilding(Canvas& cv) {
  for (int x = 168; x < 240; x++) {
    int top = 64 - (x - 168) * 6 / 72;
    cv.rect(x, top, 1, HUD_Y - top, A_FACADE);
  }
  cv.rect(168, 64, 1, 150, rgb(51, 56, 82));
  for (int y = 66; y < 212; y += 5) {
    if (!cv.hits(y, 2)) continue;
    for (int x = 171; x < 238; x += 4) {
      if (x > 174 && x < 230 && y > 66 && y < 156) continue;
      float r = frac((uint32_t)(x * 91 + y * 17));
      cv.rect(x, y, 2, 2, r < 0.07f ? rgb(200, 132, 64) : r < 0.12f ? rgb(138, 160, 200) : rgb(23, 20, 37));
    }
  }
  cv.rect(174, 68, 56, 88, rgb(28, 26, 38));
  for (int x : {184, 202, 220}) cv.rect(x, 156, 2, 6, rgb(42, 40, 54));
  drawBillboard(cv);
  cv.rect(184, 161, 36, 10, rgb(20, 6, 18));
  if (cv.hits(161, 10)) cv.g.drawRect(184 + cv.dx, 161 + cv.dy, 36, 10, A_PINK);
  cv.text("DONUTS", 202, 166, 1, A_PINK, MC_DATUM);
}

static void drawStreet(Canvas& cv) {
  for (int y = max(cv.y0, STREET_Y); y < min(cv.y1, HUD_Y); y++) {
    uint16_t c = c565(lerpRgb({16, 14, 25}, {5, 5, 10}, (y - STREET_Y) * 256 / 38));
    cv.rect(0, y, y >= 198 ? 240 : 168, 1, c);
  }
  cv.rect(0, 178, 168, 1, rgb(34, 32, 46));
  for (int x = 4; x < 168; x += 14) cv.rect(x, 205, 7, 1, rgb(64, 60, 52));
}

static void drawMotel(Canvas& cv) {
  cv.rect(14, 160, 3, 40, rgb(26, 22, 34));
  cv.rect(5, 104, 21, 56, rgb(20, 7, 12));
  if (cv.hits(104, 56)) cv.g.drawRect(5 + cv.dx, 104 + cv.dy, 21, 56, A_MOTEL);
  static const char* const M[5] = {"M", "O", "T", "E", "L"};
  for (int i = 0; i < 5; i++)
    if (i != 3 || motelE) cv.text(M[i], 16, 113 + i * 21 / 2, 2, A_MOTEL, MC_DATUM);
  cv.rect(1, 162, 44, 10, rgb(10, 26, 16));
  if (cv.hits(162, 10)) cv.g.drawRect(1 + cv.dx, 162 + cv.dy, 44, 10, rgb(34, 130, 77));
  cv.text("VACANCY", 23, 167, 1, vacancyOn ? A_VACANCY : A_VACANCYOFF, MC_DATUM);
}

static void drawTruck(Canvas& cv) {
  if (!cv.hits(158, 50)) return;
  cv.rect(84, 164, 1, 42, rgb(42, 38, 48));  // pole for the string lights
  for (int y = 170; y < 202; y++) {  // box body with rounded top corners, then the cab
    uint16_t c = c565(lerpRgb({238, 240, 244}, {169, 172, 184}, (y - 170) * 256 / 32));
    int in = y == 170 ? 2 : (y == 171 ? 1 : 0);
    cv.rect(92 + in, y, 50 - 2 * in, 1, c);
    if (y >= 178) cv.rect(141, y, (y < 187 ? 9 + (y - 178) * 6 / 9 : 15) + 1, 1, c);
  }
  for (int y = 180; y < 188; y++) cv.rect(143, y, 7 + (y - 180) * 5 / 7, 1, rgb(26, 34, 54));  // windshield
  for (int y = 180; y < 188; y++) cv.rect(146 - (y - 180) * 3 / 8, y, 2, 1, rgb(106, 82, 74));
  cv.rect(141, 178, 1, 24, rgb(124, 128, 140));
  cv.rect(92, 194, 64, 1, rgb(255, 204, 51));
  cv.rect(92, 195, 64, 3, rgb(200, 35, 44));
  cv.rect(92, 198, 64, 1, rgb(31, 154, 74));
  cv.rect(140, 201, 17, 2, rgb(52, 52, 62));
  cv.rect(91, 201, 6, 2, rgb(52, 52, 62));
  cv.rect(155, 190, 2, 2, rgb(255, 246, 216));
  cv.rect(155, 193, 2, 1, rgb(255, 154, 58));
  cv.rect(151, 181, 2, 3, rgb(34, 35, 44));
  for (int q : {103, 148}) {
    for (int y = 197; y <= 202; y++) {
      int hw = isqrtSmall(25 - (202 - y) * (202 - y));
      cv.rect(q - hw, y, 2 * hw + 1, 1, rgb(22, 22, 30));
    }
    cv.dot(q, 203, 4, rgb(8, 8, 12));
    cv.dot(q, 203, 1, rgb(90, 92, 102));
  }
  // serving window: kitchen light, menu, hood, the cook
  for (int y = 178; y < 190; y++) cv.rect(99, y, 33, 1, c565(lerpRgb({255, 240, 196}, {240, 138, 46}, (y - 178) * 256 / 12)));
  cv.rect(101, 179, 11, 5, rgb(58, 36, 22));
  cv.rect(102, 180, 6, 1, rgb(255, 210, 122));
  cv.rect(102, 182, 8, 1, rgb(255, 210, 122));
  cv.rect(114, 179, 16, 1, rgb(185, 138, 90));
  cv.ellipse(122, 190, 5, 5, rgb(42, 26, 20));
  cv.dot(122, 183, 2, rgb(42, 26, 20));
  cv.rect(121, 181, 3, 1, rgb(244, 240, 232));
  cv.rect(98, 177, 35, 1, rgb(108, 112, 124));
  cv.rect(98, 177, 1, 14, rgb(108, 112, 124));
  cv.rect(132, 177, 1, 14, rgb(108, 112, 124));
  for (int y = 172; y < 178; y++) {  // awning propped open
    int in = (y - 172) * 4 / 6;
    cv.rect(94 + in, y, 42 - 2 * in, 1, y == 172 ? rgb(224, 65, 74) : rgb(179, 28, 38));
  }
  cv.line(99, 180, 95, 173, rgb(138, 142, 154));
  cv.line(131, 180, 135, 173, rgb(138, 142, 154));
  cv.rect(96, 190, 38, 2, rgb(201, 204, 214));
  cv.rect(96, 192, 38, 1, rgb(126, 130, 142));
  cv.rect(103, 188, 1, 2, rgb(216, 38, 46));
  cv.rect(105, 188, 1, 2, rgb(242, 194, 48));
  cv.rect(107, 188, 1, 2, rgb(58, 160, 74));
  cv.rect(127, 187, 2, 3, rgb(180, 190, 205));
  // roof sign and AC unit
  cv.rect(102, 168, 1, 2, rgb(58, 54, 64));
  cv.rect(125, 168, 1, 2, rgb(58, 54, 64));
  cv.rect(97, 158, 33, 10, rgb(255, 210, 58));
  for (int x = 98; x < 130; x += 3) {
    cv.px(x, 158, rgb(255, 246, 200));
    cv.px(x, 167, rgb(255, 246, 200));
  }
  cv.text("TACOS", 114, 163, 1, rgb(176, 24, 24), MC_DATUM);
  cv.rect(131, 167, 7, 2, rgb(142, 146, 158));
  cv.rect(131, 169, 7, 1, rgb(110, 114, 126));
  // string lights from the pole along the awning
  cv.line(84, 164, 88, 167, rgb(42, 36, 48));
  cv.line(88, 167, 94, 172, rgb(42, 36, 48));
  static const uint16_t BULB[5] = {rgb(255, 80, 80), rgb(255, 210, 58), rgb(95, 208, 255), rgb(122, 255, 138), rgb(255, 138, 240)};
  for (int i = 0; i < 14; i++) {
    int x, y;
    if (i < 4) { x = 84 + i * 10 / 4; y = 164 + 8 * i * i / 16; }
    else { x = 95 + (i - 4) * 4; y = 172; }
    bool on = hsh(i * 31 + bulbSeed) % 8 != 0;
    cv.px(x, y, on ? BULB[i % 5] : rgb(58, 48, 64));
  }
  // warm light on the wet road, headlight beam
  cv.glow(115, 207, 28, {255, 180, 100}, 56);
  for (int i = 0; i < 34; i++) {
    int half = 1 + i * 6 / 34;
    for (int y = 192 - half; y <= 192 + half; y++) cv.add(156 + i, y, 1, {255, 240, 210}, 56 * (34 - i) / 34);
  }
  // steam from the grill vent
  for (int i = 0; i < 3; i++) {
    float p = fmodf(sceneMs * 0.0003f + i / 3.0f, 1);
    int cx = 134 + (int)lroundf(sinf(sceneMs * 0.0013f + i) * 2), cy = 165 - (int)(p * 20), r = 1 + (int)(p * 5);
    int a = (int)(41 * (1 - p));
    for (int y = cy - r; y <= cy + r; y++) {
      int hw = isqrtSmall(max(0, r * r - (y - cy) * (y - cy)));
      cv.blend(cx - hw, y, 2 * hw + 1, {235, 225, 215}, a, false);
    }
  }
}

// One customer seen from behind, lit by the serving window.
static void person(Canvas& cv, int px, int fy, int h, uint16_t coat, Rgb coatRgb, uint8_t kind, uint8_t umb) {
  const int hy = fy - h;
  const uint16_t rim = c565(lerpRgb(coatRgb, {255, 176, 104}, 154));
  cv.rect(px - 2, fy - 7, 2, 7, rgb(12, 17, 32));
  cv.rect(px + 1, fy - 7, 2, 7, rgb(12, 17, 32));
  cv.rect(px - 2, fy - 1, 2, 1, rgb(5, 5, 8));
  cv.rect(px + 1, fy - 1, 2, 1, rgb(5, 5, 8));
  cv.dot(px, hy + 6, 3, coat);
  for (int y = hy + 6; y < fy - 6; y++) {
    int hw = 3 - (y - hy - 6) / 4;
    cv.rect(px - hw, y, 2 * hw + 1, 1, coat);
  }
  if (kind & 1) cv.ellipse(px, hy + 2, 3, 3, coat);  // hood up
  else cv.dot(px, hy + 2, 2, rgb(10, 8, 16));
  cv.rect(px - 1, (kind & 1) ? hy - 1 : hy, 3, 1, rim);
  cv.px(px - 3, hy + 5, rim);
  cv.px(px + 3, hy + 5, rim);
  if (kind & 4) {  // shoulder bag
    cv.rect(px - 4, hy + 6, 1, fy - 11 - hy - 6, rgb(26, 20, 14));
    cv.rect(px - 6, fy - 11, 3, 3, rgb(42, 32, 22));
  }
  if ((kind & 2) && !umb) {  // phone
    cv.rect(px + 3, hy + 6, 1, 3, coat);
    cv.rect(px + 3, hy + 4, 1, 2, rgb(143, 244, 255));
    cv.glow(px + 4, hy + 5, 4, {120, 230, 255}, 90);
  }
  if (umb) {
    cv.line(px + 1, hy + 7, px + 1, hy - 2, rgb(58, 56, 66));
    for (int y = hy - 5; y <= hy - 2; y++) {
      int dy = hy - 2 - y, hw = (int)lroundf(7.5f * sqrtf(max(0.0f, 1 - dy * dy / 9.0f)));
      if (umb == 1) cv.rect(px + 1 - hw, y, 2 * hw + 1, 1, rgb(28, 26, 38));
      else cv.blend(px + 1 - hw, y, 2 * hw + 1, {190, 205, 230}, 107, false);
    }
    cv.add(px - 6, hy - 2, 15, {255, 190, 120}, 100);
  }
}

static void drawPeople(Canvas& cv) {
  if (!cv.hits(185, 24)) return;
  uint8_t wet = precip == 1 ? 1 : 0;
  person(cv, 106, 207, 17, rgb(42, 15, 24), {42, 15, 24}, 1, 0);
  person(cv, 118, 208, 16, rgb(20, 27, 44), {20, 27, 44}, 2, wet ? 1 : 0);
  person(cv, 130, 207, 17, rgb(34, 26, 20), {34, 26, 20}, 4, wet ? 2 : 0);
}

// Neon smeared on the wet street.
static void drawReflections(Canvas& cv) {
  if (!cv.hits(180, 34)) return;
  const struct { int16_t x, w, y0; Rgb c; } SRC[5] = {
      {15, 12, 180, {255, 59, 74}}, {117, 30, 208, {255, 190, 110}}, {202, 40, 180, slideColor()},
      {58, 6, 180, {255, 160, 60}}, {150, 6, 208, {255, 160, 60}}};
  for (auto& s : SRC) {
    for (int y = s.y0; y < HUD_Y; y += 2) {
      int a = (int)((61 - (y - 180) * 3 / 2) * (0.55f + 0.45f * frac(reflSeed * 97 + y + s.x)));
      int off = (int)(frac(reflSeed + y * 3 + s.x) * 3) - 1;
      int w = (int)(s.w * (0.5f + 0.5f * frac(y + s.x)));
      cv.add(s.x - s.w / 2 + off, y, w, s.c, a);
    }
  }
}

static void drawPrecip(Canvas& cv) {
  if (!precip) return;
  for (int i = 0; i < DROPS; i++) {
    int x = (int)dropX[i], y = (int)dropY[i];
    if (precip == 1) cv.line(x, y, x - 2, y + 7, A_RAIN);
    else cv.rect(x, y, 2, 2, A_SNOW);
  }
}

static void drawFog(Canvas& cv) {
  if (!fog) return;
  for (int y = max(cv.y0, SCENE_Y0); y < min(cv.y1, HUD_Y); y++) cv.blend(0, y, 240, {140, 130, 145}, 50 + (y - SCENE_Y0) * 30 / 164, false);
}

static void drawAll(Canvas& cv) {
  drawSky(cv);
  drawBeams(cv);
  drawPlanes(cv);
  drawDowntown(cv);
  drawBlimp(cv);
  drawCar(cv);
  drawTraffic(cv);
  for (int p = 0; p < 3; p++) palm(cv, p);
  drawRightBuilding(cv);
  drawStreet(cv);
  drawMotel(cv);
  drawTruck(cv);
  drawPeople(cv);
  drawReflections(cv);
  drawPrecip(cv);
  drawFog(cv);
}

class NeoLA : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band = new TFT_eSprite(&tft);  // allocated while this face is up, so other faces get the RAM back
    box = new TFT_eSprite(&tft);
    band->setColorDepth(16);
    bandOk = band->createSprite(240, STRIP_H) != nullptr;
    box->setColorDepth(16);
    boxOk = box->createSprite(BOX_W, BOX_H) != nullptr;
    pre = (Precomp*)malloc(sizeof(Precomp));
    if (pre) buildPrecomp();

    sceneMs = millis();
    carX = NO_CAR;
    flashOn = false;
    motelE = vacancyOn = true;
    stepScene(sceneMs);
    tft.fillScreen(A_BG);
    drawHud();
    timeStr[0] = '\0';
    shownCount = -1;
    for (bool& d : dirty) d = true;
  }

  void end() override {
    band->deleteSprite();
    box->deleteSprite();
    delete band;
    delete box;
    band = box = nullptr;
    bandOk = boxOk = false;
    free(pre);
    pre = nullptr;
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(A_CREAM, A_BG);
      tft.setTextPadding(tft.textWidth("88:88:88", 4));
      tft.drawString(timeStr, 10, 6, 4);
      tft.setTextPadding(0);
    }
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 92, textWidth2);
      tft.fillRect(10, 34, 94, 16, A_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(A_TEAL, A_BG);
      tft.drawString(city, 10, 34, 2);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate();
    bool wxChanged = weatherChanged(s, prev) || s.code != prev.code;
    if (wxChanged) setWeather(s);
    if (wxChanged || strcmp(s.time, prev.time) != 0) setBlimpMessage(s);
  }

  void tick(uint32_t ms) override {
    drawMillis();
    int mim = msIntoMin(ms);
    sceneMs = ms;
    stepScene(ms);
    flush();
    if (precip) {
      for (int i = 0; i < DROPS; i++) {
        paintBox(oldDropX[i] - 8, oldDropY[i] - 1, 16, 18);
        if (respawned[i]) paintBox((int)dropX[i] - 8, (int)dropY[i] - 1, 16, 18);
      }
    }
    if (ms - lastSteam >= 120) {
      lastSteam = ms;
      paintBox(126, 138, 18, 34);
    }
    for (int i = 0; i < 3; i++) {
      if (planeX[i] != shownPlaneX[i] || planeY[i] != shownPlaneY[i] || ms - lastPlaneBlink >= 100) {
        paintBox(min(planeX[i], shownPlaneX[i]) - 1, min(planeY[i], shownPlaneY[i]) - 1, 6, abs(planeY[i] - shownPlaneY[i]) + 3);
        shownPlaneX[i] = planeX[i];
        shownPlaneY[i] = planeY[i];
      }
    }
    if (ms - lastPlaneBlink >= 100) lastPlaneBlink = ms;
    if (beaconsChanged) {
      beaconsChanged = false;
      paintBox(27, 102, 3, 14);
      paintBox(129, 56, 3, 3);
      paintBox(78, 86, 16, 3);
    }
    if (motelChanged) {
      motelChanged = false;
      paintBox(8, 136, 16, 16);
    }
    if (vacancyChanged) {
      vacancyChanged = false;
      paintBox(1, 162, 32, 10);
      paintBox(33, 162, 12, 10);
    }
    drawCount(mim);
  }

 private:
  TFT_eSprite* band = nullptr;
  TFT_eSprite* box = nullptr;
  bool bandOk = false, boxOk = false;
  bool dirty[STRIPS] = {};
  char timeStr[9] = "";
  int shownCount = -1;
  uint32_t lastBeam = 0, lastBlimp = 0, lastTraffic = 0, lastShimmer = 0, lastBulbs = 0, lastSteam = 0;
  uint32_t lastPlaneBlink = 0, nextFlicker = 0, nextVacancy = 0, nextFlash = 0, flashEnd = 0;
  int oldDropX[DROPS], oldDropY[DROPS];
  bool respawned[DROPS] = {};
  int shownPlaneX[3] = {}, shownPlaneY[3] = {};
  bool beaconsChanged = false, motelChanged = false, vacancyChanged = false;
  bool carShown = false;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  void mark(int a, int b) {
    for (int i = 0; i < STRIPS; i++) {
      int sy = SCENE_Y0 + i * STRIP_H;
      if (sy < b && sy + STRIP_H > a) dirty[i] = true;
    }
  }

  bool covered(int a, int b) {
    for (int i = 0; i < STRIPS; i++) {
      int sy = SCENE_Y0 + i * STRIP_H;
      if (sy < b && sy + STRIP_H > a && !dirty[i]) return false;
    }
    return true;
  }

  // Advance everything that moves and mark the rows that need repainting.
  void stepScene(uint32_t ms) {
    float t = ms / 1000.0f;
    if (ms - lastBeam >= 400) {
      lastBeam = ms;
      beamA[0] = -PIF / 2 + 0.45f * sinf(t * 0.22f);
      beamA[1] = -PIF / 2 + 0.45f * sinf(t * 0.22f + 2.3f);
      mark(SCENE_Y0, 161);
    }
    if (ms - lastBlimp >= 120) {
      lastBlimp = ms;
      int bx = 270 - (int)fmodf(t * 7, 330), sc = (int)(t * 14);
      if (bx != blimpX || sc != blimpScroll) {
        blimpX = bx;
        blimpScroll = sc;
        mark(BLIMP_Y - 10, BLIMP_Y + 12);
      }
    }
    int mim = msIntoMin(ms);
    carX = NO_CAR;
    for (int i = 0; i < 2; i++) {
      int dt = mim - (i == 0 ? 15000 : 45000);
      if (dt >= 0 && dt < CAR_MS) {
        float p = dt / (float)CAR_MS;
        carDir = i == 0 ? 1 : -1;
        carX = carDir > 0 ? -30 + (int)(p * 300) : 270 - (int)(p * 300);
        carY = 118 + (int)lroundf(sinf(p * 6) * 1.5f);
      }
    }
    if (carX != NO_CAR || carShown) mark(108, 131);
    carShown = carX != NO_CAR;
    if (ms - lastTraffic >= 100) {
      lastTraffic = ms;
      mark(156, 164);
    }
    if (ms - lastShimmer >= 250) {
      lastShimmer = ms;
      reflSeed++;
      mark(180, HUD_Y);
    }
    if (ms - lastBulbs >= 500) {
      lastBulbs = ms;
      bulbSeed++;
      mark(164, 173);
    }
    uint8_t sl = (ms / 4000) % 3;
    if (sl != slide) {
      slide = sl;
      mark(62, 172);
      mark(180, HUD_Y);
    }
    for (int i = 0; i < 3; i++) {
      float p = fmodf(t * 0.012f + i / 3.0f, 1);
      planeX[i] = 250 - (int)(p * 270);
      planeY[i] = 70 + i * 9 + (int)(p * 14);
    }
    bool m = (ms % 1400) < 700, tw = (ms % 1000) < 500;
    if (m != mastOn || tw != towerOn) {
      mastOn = m;
      towerOn = tw;
      beaconsChanged = true;
    }
    if ((int32_t)(ms - nextFlicker) >= 0) {  // a failing E on the motel sign
      bool on = random(6) != 0;
      nextFlicker = ms + (on ? 400 + random(1500) : 70 + random(120));
      if (on != motelE) {
        motelE = on;
        motelChanged = true;
      }
    }
    if ((int32_t)(ms - nextVacancy) >= 0) {
      bool on = random(9) != 0;
      nextVacancy = ms + (on ? 1200 + random(3000) : 90 + random(150));
      if (on != vacancyOn) {
        vacancyOn = on;
        vacancyChanged = true;
      }
    }
    if (flashOn && (int32_t)(ms - flashEnd) >= 0) {
      flashOn = false;
      nextFlash = ms + 2500 + random(3500);
      mark(SCENE_Y0, 172);
    } else if (!flashOn && storm && (int32_t)(ms - nextFlash) >= 0) {
      flashOn = true;
      boltSeed = random(1, 100000);
      flashEnd = ms + 150;
      mark(SCENE_Y0, 172);
    }
    if (precip) stepPrecip();
  }

  void spawnDrop(int i, bool anywhere) {
    dropX[i] = random(0, 256);
    dropY[i] = anywhere ? random(52, 196) : random(52, 64);
  }

  void stepPrecip() {
    for (int i = 0; i < DROPS; i++) {
      oldDropX[i] = (int)dropX[i];
      oldDropY[i] = (int)dropY[i];
      respawned[i] = false;
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

  void setWeather(const FaceState& s) {
    CnWeather w = cnWeather(s.code);
    uint8_t p = !s.weatherValid ? 0 : (w == CNW_RAIN || w == CNW_THUNDER) ? 1 : (w == CNW_SNOW ? 2 : 0);
    bool fg = s.weatherValid && w == CNW_FOG;
    bool cl = s.weatherValid && (w == CNW_CLOUD || w == CNW_OVERCAST || w == CNW_RAIN || w == CNW_THUNDER);
    wxKind = s.weatherValid ? w : CNW_CLOUD;
    wxTemp = s.temp;
    wxValid = s.weatherValid;
    wxLabel = s.weatherValid ? cnWeatherLabel(w) : "NO SIGNAL";
    storm = s.weatherValid && w == CNW_THUNDER;
    drawWeather(s);
    if (p != precip) {
      precip = p;
      for (int i = 0; i < DROPS; i++) spawnDrop(i, true);
    }
    fog = fg;
    cloudy = cl;
    mark(SCENE_Y0, HUD_Y);
  }

  // The blimp's LED band: temperature, weather and time.
  void setBlimpMessage(const FaceState& s) {
    char tbuf[8];
    if (s.weatherValid) snprintf(tbuf, sizeof(tbuf), "%d%c", s.temp, s.unit);
    else strcpy(tbuf, "--");
    snprintf(blimpMsg, sizeof(blimpMsg), " %s %s // NEO LA // %s //", tbuf, wxLabel, s.time);
  }

  void flush() {
    if (!bandOk || !pre) return;
    for (int i = 0; i < STRIPS; i++) {
      if (!dirty[i]) continue;
      int sy = SCENE_Y0 + i * STRIP_H, h = min(STRIP_H, HUD_Y - sy);
      Canvas cv{*band, 0, -sy, sy, sy + h, (uint16_t*)band->getPointer()};
      drawAll(cv);
      band->pushSprite(0, sy, 0, 0, 240, h);
    }
    for (bool& d : dirty) d = false;
  }

  // Repaint one small area (at most BOX_W x BOX_H), unless a strip already covered it.
  void paintBox(int bx, int by, int w, int h) {
    if (!boxOk || !pre) return;
    if (bx < 0) { w += bx; bx = 0; }
    if (by < SCENE_Y0) { h += by - SCENE_Y0; by = SCENE_Y0; }
    w = min(min(w, BOX_W), 240 - bx);
    h = min(min(h, BOX_H), HUD_Y - by);
    if (w <= 0 || h <= 0) return;
    Canvas cv{*box, -bx, -by, by, by + h, (uint16_t*)box->getPointer()};
    drawAll(cv);
    box->pushSprite(bx, by, 0, 0, w, h);
  }

  void drawMillis() {
    char buf[6] = ".---";
    if (timeSynced()) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      snprintf(buf, sizeof(buf), ".%03d", (int)(tv.tv_usec / 1000));
    }
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(A_TEAL, A_BG);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, 14 + tft.textWidth("88:88:88", 4), 12, 2);
    tft.setTextPadding(0);
  }

  void drawDate() {
    static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    tft.fillRect(104, 34, 74, 16, A_BG);
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, MONTHS[t.tm_mon]);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(A_AMBER, A_BG);
    int x = 104 + tft.drawString(buf, 104, 34, 2) + 4;  // 11 NOV, then the weekday boxed: [WED]
    tft.drawRect(x, 36, 23, 12, A_AMBER);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(DAYS[t.tm_wday], x + 12, 42, 1);
  }

  void drawHud() {
    tft.drawRect(178, 6, 54, 44, A_ORANGE);
    tft.fillRect(178, 6, 54, 14, A_ORANGE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(A_ON_ORANGE);
    tft.drawString("WEATHER", 205, 13, 1);

    tft.fillRect(0, HUD_Y, 240, 240 - HUD_Y, A_BG);
    tft.drawRect(6, HUD_Y, 228, 18, A_TEAL);
    tft.fillRect(6, HUD_Y, 30, 18, A_ORANGE);
    tft.setTextColor(A_ON_ORANGE);
    tft.drawString("LA", 21, HUD_Y + 9, 2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(A_CREAM, A_BG);
    tft.drawString("NEO LOS ANGELES", 42, HUD_Y + 9, 1);
  }

  void drawWeather(const FaceState& s) {
    tft.fillRect(179, 20, 52, 29, A_BG);
    tft.setTextDatum(TL_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(A_LABEL);
      tft.drawString("NO SIG", 187, 30, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    weatherIcon(tft, w, 190, 29, 1);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", s.temp);
    tft.setTextColor(TFT_WHITE);
    int x = 202 + tft.drawString(buf, 202, 21, 2);
    tft.drawCircle(x + 2, 23, 1, TFT_WHITE);
    char u[2] = {s.unit, '\0'};
    tft.drawString(u, x + 5, 21, 2);
    tft.setTextColor(A_LABEL);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(cnWeatherLabel(w), 205, 41, 1);
  }

  // FLYBY 00:12 until the next flying car, FLYBY NOW while it crosses.
  void drawCount(int mim) {
    int s = mim / 1000;
    bool live = (mim >= 15000 && mim < 15000 + CAR_MS) || (mim >= 45000 && mim < 45000 + CAR_MS);
    int left = s < 15 ? 15 - s : (s < 45 ? 45 - s : 75 - s);
    int key = live ? 1000 : left;
    if (key == shownCount) return;
    shownCount = key;
    char buf[16];
    if (live) strcpy(buf, "FLYBY NOW");
    else snprintf(buf, sizeof(buf), "FLYBY 00:%02d", left);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(live ? A_ORANGE : A_TEAL, A_BG);
    tft.setTextPadding(tft.textWidth("FLYBY 00:88", 1));
    tft.drawString(buf, 230, HUD_Y + 9, 1);
    tft.setTextPadding(0);
  }
};

static NeoLA neoLA;
Face* faceNeoLA() { return &neoLA; }
