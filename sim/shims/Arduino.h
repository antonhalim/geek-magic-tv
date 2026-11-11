// Just enough of the Arduino/ESP8266 API to build TFT_eSPI and the faces on a desktop.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <type_traits>

typedef bool boolean;
typedef uint8_t byte;

#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
// TFT_eSPI reads pointers with pgm_read_dword; on a 64-bit desktop they need all 64 bits.
template <class T> inline uintptr_t pgm_read_dword_(const T* a) { return (uintptr_t)*a; }
inline uintptr_t pgm_read_dword_(const void* a) { return *(const uintptr_t*)a; }
#define pgm_read_dword(a) pgm_read_dword_(a)
#define pgm_read_ptr(a) (*(void* const*)(a))
class __FlashStringHelper;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define MSBFIRST 1
#define LSBFIRST 0
#ifndef PI
#define PI 3.14159265358979323846
#endif
#define DEG_TO_RAD 0.017453292519943295
#define RAD_TO_DEG 57.29577951308232

// By value: returning decltype(a < b ? a : b) would be a reference to a parameter.
template <class A, class B> inline typename std::common_type<A, B>::type min(A a, B b) { return a < b ? a : b; }
template <class A, class B> inline typename std::common_type<A, B>::type max(A a, B b) { return a > b ? a : b; }
template <class T, class L, class H> inline T constrain(T x, L lo, H hi) { return x < lo ? lo : (x > hi ? hi : x); }
#define radians(d) ((d) * DEG_TO_RAD)
#define degrees(r) ((r) * RAD_TO_DEG)
#define sq(x) ((x) * (x))
#define bitRead(v, b) (((v) >> (b)) & 1)

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
inline void yield() {}
long random(long hi);
long random(long lo, long hi);
void randomSeed(unsigned long s);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
inline int digitalRead(uint8_t) { return 0; }
inline void analogWrite(uint8_t, int) {}
inline void analogWriteRange(uint32_t) {}
inline uint32_t digitalPinToBitMask(int pin) { return 1u << (pin & 31); }
inline char* ltoa(long v, char* buf, int base) {
  if (base == 10) snprintf(buf, 24, "%ld", v);
  else snprintf(buf, 24, "%lx", v);
  return buf;
}

class String {
 public:
  String(const char* s = "") : s_(s ? s : "") {}
  unsigned int length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  bool operator==(const char* o) const { return s_ == o; }
  void toCharArray(char* buf, unsigned int n) const {
    if (!n) return;
    strncpy(buf, s_.c_str(), n - 1);
    buf[n - 1] = '\0';
  }
 private:
  std::string s_;
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) {
    size_t k = 0;
    while (n--) k += write(*b++);
    return k;
  }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
};

struct SimEsp {
  uint32_t getFreeHeap() { return 28 * 1024; }
  uint32_t getChipId() { return 0x5A1234; }
  uint8_t getHeapFragmentation() { return 9; }
};
extern SimEsp ESP;
