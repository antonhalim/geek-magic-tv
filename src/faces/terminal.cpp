#include <ESP8266WiFi.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "face.h"
#include "../display.h"
#include "../weather.h"

#define C_LOGDIM 0x1BC6  // #1F7A34, older output lines

// Layout (240x240)
static const int TX = (240 - TIME_W) / 2, TY = 38;  // time sprite (HH:MM:SS)
static const int GLITCH_OFF = 4;
static const int DATE_Y = 98, MS_X = 226;
static const int DIV_Y = 110;
static const int PANE_X = 16, PANE_Y = 115, PANE_W = 208, LINE_H = 11, LINES = 5;
static const int LINE_LEN = 36;  // PANE_W / 6 px per GLCD char, plus NUL
static const char* const PROMPT = "root@cc:~# ";

enum ShellState { SH_PROMPT, SH_TYPING, SH_OUTPUT };

class Terminal : public Face {
 public:
  void begin() override {
    bg = spriteColor(C_BG_B);
    grid = spriteColor(C_DIMGREEN);  // same colour inside and outside the time sprite
    tft.fillScreen(bg);
    for (int g = 60; g < 240; g += 60) {
      tft.drawFastVLine(g, 0, 240, grid);
      tft.drawFastHLine(0, g, 240, grid);
    }
    // amber 2px corner brackets, 20px arms, 8px inset
    tft.fillRect(8, 8, 20, 2, C_AMBER);
    tft.fillRect(8, 8, 2, 20, C_AMBER);
    tft.fillRect(212, 8, 20, 2, C_AMBER);
    tft.fillRect(230, 8, 2, 20, C_AMBER);
    tft.fillRect(8, 230, 20, 2, C_AMBER);
    tft.fillRect(8, 212, 2, 20, C_AMBER);
    tft.fillRect(212, 230, 20, 2, C_AMBER);
    tft.fillRect(230, 212, 2, 20, C_AMBER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_GREEN);
    tft.drawString("> CHRONO.SYNC OK", 20, 14, 2);
    for (int x = PANE_X; x < PANE_X + PANE_W; x += 6) tft.drawFastHLine(x, DIV_Y, 3, C_LOGDIM);
    tft.fillRect(PANE_X - 2, PANE_Y - 2, PANE_W + 4, LINES * LINE_H + 1, bg);  // no grid behind the shell

    timeStr[0] = '\0';
    memset(lines, 0, sizeof(lines));
    cmdIdx = 0;
    newPrompt(millis());
    glitchReset(glitch, millis());
  }

  void update(const FaceState& s, const FaceState& prev) override {
    last = s;
    if (strcmp(s.time, prev.time) != 0 || s.second != prev.second) {
      if (s.time[0] == '-') strcpy(timeStr, "--:--:--");
      else snprintf(timeStr, sizeof(timeStr), "%s:%02d", s.time, s.second);
      renderTime();
      if (!glitch.active) pushClean(TX, TY, GLITCH_OFF, bg);
    }
    if (strcmp(s.date, prev.date) != 0) drawDate(s);
    if (strcmp(s.city, prev.city) != 0) {
      clearRect(20, 172, 200, 16);
      char city[48], buf[64];
      fitLabel(s.city, city, sizeof(city), 200 - tft.textWidth("> ATMOS.", 2), textWidth2);
      snprintf(buf, sizeof(buf), "> ATMOS.%s", city);
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_AMBER);
      tft.drawString(buf, 20, 172, 2);
    }
    if (weatherChanged(s, prev)) drawWeather(s);
  }

  void tick(uint32_t ms) override {
    int g = glitchStep(glitch, ms);
    if (g == 1)
      pushSliced(TX, TY, 2, GLITCH_OFF, bg);
    else if (g == -1)
      pushClean(TX, TY, GLITCH_OFF, bg);
    drawMillis();
    runShell(ms);
  }

 private:
  uint16_t bg = 0;
  uint16_t grid = 0;
  char timeStr[9] = "";
  Glitch glitch = {0, 0, false};
  FaceState last = {};

  // Shell pane: bottom line is the live one; everything scrolls up.
  char lines[LINES][LINE_LEN];
  uint16_t colors[LINES] = {};
  ShellState state = SH_PROMPT;
  uint32_t nextAt = 0;
  int cmdIdx = 0;
  char cmd[LINE_LEN] = "";
  int typed = 0;
  char out[3][LINE_LEN];
  uint16_t outColor[3] = {};
  int nOut = 0, outIdx = 0;
  bool cursorOn = false;

  // Clear to background and restore any grid lines crossing the rect.
  void clearRect(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, bg);
    for (int g = 60; g < 240; g += 60) {
      if (g >= x && g < x + w) tft.drawFastVLine(g, y, h, grid);
      if (g >= y && g < y + h) tft.drawFastHLine(x, g, w, grid);
    }
  }

  void renderTime() {
    TFT_eSprite& spr = timeSprite();
    spr.fillSprite(bg);
    for (int g = 60; g < 240; g += 60) spr.drawFastVLine(g - TX, 0, TIME_H, grid);
    spr.drawFastHLine(0, 60 - TY, TIME_W, grid);  // grid row crossing the sprite
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(C_AMBER);
    spr.drawString(timeStr, TIME_W / 2, TIME_H / 2, 7);
  }

  void drawDate(const FaceState& s) {
    char buf[48];
    if (timeSynced()) {
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      snprintf(buf, sizeof(buf), "%04d.%02d.%02d // %.3s", t.tm_year + 1900, t.tm_mon + 1,
               t.tm_mday, s.date);
    } else {
      snprintf(buf, sizeof(buf), "----.--.-- // ---");
    }
    clearRect(16, DATE_Y - 8, 160, 16);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_GREEN);
    tft.drawString(buf, 20, DATE_Y, 2);
  }

  void drawMillis() {
    char buf[6];
    if (timeSynced()) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      snprintf(buf, sizeof(buf), ".%03d", (int)(tv.tv_usec / 1000));
    } else {
      strcpy(buf, ".---");
    }
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(C_AMBER, bg);
    tft.setTextPadding(tft.textWidth(".888", 2));
    tft.drawString(buf, MS_X, DATE_Y, 2);
    tft.setTextPadding(0);
  }

  void drawWeather(const FaceState& s) {
    clearRect(20, 192, 201, 34);
    int end = drawTemp(20, 196, s, C_GREEN);

    char row1[32];
    if (s.weatherValid)
      snprintf(row1, sizeof(row1), "%s  %d%%RH", s.label, s.humidity);
    else
      snprintf(row1, sizeof(row1), "%s", s.label);
    // Long labels fall back to the small GLCD font so they never overlap the temperature
    int font = (tft.textWidth(row1, 2) <= 220 - end - 6) ? 2 : 1;
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_GREEN);
    tft.drawString(row1, 220, font == 2 ? 194 : 198, font);

    char row2[20];
    if (s.weatherValid)
      snprintf(row2, sizeof(row2), "H %d  L %d", s.hi, s.lo);
    else
      snprintf(row2, sizeof(row2), "H --  L --");
    tft.setTextColor(C_AMBER);
    tft.drawString(row2, 220, 211, 2);
  }

  // ---- shell pane ----

  void pushLine(const char* text, uint16_t color) {
    for (int i = 0; i < LINES - 1; i++) {
      memcpy(lines[i], lines[i + 1], LINE_LEN);
      colors[i] = (colors[i + 1] == C_GREEN) ? C_LOGDIM : colors[i + 1];  // older prompts fade
    }
    snprintf(lines[LINES - 1], LINE_LEN, "%s", text);
    colors[LINES - 1] = color;
    for (int i = 0; i < LINES; i++) drawLine(i, false);
  }

  void drawLine(int i, bool cursor) {
    int y = PANE_Y + i * LINE_H;
    tft.fillRect(PANE_X, y, PANE_W, 8, bg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(colors[i]);
    int w = tft.drawString(lines[i], PANE_X, y, 1);
    if (cursor) tft.fillRect(PANE_X + w, y, 5, 8, C_GREEN);
  }

  void newPrompt(uint32_t now) {
    pushLine(PROMPT, C_GREEN);
    state = SH_PROMPT;
    nextAt = now + 1200 + random(800);
  }

  // The looping script. Outputs use live values where there are any.
  void prepareCommand() {
    char place[12] = "";
    int n = 0;
    for (const char* p = last.city; *p && *p != ',' && n < 10; p++, n++)
      place[n] = (*p == ' ') ? '-' : (char)tolower(*p);
    place[n] = '\0';

    nOut = 1;
    outColor[0] = C_LOGDIM;
    switch (cmdIdx) {
      case 0: {
        uint32_t s = millis() / 1000;
        strcpy(cmd, "uptime");
        if (s < 3600) snprintf(out[0], LINE_LEN, "up %um%02us", s / 60, s % 60);
        else snprintf(out[0], LINE_LEN, "up %uh%02um", s / 3600, (s / 60) % 60);
        break;
      }
      case 1:
        strcpy(cmd, "iwconfig wlan0");
        snprintf(out[0], LINE_LEN, "link ok  rssi %ddBm  ch %d", (int)WiFi.RSSI(), (int)WiFi.channel());
        break;
      case 2:
        snprintf(cmd, LINE_LEN, "curl wttr/%s", place[0] ? place : "local");
        if (last.weatherValid)
          snprintf(out[0], LINE_LEN, "%s %d%c %d%%RH", last.label, last.temp, last.unit, last.humidity);
        else
          strcpy(out[0], "curl: (7) no route to host");
        break;
      case 3:
        strcpy(cmd, "free -k");
        snprintf(out[0], LINE_LEN, "heap %uK free  frag %u%%", (unsigned)(ESP.getFreeHeap() / 1024),
                 (unsigned)ESP.getHeapFragmentation());
        break;
      case 4:
        strcpy(cmd, "ping nightcity.net");
        snprintf(out[0], LINE_LEN, "64 bytes: icmp_seq=1 time=%dms", (int)(8 + random(33)));
        break;
      default: {
        strcpy(cmd, "./breach --ice=black");
        int pct = 60 + random(4) * 10;
        char bar[11];
        for (int i = 0; i < 10; i++) bar[i] = (i < pct / 10) ? '#' : '-';
        bar[10] = '\0';
        strcpy(out[0], "bypassing ICE...");
        snprintf(out[1], LINE_LEN, "[%s] %d%%", bar, pct);
        strcpy(out[2], "ACCESS GRANTED");
        outColor[1] = C_LOGDIM;
        outColor[2] = C_AMBER;
        nOut = 3;
        break;
      }
    }
    cmdIdx = (cmdIdx + 1) % 6;
    typed = 0;
  }

  void runShell(uint32_t now) {
    bool blink = (now / 500) & 1;
    if ((int32_t)(now - nextAt) < 0) {
      // idle: only blink the cursor on the live prompt line
      if (state != SH_OUTPUT && blink != cursorOn) {
        cursorOn = blink;
        drawLine(LINES - 1, cursorOn);
      }
      return;
    }
    switch (state) {
      case SH_PROMPT:
        prepareCommand();
        state = SH_TYPING;
        nextAt = now;
        break;
      case SH_TYPING:
        if (cmd[typed]) {
          size_t len = strlen(lines[LINES - 1]);
          if (len < LINE_LEN - 1) {
            lines[LINES - 1][len] = cmd[typed];
            lines[LINES - 1][len + 1] = '\0';
          }
          typed++;
          cursorOn = true;
          drawLine(LINES - 1, true);
          // human-ish rhythm with the occasional hesitation
          nextAt = now + 60 + random(80) + (random(8) == 0 ? 250 : 0);
        } else {
          drawLine(LINES - 1, false);  // "enter"
          cursorOn = false;
          state = SH_OUTPUT;
          outIdx = 0;
          nextAt = now + 350;
        }
        break;
      case SH_OUTPUT:
        if (outIdx < nOut) {
          pushLine(out[outIdx], outColor[outIdx]);
          outIdx++;
          nextAt = now + 220;
        } else {
          newPrompt(now);
        }
        break;
    }
  }
};

static Terminal terminal;
Face* faceTerminal() { return &terminal; }
