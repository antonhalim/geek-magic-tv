#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "orbit_data.h"
#include "../config.h"
#include "../display.h"
#include "../weather.h"

// Orbit: Earth through a station window. The day/night line is the real one for
// your city and time, with city lights on the night side and a satellite at :00.

static constexpr uint16_t rgb(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---- palette ----
static const uint16_t O_BG = rgb(7, 6, 20);
static const uint16_t O_CYAN = rgb(0, 229, 255);
static const uint16_t O_BRACKET = rgb(0, 150, 170);  // HUD corners on the glass
static const uint16_t O_PING1 = rgb(0, 170, 190), O_PING2 = rgb(0, 95, 110);
static const uint16_t O_MAGENTA = rgb(255, 79, 216);
static const uint16_t O_WHITE = rgb(234, 246, 255);
static const uint16_t O_PINK = rgb(255, 194, 238);
static const uint16_t O_COOL = rgb(127, 208, 255);
static const uint16_t O_AMBER = rgb(255, 179, 71);
static const uint16_t O_ON_CYAN = rgb(4, 22, 26);
static const uint16_t O_ON_MAG = rgb(42, 0, 34);
// window frame
static const uint16_t O_METAL = rgb(27, 33, 45), O_RIM = rgb(58, 68, 89), O_LIP = rgb(5, 7, 12);
static const uint16_t O_HAIR = rgb(18, 101, 118);  // cyan at 35% over the metal
static const uint16_t O_BOLT = rgb(75, 86, 108), O_BOLTHI = rgb(135, 148, 173);
// satellite
static const uint16_t O_TRUSS = rgb(138, 144, 160), O_PANEL = rgb(36, 67, 143), O_PANELHI = rgb(61, 100, 200);
static const uint16_t O_FOIL = rgb(217, 164, 65), O_FOILHI = rgb(255, 224, 138), O_LED = rgb(255, 59, 59);
// weather icon
static const uint16_t O_SUN = rgb(255, 210, 122), O_CLOUD = rgb(168, 200, 232), O_CLOUDDK = rgb(100, 118, 148);
static const uint16_t O_DROP = rgb(95, 180, 255), O_YELLOW = rgb(255, 210, 58);

// ---- layout and timing ----
static const int SCENE_Y0 = 50, HUD_Y = 214;
static const int STRIP_H = 12;
static const int BOX_W = 32, BOX_H = 28;
static const int NO_SAT = -9999;
static const int SAT_RUN_MS = 14000;  // satellite crosses from :00
static const int MOON_X = 200, MOON_Y = 80;
static const float MOON_R = 6.5f;
static const int TWINKLES = 6;
static const float DEG = 3.14159265f / 180;

// Scene state the drawing reads, so every repaint agrees with what is on screen.
static uint8_t* dayTab = nullptr;  // 0 night .. 255 day by sideways offset (512), refreshed each minute
static float sunX = -1, sunY = 0;    // sunrise or sunset flare on the horizon; sunX < 0 when out of view
static bool moonUp = false;
static float moonPhase = 0, moonCos = 1;
static int cloudQ10 = 155;  // cloud threshold in tenths of a texture step
static bool snowCover = false, fogHaze = false;
static int satX = NO_SAT, satY = 0;
static bool satLed = false;
static uint8_t pingR = 0;
static bool flashOn = false;
static int flashX = 0, flashY = 0;
static uint8_t twX[TWINKLES], twY[TWINKLES], twLvl[TWINKLES];
static int twCount = 0;

static uint32_t cellHash(int x, int y) {  // stable pseudo-random per pixel
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  return h ^ (h >> 13);
}

static uint32_t isqrt32(uint32_t n) {
  uint32_t r = 0, b = 1u << 30;
  while (b > n) b >>= 2;
  while (b) {
    if (n >= r + b) {
      n -= r + b;
      r = (r >> 1) + b;
    } else {
      r >>= 1;
    }
    b >>= 2;
  }
  return r;
}

// Point in a rounded rectangle, in doubled coordinates so pixel centres are whole numbers.
static bool inRound(int X2, int Y2, int x0, int y0, int x1, int y1, int r) {
  x0 *= 2; y0 *= 2; x1 *= 2; y1 *= 2; r *= 2;
  if (X2 < x0 || X2 > x1 || Y2 < y0 || Y2 > y1) return false;
  int qx = max(max(x0 + r - X2, 0), X2 - (x1 - r));
  int qy = max(max(y0 + r - Y2, 0), Y2 - (y1 - r));
  return qx * qx + qy * qy <= r * r;
}

static bool inGlass(int x, int y) { return inRound(2 * x + 1, 2 * y + 1, GL_X0, GL_Y0, GL_X1, GL_Y1, GL_R); }

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Daylight for a sideways offset from the marker, already scaled for depth.
static int dayAt(int side) {
  if (!dayTab) return 255;
  int i = side + 256;
  return dayTab[i < 0 ? 0 : (i > 511 ? 511 : i)];
}

static void earthPixel(int x, int y, int xa, int h, int& r, int& g, int& b) {
  if (h > 127) h = 127;
  int L = dayAt((xa * (int)pgm_read_word(&ORB_FZ[h])) >> 8);
  int ty = y - TEX_Y0, tx = x - TEX_X0;
  bool inTex = ty >= 0 && ty < TEX_H;
  uint8_t t = inTex ? pgm_read_byte(&ORBIT_TEX[ty * TEX_W + tx]) : 0;
  int cl = ((t & 31) * 10 - cloudQ10) * 5;  // cloud cover 0..256
  cl = cl < 0 ? 0 : (cl > 256 ? 256 : cl);
  bool land = t & 32, high = t & 64;

  int dr, dg, db;  // daylit colour
  if (land && high && snowCover) { dr = 205; dg = 214; db = 228; }
  else if (land && high) { dr = 110; dg = 106; db = 64; }
  else if (land) { dr = 62; dg = 92; db = 48; }
  else { dr = 12; dg = 50; db = 108; }
  dr += (236 - dr) * cl >> 8; dg += (242 - dg) * cl >> 8; db += (252 - db) * cl >> 8;
  int hz = pgm_read_byte(&ORB_HAZE[h]);
  if (fogHaze) hz += (255 - hz) * 90 >> 8;
  dr += (125 - dr) * hz >> 8; dg += (190 - dg) * hz >> 8; db += (245 - db) * hz >> 8;

  int nr = land ? 9 : 3, ng = land ? 12 : 6, nb = land ? 24 : 17;  // night colour
  nr += (28 - nr) * cl * 205 >> 16; ng += (34 - ng) * cl * 205 >> 16; nb += (56 - nb) * cl * 205 >> 16;
  int rim = pgm_read_byte(&ORB_RIM[h]);
  nb += 30 * rim >> 8; ng += 8 * rim >> 8;

  r = nr + ((dr - nr) * L >> 8); g = ng + ((dg - ng) * L >> 8); b = nb + ((db - nb) * L >> 8);
  int tw = ((4 * L * (255 - L)) >> 8) * pgm_read_byte(&ORB_TWH[h]) >> 8;  // orange along the terminator
  r += 60 * tw >> 8; g += 18 * tw >> 8; b -= 6 * tw >> 8;

  if (t & 128) {  // city lights, under the clouds and fading into the haze
    int k = (255 - L) * (255 - L) >> 8;
    k = k * (256 - (cl * 218 >> 8)) >> 8;
    k = k * (256 - (rim * 154 >> 8)) >> 8;
    if (cellHash(x, y) & 1) k = k * 3 / 5;
    r += 250 * k >> 8; g += 170 * k >> 8; b += 70 * k >> 8;
  }
  if (flashOn && cl > 77) {  // lightning inside the clouds
    int fx = x - flashX, fy = y - flashY;
    int d2 = fx * fx + (fy * fy * 41 >> 4);
    if (d2 < 100) {
      int k = (100 - d2) * cl >> 8;
      r += 2 * k; g += 2 * k + k / 10; b += k * 255 / 100;
    }
  }
  r = clamp255(r); g = clamp255(g); b = clamp255(b);
  if (inTex && (pgm_read_byte(&ORBIT_TRACK[ty * TRACK_BPR + (tx >> 3)]) & (0x80 >> (tx & 7)))) {
    r -= r * 140 >> 8; g += (229 - g) * 140 >> 8; b += (255 - b) * 140 >> 8;  // ground track, 55% cyan
  }
}

static void skyPixel(int x, int y, int xa, int e16, int& r, int& g, int& b) {
  r = 3; g = 4; b = 10;
  int eq = e16 >> 2;
  if (eq < SKY_N) {  // atmosphere along the limb
    int L = dayAt((xa * (int)pgm_read_word(&ORB_FZ[0])) >> 8);
    int tw = (4 * L * (255 - L)) >> 8;
    int* c[3] = {&r, &g, &b};
    for (int i = 0; i < 3; i++) {
      int d = pgm_read_byte(&ORB_SKY_DAY[eq * 3 + i]), n = pgm_read_byte(&ORB_SKY_NIGHT[eq * 3 + i]);
      *c[i] += n + ((d - n) * L >> 8) + (pgm_read_byte(&ORB_SKY_TW[eq * 3 + i]) * tw >> 8);
    }
  }
  if (moonUp && abs(x - MOON_X) <= 7 && abs(y - MOON_Y) <= 7) {
    float mx = (x + 0.5f - MOON_X) / MOON_R, my = (y + 0.5f - MOON_Y) / MOON_R;
    if (mx * mx + my * my < 1) {
      float sx = sqrtf(1 - my * my);
      bool lit = moonPhase < 0.5f ? mx > sx * moonCos : mx < -sx * moonCos;
      if (lit) {
        int v = 5 + (cellHash(x, y) & 15);
        r = 235 - v; g = 232 - v; b = 215 - v;
      } else {
        r = 22; g = 24; b = 36;
      }
      return;
    }
  }
  if (g < 90) {  // stars, hidden by the bright limb
    uint32_t hh = cellHash(x, y);
    if (hh % 1000 < 14) {
      int s = 60 + ((hh >> 12) & 127);
      for (int i = 0; i < twCount; i++)
        if (twX[i] == x && twY[i] == y) s = s * (2 + twLvl[i]) / 4;
      s = s * (90 - g) / 90;
      r += s; g += s; b += s + 10;
    }
  }
}

// The sun on the horizon at sunrise or sunset: a white core, a halo and a lens streak.
static void flare(int x, int y, int& r, int& g, int& b) {
  if (sunX < 0) return;
  float fx = x + 0.5f - sunX, fy = y + 0.5f - sunY;
  if (fabsf(fx) >= 34 || fabsf(fy) >= 16) return;
  float d = sqrtf(fx * fx + fy * fy);
  if (d < 2.2f) {
    r = g = b = 255;
    return;
  }
  if (d < 16) {
    float t = d / 16, a = 0.75f * (1 - t);
    r += (int)(255 * a); g += (int)((210 - 90 * t) * a); b += (int)((150 - 110 * t) * a);
  }
  if (fabsf(fy) < 0.5f) {
    float a = 0.8f * (1 - fabsf(fx) / 34);
    r += (int)(255 * a); g += (int)(230 * a); b += (int)(200 * a);
  }
}

static uint16_t basePixel(int x, int y) {
  int X2 = 2 * x + 1, Y2 = 2 * y + 1;
  if (!inRound(X2, Y2, FR_X0, FR_Y0, FR_X1, FR_Y1, FR_R)) return O_BG;
  if (!inRound(X2, Y2, GL_X0, GL_Y0, GL_X1, GL_Y1, GL_R)) {
    if (!inRound(X2, Y2, FR_X0 + 1, FR_Y0 + 1, FR_X1 - 1, FR_Y1 - 1, FR_R - 1)) return O_RIM;
    if (inRound(X2, Y2, GL_X0 - 1, GL_Y0 - 1, GL_X1 + 1, GL_Y1 + 1, GL_R + 1)) return O_HAIR;
    return O_METAL;
  }
  if (!inRound(X2, Y2, GL_X0 + 1, GL_Y0 + 1, GL_X1 - 1, GL_Y1 - 1, GL_R - 1)) return O_LIP;
  int dx16 = (X2 - 2 * ORB_CX) * 8, dy16 = (Y2 - 2 * ORB_CY) * 8;
  int d16 = (int)isqrt32((uint32_t)(dx16 * dx16 + dy16 * dy16));
  int xa = (ORB_CX - ORB_MX) + dx16 * ORB_R / d16;  // sideways from the marker, along the limb
  int r, g, b;
  if (d16 < ORB_R * 16) earthPixel(x, y, xa, (ORB_R * 16 - d16) >> 4, r, g, b);
  else skyPixel(x, y, xa, d16 - ORB_R * 16, r, g, b);
  int s = x + y;
  if ((s >= 96 && s < 126) || (s >= 134 && s < 142)) { r += 11; g += 11; b += 11; }  // glare on the glass
  flare(x, y, r, g, b);
  return rgb(clamp255(r), clamp255(g), clamp255(b));
}

namespace {  // file-local: other faces have their own Canvas

// Overlays go through this, so the same code draws into the strip and the box sprite.
struct Canvas {
  TFT_eSPI& g;
  int dx, dy, y0, y1;  // screen point (x, y) maps to (x + dx, y + dy); rows outside [y0, y1) skipped
  bool hits(int y, int h) const { return y < y1 && y + h > y0; }
  void rect(int x, int y, int w, int h, uint16_t c) {
    if (hits(y, h)) g.fillRect(x + dx, y + dy, w, h, c);
  }
  void circle(int x, int y, int r, uint16_t c) {
    if (hits(y - r, 2 * r + 1)) g.drawCircle(x + dx, y + dy, r, c);
  }
  // Clipped to the window glass, for things that drift in from behind the frame.
  void glassRect(int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; j++) {
      if (!hits(j, 1)) continue;
      for (int i = x; i < x + w; i++)
        if (inGlass(i, j)) g.drawPixel(i + dx, j + dy, c);
    }
  }
};

}  // namespace

static void drawOverlays(Canvas& cv) {
  static const int16_t CORNERS[4][4] = {{16, 62, 1, 1}, {223, 62, -1, 1}, {16, 201, 1, -1}, {223, 201, -1, -1}};
  for (auto& k : CORNERS) {
    cv.rect(k[0], k[3] > 0 ? k[1] : k[1] - 6, 1, 7, O_BRACKET);
    cv.rect(k[2] > 0 ? k[0] : k[0] - 6, k[1], 7, 1, O_BRACKET);
  }
  static const int16_t BOLTS[8][2] = {{40, 53}, {120, 53}, {200, 53}, {40, 210}, {120, 210}, {200, 210}, {6, 132}, {233, 132}};
  for (auto& p : BOLTS) {
    cv.rect(p[0] - 1, p[1], 3, 1, O_BOLT);
    cv.rect(p[0], p[1] - 1, 1, 3, O_BOLT);
    cv.rect(p[0] - 1, p[1] - 1, 1, 1, O_BOLTHI);
  }

  // your city: target with a ping ring
  const int mx = ORB_MX, my = ORB_MY;
  if (pingR) cv.circle(mx, my, pingR, pingR == 7 ? O_PING1 : O_PING2);
  cv.circle(mx, my, 4, O_CYAN);
  cv.rect(mx - 8, my, 3, 1, O_CYAN);
  cv.rect(mx + 6, my, 3, 1, O_CYAN);
  cv.rect(mx, my - 8, 1, 3, O_CYAN);
  cv.rect(mx, my + 6, 1, 3, O_CYAN);
  cv.rect(mx, my, 1, 1, O_CYAN);

  if (satX != NO_SAT) {
    int x = satX, y = satY;
    cv.glassRect(x - 12, y, 24, 1, O_TRUSS);
    cv.glassRect(x - 12, y - 2, 8, 5, O_PANEL);
    cv.glassRect(x + 4, y - 2, 8, 5, O_PANEL);
    cv.glassRect(x - 8, y - 2, 1, 5, O_PANELHI);
    cv.glassRect(x + 8, y - 2, 1, 5, O_PANELHI);
    cv.glassRect(x - 2, y - 2, 4, 5, O_FOIL);
    cv.glassRect(x - 2, y - 2, 4, 1, O_FOILHI);
    if (satLed) cv.glassRect(x + 1, y + 3, 1, 1, O_LED);
  }
}

// Fill a sprite's top-left w x h with the scene at screen (sx, sy), then the overlays.
static void renderArea(TFT_eSprite& spr, int sx, int sy, int w, int h) {
  uint16_t* buf = (uint16_t*)spr.getPointer();
  int stride = spr.width();
  for (int y = 0; y < h; y++) {
    uint16_t* row = buf + y * stride;
    for (int x = 0; x < w; x++) {
      uint16_t c = basePixel(sx + x, sy + y);
      row[x] = (uint16_t)((c >> 8) | (c << 8));  // sprites keep 16-bit pixels byte-swapped
    }
  }
  Canvas cv{spr, -sx, -sy, sy, sy + h};
  drawOverlays(cv);
}

static int minutesUntil(float eventUtc, float utcH) {
  int m = (int)floorf((eventUtc - utcH) * 60 + 0.5f) % 1440;
  return m < 0 ? m + 1440 : m;
}

class Orbit : public Face {
 public:
  void begin() override {
    freeTimeSprite();  // the strip sprite needs that RAM; other faces re-create it
    band.setColorDepth(16);
    bandOk = band.createSprite(240, STRIP_H) != nullptr;
    box.setColorDepth(16);
    boxOk = box.createSprite(BOX_W, BOX_H) != nullptr;
    dayTab = (uint8_t*)malloc(512);

    satX = NO_SAT;
    satLed = flashOn = false;
    pingR = 0;
    pickTwinkles();
    tft.fillScreen(O_BG);
    drawHud();
    timeStr[0] = '\0';
    shownMinute = -3;  // forces the sky and the first full paint on the next tick
    sceneDirty = true;
  }

  void end() override {
    band.deleteSprite();
    box.deleteSprite();
    free(dayTab);
    dayTab = nullptr;
  }

  void update(const FaceState& s, const FaceState& prev) override {
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(O_WHITE, O_BG);
      tft.setTextPadding(tft.textWidth("88:88:88", 4));
      tft.drawString(timeStr, 10, 6, 4);
      tft.setTextPadding(0);
    }
    if (strcmp(s.city, prev.city) != 0) {
      char city[48];
      fitLabel(s.city, city, sizeof(city), 92, textWidth2);
      tft.fillRect(10, 34, 94, 16, O_BG);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(O_CYAN, O_BG);
      tft.drawString(city, 10, 34, 2);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate();
    if (weatherChanged(s, prev) || s.code != prev.code) {
      drawWeather(s);
      setWeather(s);
    }
  }

  void tick(uint32_t ms) override {
    drawMillis();
    long minute = timeSynced() ? (long)(time(nullptr) / 60) : -2;
    if (minute != shownMinute) {
      shownMinute = minute;
      uint32_t sum = computeSky();
      if (sum != skySum) {
        skySum = sum;
        sceneDirty = true;
      }
      drawCount();
    }

    int oldX = satX, oldY = satY;
    bool oldLed = satLed;
    updateSatellite(ms);
    uint8_t oldPing = pingR;
    uint32_t p = ms % 2000;
    pingR = p < 200 ? 7 : (p < 400 ? 10 : 0);
    bool flashed = stepLightning(ms);

    if (sceneDirty) {
      sceneDirty = false;
      repaintRows(SCENE_Y0, HUD_Y);
      return;
    }
    if (satX != oldX || satY != oldY || satLed != oldLed) repaintSatellite(oldX, oldY);
    if (pingR != oldPing) paintBox(ORB_MX - 11, ORB_MY - 11, 23, 23);
    if (flashed) paintBox(flashX - 11, flashY - 7, 23, 15);
    twinkle(ms);
  }

 private:
  TFT_eSprite band = TFT_eSprite(&tft);
  TFT_eSprite box = TFT_eSprite(&tft);
  bool bandOk = false, boxOk = false;
  bool sceneDirty = true;
  bool storm = false;
  char timeStr[9] = "";
  char countText[16] = "";
  long shownMinute = -3;
  uint32_t skySum = 0;
  uint32_t flashEnd = 0, nextFlash = 0, nextTwinkle = 0;

  int msIntoMin(uint32_t ms) {
    if (!timeSynced()) return (int)(ms % 60000);
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int)((tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000);
  }

  // Daylight across the view, the sunrise/sunset flare, the countdown and the moon,
  // from the sun's position over your city. Returns a checksum of what the scene shows.
  uint32_t computeSky() {
    sunX = -1;
    if (!timeSynced()) {
      if (dayTab) memset(dayTab, 255, 512);
      moonUp = false;
      countText[0] = '\0';
      return 1;
    }
    time_t now = time(nullptr);
    struct tm u;
    gmtime_r(&now, &u);
    float doy = u.tm_yday + 1;
    float utcH = u.tm_hour + u.tm_min / 60.0f + u.tm_sec / 3600.0f;
    float B = 360 * DEG * (doy - 81) / 365;
    float eot = 9.87f * sinf(2 * B) - 7.53f * cosf(B) - 1.5f * sinf(B);  // equation of time, minutes
    float dec = -23.44f * DEG * cosf(360 * DEG * (doy + 10) / 365);
    float lat = settings.lat * DEG;
    float a = sinf(lat) * sinf(dec), c = cosf(lat) * cosf(dec);
    float h0 = (utcH + settings.lon / 15 + eot / 60 - 12) * 15 * DEG;  // sun's hour angle at the marker
    float step = ORB_KT * 15 * DEG;
    if (dayTab) {
      for (int i = 0; i < 512; i++) {
        float el = (a + c * cosf(h0 + (i - 256) * step)) / DEG;  // ~ elevation in degrees near the horizon
        dayTab[i] = (uint8_t)clamp255((int)((el / 7 + 0.5f) * 255));
      }
    }

    int fz0 = pgm_read_word(&ORB_FZ[0]);
    int prevL = dayAt(((16 - ORB_MX) * fz0) >> 8);
    for (int x = 17; x <= 224; x++) {  // where the terminator meets the horizon
      int L = dayAt(((x - ORB_MX) * fz0) >> 8);
      if ((L >= 128) != (prevL >= 128)) {
        float xs = L >= 128 ? x + 6 : x - 7;  // the sun sits just on the day side
        if (xs >= 16 && xs <= 224) {
          sunX = xs;
          sunY = ORB_CY - sqrtf((float)ORB_R * ORB_R - (xs - ORB_CX) * (xs - ORB_CX)) - 2.5f;
        }
        break;
      }
      prevL = L;
    }

    float cosH = (sinf(-0.833f * DEG) - a) / c;
    if (cosH > 1) strcpy(countText, "POLAR NIGHT");
    else if (cosH < -1) strcpy(countText, "MIDNIGHT SUN");
    else {
      float half = acosf(cosH) / DEG / 15;
      float noon = 12 - settings.lon / 15 - eot / 60;  // UTC
      int rise = minutesUntil(noon - half, utcH), set = minutesUntil(noon + half, utcH);
      int m = min(rise, set);
      snprintf(countText, sizeof(countText), "%s %02d:%02d", rise < set ? "SUNRISE" : "SUNSET", m / 60, m % 60);
    }

    double age = fmod((double)now / 86400.0 - 10962.76, 29.530588);  // days since a known new moon
    moonPhase = (float)(age / 29.530588);
    if (moonPhase < 0) moonPhase += 1;
    moonUp = fabsf(moonPhase - 0.5f) < 0.42f;  // a thin crescent is lost in the glare
    moonCos = cosf(2 * 3.14159265f * moonPhase);

    uint32_t sum = 2166136261u;
    for (int i = 0; dayTab && i < 512; i++) sum = (sum ^ dayTab[i]) * 16777619u;
    sum = (sum ^ (uint32_t)(int)sunX) * 16777619u;
    sum = (sum ^ (uint32_t)(moonUp ? (int)(moonPhase * 100) : 999)) * 16777619u;
    return sum;
  }

  void setWeather(const FaceState& s) {
    static const uint8_t TH[] = {70, 60, 50, 54, 50, 56, 48};  // cloud threshold x100, CnWeather order
    CnWeather w = s.weatherValid ? cnWeather(s.code) : CNW_CLOUD;
    int q = (TH[w] - 40) * 310 / 60;
    bool sn = w == CNW_SNOW, fg = w == CNW_FOG;
    if (q != cloudQ10 || sn != snowCover || fg != fogHaze) sceneDirty = true;
    cloudQ10 = q;
    snowCover = sn;
    fogHaze = fg;
    storm = w == CNW_THUNDER;
  }

  void updateSatellite(uint32_t ms) {
    int t = msIntoMin(ms);
    satX = NO_SAT;
    if (t < SAT_RUN_MS) {
      satX = -25 + t * 290 / SAT_RUN_MS;
      satY = 92 - (satX - 120) * 7 / 100;
      satLed = ms % 1000 < 200;
    }
  }

  void repaintSatellite(int oldX, int oldY) {
    if (oldX != NO_SAT && satX != NO_SAT && abs(satX - oldX) <= 8 && abs(satY - oldY) <= 8) {
      int x0 = min(oldX, satX) - 12, x1 = max(oldX, satX) + 12;
      int y0 = min(oldY, satY) - 2, y1 = max(oldY, satY) + 4;
      paintBox(x0, y0, x1 - x0, y1 - y0);
      return;
    }
    if (oldX != NO_SAT) paintBox(oldX - 12, oldY - 2, 24, 6);
    if (satX != NO_SAT) paintBox(satX - 12, satY - 2, 24, 6);
  }

  bool stepLightning(uint32_t ms) {
    if (flashOn && (int32_t)(ms - flashEnd) >= 0) {
      flashOn = false;
      nextFlash = ms + 700 + random(900);
      return true;
    }
    if (!flashOn && storm && (int32_t)(ms - nextFlash) >= 0) {
      flashX = random(24, 216);
      flashY = random(146, 202);
      flashOn = true;
      flashEnd = ms + 120;
      return true;
    }
    return false;
  }

  void pickTwinkles() {
    twCount = 0;
    int seen = 0;
    for (int y = 62; y < 110 && twCount < TWINKLES; y++) {
      for (int x = 18; x < 222 && twCount < TWINKLES; x++) {
        if (cellHash(x, y) % 1000 >= 14) continue;
        if (abs(x - MOON_X) < 10 && abs(y - MOON_Y) < 10) continue;
        if (seen++ % 3) continue;  // spread them out
        twX[twCount] = x;
        twY[twCount] = y;
        twLvl[twCount] = 2;
        twCount++;
      }
    }
  }

  void twinkle(uint32_t ms) {
    if (twCount == 0 || (int32_t)(ms - nextTwinkle) < 0) return;
    nextTwinkle = ms + 250;
    int i = random(twCount);
    twLvl[i] = random(3);
    paintBox(twX[i], twY[i], 1, 1);
  }

  // Repaint screen rows [ya, yb) through the strip sprite.
  void repaintRows(int ya, int yb) {
    if (!bandOk) return;
    for (int sy = ya; sy < yb; sy += STRIP_H) {
      int h = min(STRIP_H, yb - sy);
      renderArea(band, 0, sy, 240, h);
      band.pushSprite(0, sy, 0, 0, 240, h);
    }
  }

  // Repaint one small area of the scene (at most BOX_W x BOX_H).
  void paintBox(int bx, int by, int w, int h) {
    if (!boxOk) return;
    if (bx < 0) { w += bx; bx = 0; }
    if (by < SCENE_Y0) { h += by - SCENE_Y0; by = SCENE_Y0; }
    w = min(min(w, BOX_W), 240 - bx);
    h = min(min(h, BOX_H), HUD_Y - by);
    if (w <= 0 || h <= 0) return;
    renderArea(box, bx, by, w, h);
    box.pushSprite(bx, by, 0, 0, w, h);
  }

  void drawMillis() {
    char buf[6] = ".---";
    if (timeSynced()) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      snprintf(buf, sizeof(buf), ".%03d", (int)(tv.tv_usec / 1000));
    }
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(O_CYAN, O_BG);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, 14 + tft.textWidth("88:88:88", 4), 12, 2);
    tft.setTextPadding(0);
  }

  void drawDate() {
    static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    tft.fillRect(104, 34, 74, 16, O_BG);
    if (!timeSynced()) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, MONTHS[t.tm_mon]);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(O_PINK, O_BG);
    int x = 104 + tft.drawString(buf, 104, 34, 2) + 4;  // 11 NOV, then the weekday boxed: [WED]
    tft.drawRect(x, 36, 23, 12, O_PINK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(DAYS[t.tm_wday], x + 12, 42, 1);
  }

  void drawHud() {
    // ATMOS weather chip
    tft.drawRect(178, 6, 54, 44, O_MAGENTA);
    tft.fillRect(178, 6, 54, 14, O_MAGENTA);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(O_ON_MAG);
    tft.drawString("ATMOS", 205, 13, 1);

    // bottom HUD: LEO tag, orbit, sunrise/sunset countdown
    tft.fillRect(0, HUD_Y, 240, 240 - HUD_Y, O_BG);
    tft.drawRect(6, HUD_Y, 228, 18, O_CYAN);
    tft.fillRect(6, HUD_Y, 32, 18, O_CYAN);
    tft.setTextColor(O_ON_CYAN);
    tft.drawString("LEO", 22, HUD_Y + 9, 2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(O_WHITE, O_BG);
    tft.drawString("420KM 7.66KM/S", 42, HUD_Y + 9, 1);
  }

  void drawCount() {
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(O_AMBER, O_BG);
    tft.setTextPadding(tft.textWidth("SUNRISE 88:88", 1));
    tft.drawString(countText[0] ? countText : "SUN --:--", 230, HUD_Y + 9, 1);
    tft.setTextPadding(0);
  }

  void drawIcon(CnWeather w, int x, int y) {
    auto cloud = [&](uint16_t c) {
      tft.fillCircle(x - 3, y, 3, c);
      tft.fillCircle(x + 1, y - 2, 4, c);
      tft.fillCircle(x + 5, y + 1, 3, c);
      tft.fillRect(x - 3, y + 1, 9, 3, c);
    };
    switch (w) {
      case CNW_SUNNY:
        tft.fillCircle(x, y, 4, O_SUN);
        for (int i = 0; i < 8; i++) {
          float an = i * 45 * DEG;
          int c6x = x + (int)roundf(cosf(an) * 6), c6y = y + (int)roundf(sinf(an) * 6);
          int c8x = x + (int)roundf(cosf(an) * 8), c8y = y + (int)roundf(sinf(an) * 8);
          tft.drawLine(c6x, c6y, c8x, c8y, O_SUN);
        }
        break;
      case CNW_CLOUD:
        tft.fillCircle(x - 3, y - 4, 4, O_SUN);
        cloud(O_CLOUD);
        break;
      case CNW_OVERCAST:
        tft.fillCircle(x - 4, y - 4, 3, O_CLOUDDK);
        tft.fillCircle(x, y - 6, 3, O_CLOUDDK);
        cloud(O_CLOUD);
        break;
      case CNW_FOG:
        for (int i = 0; i < 3; i++) tft.drawFastHLine(x - 7 + (i & 1) * 2, y - 4 + i * 4, 14, O_CLOUD);
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
        tft.fillTriangle(x + 2, y + 4, x - 2, y + 8, x + 1, y + 8, O_YELLOW);
        tft.fillTriangle(x + 1, y + 7, x + 3, y + 7, x - 1, y + 11, O_YELLOW);
        break;
    }
  }

  void drawWeather(const FaceState& s) {
    tft.fillRect(179, 20, 52, 29, O_BG);
    tft.setTextDatum(TL_DATUM);
    if (!s.weatherValid) {
      tft.setTextColor(O_COOL);
      tft.drawString("NO SIG", 187, 30, 1);
      return;
    }
    CnWeather w = cnWeather(s.code);
    drawIcon(w, 190, 29);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", s.temp);
    tft.setTextColor(TFT_WHITE);
    int x = 202 + tft.drawString(buf, 202, 21, 2);
    tft.drawCircle(x + 2, 23, 1, TFT_WHITE);
    char u[2] = {s.unit, '\0'};
    tft.drawString(u, x + 5, 21, 2);
    tft.setTextColor(O_COOL);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(cnWeatherLabel(w), 205, 41, 1);
  }
};

static Orbit orbit;
Face* faceOrbit() { return &orbit; }
