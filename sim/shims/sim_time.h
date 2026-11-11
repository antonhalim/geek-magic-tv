// Force-included into the clock sources so the simulator can shift the clock
// (e.g. jump to just before the tram) without touching the face code.
#pragma once
#include <sys/time.h>
#include <time.h>

int sim_gettimeofday(struct timeval* tv, void* tz);
time_t sim_time(time_t* t);
#define gettimeofday(tv, tz) sim_gettimeofday(tv, tz)
#define time(t) sim_time(t)
