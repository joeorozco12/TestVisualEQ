#pragma once
#include <time.h>

// Solar position (NOAA low-precision algorithm, ~0.01 deg). All inputs UTC epoch.
namespace Sun {
  // Sun elevation above horizon in degrees at the given UTC time.
  float elevationDeg(time_t utc, double latDeg, double lonDeg);
  // Next time (after 'from', within 48 h) the elevation crosses 'elevDeg' in the
  // given direction. rising=true -> dawn-type crossing. Returns 0 if none
  // (polar day/night). 60 s scan + bisection; ~2 ms on ESP32.
  time_t nextCrossing(time_t from, double latDeg, double lonDeg, float elevDeg, bool rising);
}
