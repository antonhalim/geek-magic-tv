#pragma once
#include "Arduino.h"

struct SimWiFi {
  int32_t RSSI() { return -58; }
  int32_t channel() { return 6; }
};
extern SimWiFi WiFi;
