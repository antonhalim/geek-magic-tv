#pragma once
#include <Arduino.h>

extern volatile bool faceDirty;

void netBegin();
void netLoop();
