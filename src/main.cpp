#include <Arduino.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "display.h"
#include "faces/face.h"
#include "net.h"
#include "weather.h"

static Face* face = nullptr;
static FaceState cur;
static FaceState prev;
static uint32_t lastStateAt = 0;
static uint32_t lastTickAt = 0;
static uint8_t appliedBrightness = 255;

// Day or night brightness for the current local hour; only touches the PWM on change.
static void applyBrightness() {
  int hour = 12;
  if (timeSynced()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    hour = t.tm_hour;
  }
  uint8_t want = effectiveBrightness(settings, hour, timeSynced());
  if (want != appliedBrightness) {
    setBrightness(want);
    appliedBrightness = want;
  }
}

void setup() {
  displayBegin();
  configLoad();
  showSplash();
  applyBrightness();  // showSplash forces 60 %
  timeSprite();  // allocate the 10 KB sprite early, before the heap fragments
  netBegin();
  weatherRequestRefresh();
  // faceDirty starts true, so the first loop() selects and draws the face
}

void loop() {
  netLoop();
  weatherLoop();
  uint32_t now = millis();

  if (faceDirty || !face) {
    faceDirty = false;
    if (face) face->end();
    switch (settings.face) {
      case 1: face = faceTerminal(); break;
      case 2: face = faceVitals(); break;
      case 3: face = faceCorpo(); break;
      case 4: face = faceRadar(); break;
      case 5: face = faceChongqing(); break;
      case 6: face = faceHongKong(); break;
      case 7: face = faceShibuya(); break;
      case 8: face = faceOrbit(); break;
      case 9: face = faceNeoLA(); break;
      case 10: face = faceAbyss(); break;
      case 11: face = faceMaglev(); break;
      case 12: face = faceOceanDrive(); break;
      default: face = faceNetrunner();
    }
    face->begin();
    memset(&prev, 0, sizeof(prev));  // zeroed prev forces a full update
    buildFaceState(cur);
    face->update(cur, prev);
    memcpy(&prev, &cur, sizeof(cur));
    lastStateAt = now;
  }

  if (now - lastStateAt >= 50) {  // 50 ms so seconds roll over on time
    lastStateAt = now;
    applyBrightness();
    buildFaceState(cur);
    if (memcmp(&cur, &prev, sizeof(cur)) != 0) {
      face->update(cur, prev);
      memcpy(&prev, &cur, sizeof(cur));
    }
  }

  if (now - lastTickAt >= 50) {
    lastTickAt = now;
    face->tick(now);
  }

  yield();
}
