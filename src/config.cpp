#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>

Settings settings = defaultSettings();

void configLoad() {
  settings = defaultSettings();
  if (!LittleFS.begin()) {
    LittleFS.format();
    if (!LittleFS.begin()) return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  String body = f.readString();
  f.close();
  Settings s;
  if (parseSettings(body.c_str(), s)) settings = s;
}

bool configSave() {
  char buf[256];
  size_t n = serializeSettings(settings, buf, sizeof(buf));
  if (n == 0) return false;
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) return false;
  size_t w = f.write((const uint8_t*)buf, n);
  f.close();
  if (w != n) {
    LittleFS.remove("/config.tmp");
    return false;
  }
  if (LittleFS.rename("/config.tmp", "/config.json")) return true;
  LittleFS.remove("/config.tmp");
  return false;
}
