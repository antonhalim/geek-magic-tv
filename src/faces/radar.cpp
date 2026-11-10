#include <math.h>
#include <string.h>
#include <sys/time.h>
#include "face.h"
#include "../display.h"
#include "../weather.h"

// Screen colours (16-bit)
#define R_BG 0x0041     // #02090B
#define R_CYAN 0x073F   // #00E5FF
#define R_TEAL 0x0C52   // #0F8A96
#define R_AMBER 0xFD80  // #FFB000
#define R_DIM 0x09C8    // #0C3A40

// Scope palette (4-bit sprite indices)
enum {
  P_BG, P_RING, P_TEAL, P_GLOW1, P_GLOW2, P_GLOW3, P_BEAM, P_CYAN,
  P_AMBER, P_RED, P_RAIN1, P_RAIN2, P_RAIN3, P_WEDGE, P_AMBERDIM, P_CYANDIM
};
static const uint16_t PALETTE[16] = {
    R_BG,   R_DIM,  R_TEAL, 0x0209, 0x0B2E, 0x04B4, 0x7FBF, R_CYAN,
    R_AMBER, 0xF965, 0x1225, 0x5BC3, 0xA4C3, 0x00E4, 0x49A0, 0x024A};

// Layout
static const int CX = 120, CY = 132, R = 84;   // scope centre/radius on screen
static const int SPR = 170, SC = SPR / 2;      // sprite size and centre
static const int SX = CX - SC, SY = CY - SC;   // sprite position on screen
static const uint32_t SWEEP_MS = 4000;         // one turn
static const int MAX_CONTACTS = 5;
static const float TWO_PI_F = 6.28318f;

struct Contact {
  float x, y, vx, vy;  // px from centre, px/s
  int16_t tx[4], ty[4];
  uint8_t id;
  bool hostile, alive;
};

static float frand(float a, float b) { return a + (b - a) * (random(10001) / 10000.0f); }

class Radar : public Face {
 public:
  void begin() override {
    timeSprite().deleteSprite();  // the scope needs that RAM; other faces re-create it
    scope.setColorDepth(4);
    memOk = scope.createSprite(SPR, SPR) != nullptr;
    if (memOk) scope.createPalette(PALETTE);

    tft.fillScreen(R_BG);
    drawTicks(nullptr);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(R_TEAL);
    tft.drawString("N", CX, CY - R - 13, 1);
    if (!memOk) {
      tft.setTextColor(R_AMBER);
      tft.drawString("SCOPE OFFLINE // LOW MEM", CX, CY, 1);
    }
    for (int i = 0; i < MAX_CONTACTS; i++) {
      c[i].alive = false;
      if (i < 4) spawn(c[i], true);
    }
    rainX = frand(-30, 30);
    rainY = frand(-30, 30);
    lastMs = millis();
    lastTrailAt = lastMs;
    shownCount = -1;
    windMarkDir = -1;
    timeStr[0] = '\0';
  }

  void end() override { scope.deleteSprite(); }

  void update(const FaceState& s, const FaceState& prev) override {
    raining = !strcmp(s.outlook, "RAIN") || !strcmp(s.outlook, "STORM");
    windDir = s.windDir;
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(R_CYAN, R_BG);
      tft.setTextPadding(tft.textWidth("88:88:88", 4));
      tft.drawString(timeStr, 8, 2, 4);
      tft.setTextPadding(0);
    }
    if (strcmp(s.date, prev.date) != 0) memcpy(date, s.date, sizeof(date));
    if (weatherChanged(s, prev) || strcmp(s.city, prev.city) != 0) drawWeather(s);
  }

  void tick(uint32_t ms) override {
    float dt = (ms - lastMs) / 1000.0f;
    lastMs = ms;
    if (dt > 0.5f) dt = 0.5f;
    drawDateLine(ms);
    simulate(ms, dt);
    if (memOk) render(ms);
  }

 private:
  TFT_eSprite scope = TFT_eSprite(&tft);
  bool memOk = false;
  Contact c[MAX_CONTACTS];
  uint32_t lastMs = 0, lastTrailAt = 0;
  float rainX = 0, rainY = 0;
  bool raining = false;
  int windDir = -1, windMarkDir = -1;
  int shownCount = -1;
  char timeStr[9] = "";
  char date[16] = "";

  // ---- simulation ----

  void spawn(Contact& k, bool anywhere) {
    float a = frand(0, TWO_PI_F);
    float r = anywhere ? frand(20, 70) : 80;
    k.x = r * cosf(a);
    k.y = r * sinf(a);
    float head = anywhere ? frand(0, TWO_PI_F) : a + 3.14159f + frand(-0.6f, 0.6f);
    float sp = frand(2.5f, 7.0f);
    k.vx = sp * cosf(head);
    k.vy = sp * sinf(head);
    for (int i = 0; i < 4; i++) { k.tx[i] = (int16_t)k.x; k.ty[i] = (int16_t)k.y; }
    k.id = (uint8_t)random(1, 100);
    k.hostile = random(6) == 0;
    k.alive = true;
  }

  void simulate(uint32_t ms, float dt) {
    bool trail = ms - lastTrailAt >= 700;
    if (trail) lastTrailAt = ms;
    int alive = 0;
    for (Contact& k : c) {
      if (!k.alive) continue;
      k.x += k.vx * dt;
      k.y += k.vy * dt;
      if (trail) {
        for (int i = 3; i > 0; i--) { k.tx[i] = k.tx[i - 1]; k.ty[i] = k.ty[i - 1]; }
        k.tx[0] = (int16_t)k.x;
        k.ty[0] = (int16_t)k.y;
      }
      if (k.x * k.x + k.y * k.y > 82.0f * 82.0f) k.alive = false;
      else alive++;
    }
    // keep 3-5 contacts: new ones enter from the rim now and then
    if (alive < 3 || (alive < MAX_CONTACTS && random(400) == 0))
      for (Contact& k : c)
        if (!k.alive) { spawn(k, false); break; }

    if (raining && windDir >= 0) {  // rain drifts downwind
      float b = (windDir + 180) * TWO_PI_F / 360.0f;
      rainX += 2.0f * sinf(b) * dt;
      rainY -= 2.0f * cosf(b) * dt;
      if (rainX * rainX + rainY * rainY > 70.0f * 70.0f) {
        float from = windDir * TWO_PI_F / 360.0f;  // re-enter upwind
        rainX = 55 * sinf(from);
        rainY = -55 * cosf(from);
      }
    }
  }

  // ---- drawing ----

  // Rim ticks every 10 degrees. Drawn on the screen once and into the scope each frame,
  // because the scope sprite's corners overlap the rim on the diagonals.
  void drawTicks(TFT_eSprite* spr) {
    for (int d = 0; d < 360; d += 10) {
      float a = d * TWO_PI_F / 360.0f;
      int r0 = (d % 90 == 0) ? 85 : 87;
      int x0 = (int)(r0 * sinf(a)), y0 = (int)(-r0 * cosf(a));
      int x1 = (int)(90 * sinf(a)), y1 = (int)(-90 * cosf(a));
      if (spr) spr->drawLine(SC + x0, SC + y0, SC + x1, SC + y1, d % 90 == 0 ? P_TEAL : P_RING);
      else tft.drawLine(CX + x0, CY + y0, CX + x1, CY + y1, d % 90 == 0 ? R_TEAL : R_DIM);
    }
  }

  // Amber triangle on the rim pointing inward from where the wind comes.
  void windMark(TFT_eSprite* spr, int dir, bool erase) {
    float a = dir * TWO_PI_F / 360.0f, sa = sinf(a), ca = -cosf(a);
    float px = -ca, py = sa;  // perpendicular
    int x0 = (int)(92 * sa), y0 = (int)(92 * ca);
    int x1 = (int)(99 * sa + 4 * px), y1 = (int)(99 * ca + 4 * py);
    int x2 = (int)(99 * sa - 4 * px), y2 = (int)(99 * ca - 4 * py);
    if (spr) spr->fillTriangle(SC + x0, SC + y0, SC + x1, SC + y1, SC + x2, SC + y2, P_AMBER);
    else tft.fillTriangle(CX + x0, CY + y0, CX + x1, CY + y1, CX + x2, CY + y2, erase ? R_BG : R_AMBER);
  }

  void render(uint32_t ms) {
    TFT_eSprite& g = scope;
    g.fillSprite(P_BG);
    drawTicks(&g);
    if (windDir >= 0) windMark(&g, windDir, false);
    g.drawCircle(SC, SC, R, P_TEAL);
    g.drawCircle(SC, SC, 56, P_RING);
    g.drawCircle(SC, SC, 28, P_RING);
    g.drawFastHLine(SC - R, SC, 2 * R, P_RING);
    g.drawFastVLine(SC, SC - R, 2 * R, P_RING);
    g.setTextDatum(TL_DATUM);
    g.setTextColor(P_RING);
    g.drawString("1km", SC + 30, SC - 9, 1);
    g.drawString("2km", SC + 58, SC - 9, 1);

    if (raining) {  // rain clutter blob, drifting with the wind
      static const int8_t OFF[6][3] = {{0, 0, 9}, {11, 7, 8}, {-6, 11, 7}, {6, 13, 6}, {-10, 2, 6}, {4, -8, 6}};
      int rx = SC + (int)rainX, ry = SC + (int)rainY;
      for (auto& o : OFF) g.fillCircle(rx + o[0], ry + o[1], o[2], P_RAIN1);
      g.fillCircle(rx + 3, ry + 5, 4, P_RAIN2);
      g.fillCircle(rx + 1, ry + 6, 2, P_RAIN3);
    }

    // sweep: faint wedge plus a fading afterglow behind the beam
    float sw = -1.5708f + TWO_PI_F * (ms % SWEEP_MS) / SWEEP_MS;
    for (int i = 0; i < 6; i++) {
      float a0 = sw - 0.42f + i * 0.07f, a1 = a0 + 0.07f;
      g.fillTriangle(SC, SC, SC + (int)(R * cosf(a0)), SC + (int)(R * sinf(a0)),
                     SC + (int)(R * cosf(a1)), SC + (int)(R * sinf(a1)), P_WEDGE);
    }
    static const uint8_t GLOW[4] = {P_GLOW1, P_GLOW2, P_GLOW3, P_BEAM};
    for (int i = 0; i < 4; i++) {
      float a = sw - 0.30f + i * 0.10f;
      g.drawLine(SC, SC, SC + (int)(R * cosf(a)), SC + (int)(R * sinf(a)), GLOW[i]);
    }

    // contacts: the closest friendly one is locked
    int lock = -1;
    float best = 1e9f;
    int alive = 0;
    for (int i = 0; i < MAX_CONTACTS; i++) {
      if (!c[i].alive) continue;
      alive++;
      float d = c[i].x * c[i].x + c[i].y * c[i].y;
      if (!c[i].hostile && d < best) { best = d; lock = i; }
    }
    for (int i = 0; i < MAX_CONTACTS; i++) {
      if (c[i].alive) drawContact(g, c[i], i == lock, sw, ms);
    }
    g.fillCircle(SC, SC, 2, P_BEAM);
    g.pushSprite(SX, SY);

    if (alive != shownCount) {
      shownCount = alive;
      char buf[16];
      snprintf(buf, sizeof(buf), "%02d CONTACTS", alive);
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(R_TEAL, R_BG);
      tft.setTextPadding(tft.textWidth("88 CONTACTS", 1));
      tft.drawString(buf, 232, 16, 1);
      tft.setTextPadding(0);
    }
  }

  void drawContact(TFT_eSprite& g, const Contact& k, bool locked, float sw, uint32_t ms) {
    int x = SC + (int)k.x, y = SC + (int)k.y;
    // time since the beam passed, as an angle: just swept = bright, then fades
    float d = fmodf(sw - atan2f(k.y, k.x) + 2 * TWO_PI_F, TWO_PI_F);
    uint8_t trail = k.hostile ? P_RED : (locked ? P_AMBERDIM : P_CYANDIM);
    for (int i = 1; i < 4; i++) g.drawPixel(SC + k.tx[i], SC + k.ty[i], trail);
    if (k.hostile) {
      if ((ms / 300) & 1 || d < 0.6f) {
        g.fillTriangle(x, y - 4, x + 4, y + 3, x - 4, y + 3, P_RED);
        g.setTextColor(P_RED);
        g.drawString("HSTL", x + 6, y - 2, 1);
      }
      return;
    }
    uint8_t col = d < 0.5f ? P_BEAM : (d < 3.5f ? (locked ? P_AMBER : P_CYAN) : (locked ? P_AMBERDIM : P_CYANDIM));
    g.fillCircle(x, y, locked ? 2 : 1, col);
    char id[6];
    snprintf(id, sizeof(id), "%c-%02d", locked ? 'K' : 'U', k.id);
    if (locked) {
      for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
          g.drawFastHLine(sx < 0 ? x - 7 : x + 4, y + sy * 7, 3, P_AMBER);
          g.drawFastVLine(x + sx * 7, sy < 0 ? y - 7 : y + 4, 3, P_AMBER);
        }
      g.setTextColor(P_AMBER);
      g.drawString(id, x + 10, y - 8, 1);
      g.drawString("LOCK", x + 10, y + 1, 1);
    } else {
      g.setTextColor(P_TEAL);
      g.drawString(id, x + 5, y - 3, 1);
    }
  }

  void drawDateLine(uint32_t ms) {
    char buf[32], msBuf[6] = ".---";
    if (timeSynced()) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      snprintf(msBuf, sizeof(msBuf), ".%03d", (int)(tv.tv_usec / 1000));
    }
    snprintf(buf, sizeof(buf), "%.10s // %s", date, msBuf);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(R_TEAL, R_BG);
    tft.setTextPadding(130);
    tft.drawString(buf, 8, 30, 1);
    tft.setTextPadding(0);
  }

  void drawWeather(const FaceState& s) {
    // wind readout + rim marker
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(R_AMBER, R_BG);
    tft.setTextPadding(100);
    tft.drawString(s.wind, 232, 4, 1);
    tft.setTextPadding(0);
    if (windMarkDir >= 0) windMark(nullptr, windMarkDir, true);
    if (s.windDir >= 0) windMark(nullptr, s.windDir, false);
    windMarkDir = s.windDir;

    // bottom row: temperature + condition, city
    tft.fillRect(0, 224, 240, 16, R_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(R_CYAN);
    char buf[32];
    if (s.weatherValid) {
      snprintf(buf, sizeof(buf), "%d", s.temp);
      int x = 8 + tft.drawString(buf, 8, 224, 2);
      tft.drawCircle(x + 3, 227, 2, R_CYAN);
      snprintf(buf, sizeof(buf), "%c%s %s", s.unit, s.stale ? "*" : "", s.label);
      tft.drawString(buf, x + 7, 224, 2);
    } else {
      tft.drawString("-- NO SIGNAL", 8, 224, 2);
    }
    char city[48];
    fitLabel(s.city, city, sizeof(city), 84, [](const char* t) { return (int)tft.textWidth(t, 1); });
    tft.setTextDatum(BR_DATUM);
    tft.setTextColor(R_TEAL);
    tft.drawString(city, 232, 238, 1);
  }
};

static Radar radar;
Face* faceRadar() { return &radar; }
