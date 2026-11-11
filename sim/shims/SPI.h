// SPI that feeds bytes to the emulated ST7789 (sim/st7789.cpp) instead of wires.
#pragma once
#include "Arduino.h"

#define SPI_MODE0 0
#define SPI_MODE3 3

struct SPISettings {
  SPISettings(uint32_t = 0, uint8_t = 0, uint8_t = 0) {}
};

void st7789Byte(uint8_t b);

class SPIClass {
 public:
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end() {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  void setFrequency(uint32_t) {}
  void setHwCs(bool) {}
  void pins(int8_t, int8_t, int8_t, int8_t) {}
  uint8_t transfer(uint8_t b) {
    st7789Byte(b);
    return 0;
  }
  uint16_t transfer16(uint16_t w) {
    st7789Byte(w >> 8);
    st7789Byte(w & 0xFF);
    return 0;
  }
};
extern SPIClass SPI;
