#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// RGB565 palette
#define C_BG_A 0x0021      // #040A0A-ish near-black
#define C_BG_B 0x0040      // #050A06
#define C_CYAN 0x079F      // #00F0FF
#define C_MAGENTA 0xF95A   // #FF2BD6
#define C_YELLOW 0xFF61    // #FCEE0A
#define C_AMBER 0xFD80     // #FFB000
#define C_GREEN 0x3FED     // #39FF6A
#define C_DIMGREEN 0x19C4  // #1F3A24

extern TFT_eSPI tft;

void displayBegin();
void setBrightness(uint8_t pct);
void showSplash();
void showMessage(const char* line1, const char* line2);
