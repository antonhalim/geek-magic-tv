#include <Arduino.h>
#include <string.h>
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

void setup() {
  displayBegin();
  configLoad();
  showSplash();
  setBrightness(settings.brightness);  // showSplash forces 60 %
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
    face = (settings.face == 1) ? faceTerminal() : faceNetrunner();
    face->begin();
    memset(&prev, 0, sizeof(prev));  // zeroed prev forces a full update
    buildFaceState(cur);
    face->update(cur, prev);
    memcpy(&prev, &cur, sizeof(cur));
    lastStateAt = now;
  }

  if (now - lastStateAt >= 50) {  // 50 ms so seconds roll over on time
    lastStateAt = now;
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
