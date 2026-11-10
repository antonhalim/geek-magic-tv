#pragma once
#include <Arduino.h>
#include "logic.h"

extern WeatherData weather;
extern uint32_t weatherFetchedAt;

void weatherRequestRefresh();
void weatherLoop();
bool weatherStale();
bool timeSynced();
