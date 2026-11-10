#include "display.h"

#define BACKLIGHT_PIN 5

TFT_eSPI tft = TFT_eSPI();

void setBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  analogWriteRange(1023);
  // Backlight is active-low; 0 % = off (pin held high)
  analogWrite(BACKLIGHT_PIN, 1023 - (uint32_t)pct * 1023 / 100);
}

void displayBegin() {
  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBrightness(0);
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG_A);
}

void showSplash() {
  tft.fillScreen(C_BG_A);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_CYAN, C_BG_A);
  tft.drawString("BOOTING //", 120, 100, 4);
  tft.setTextColor(C_MAGENTA, C_BG_A);
  tft.drawString("CYBERCLOCK", 120, 140, 4);
  setBrightness(60);
}

void showMessage(const char* line1, const char* line2) {
  tft.fillScreen(C_BG_A);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_CYAN, C_BG_A);
  tft.drawString(line1, 120, 100, 4);
  tft.setTextColor(C_MAGENTA, C_BG_A);
  tft.drawString(line2, 120, 140, 2);
}
