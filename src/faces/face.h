#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../logic.h"

struct FaceState {
  char time[6];
  char date[16];
  char city[48];
  int temp;
  int hi;
  int lo;
  int humidity;
  char unit;
  const char* label;
  int code;             // current WMO weather code, -1 if unknown
  const char* outlook;  // today's tag: STORM, SNOW, RAIN, DRY
  char precip[24];      // "58% TODAY // 0.29in"
  int windDir;          // degrees FROM, -1 if unknown
  char wind[24];        // "WIND SE 9mph"
  bool weatherValid;
  bool stale;
  int second;
};

void buildFaceState(FaceState& s);
int textWidth2(const char* s);  // pixel width in font 2, for fitLabel

class Face {
 public:
  virtual void begin() = 0;
  virtual void update(const FaceState& s, const FaceState& prev) = 0;
  virtual void tick(uint32_t ms) = 0;
  virtual void end() {}  // free face-specific memory before another face starts
  virtual ~Face() {}
};

Face* faceNetrunner();
Face* faceTerminal();
Face* faceVitals();
Face* faceCorpo();
Face* faceRadar();
Face* faceChongqing();
Face* faceHongKong();
Face* faceShibuya();
Face* faceOrbit();

// ---- shared helpers (common.cpp) ----

// Time digits sprite, shared by both faces (only one face is active at a time).
// 8-bit colour, allocated once on first use.
#define TIME_W 224
#define TIME_H 52
TFT_eSprite& timeSprite();
void freeTimeSprite();  // faces that need the RAM call this; timeSprite() re-creates it

// Colour as it appears after an 8-bit sprite round trip, so screen areas
// drawn directly match the sprite background exactly.
uint16_t spriteColor(uint16_t c);

// Glitch timer: idle for a random 4-8 s, then active for 200 ms.
struct Glitch {
  uint32_t nextAt;
  uint32_t endAt;
  bool active;
};
void glitchReset(Glitch& g, uint32_t now);
// Returns +1 when a glitch frame should be drawn, -1 on the frame it ends, 0 otherwise.
int glitchStep(Glitch& g, uint32_t now);

// Push the time sprite in horizontal slices with random x offsets (+-maxOff),
// clearing the uncovered margins with bg.
void pushSliced(int x, int y, int slices, int maxOff, uint16_t bg);
// Clean push plus clearing of the glitch margins.
void pushClean(int x, int y, int maxOff, uint16_t bg);

// Temperature in font 4 with a drawn degree ring, top-left at (x, y), transparent
// background. "--" when weather is invalid, "*" appended when stale. Returns end x.
int drawTemp(int x, int y, const FaceState& s, uint16_t fg);

bool weatherChanged(const FaceState& a, const FaceState& b);
