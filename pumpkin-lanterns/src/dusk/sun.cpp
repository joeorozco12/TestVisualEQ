#include "sun.h"
#include <math.h>

namespace {
  constexpr double D2R = M_PI / 180.0;
  inline double wrap360(double x) { x = fmod(x, 360.0); return x < 0 ? x + 360.0 : x; }
}

float Sun::elevationDeg(time_t utc, double latDeg, double lonDeg) {
  double n = (double)utc / 86400.0 + 2440587.5 - 2451545.0;     // days since J2000.0
  double L = wrap360(280.460 + 0.9856474 * n);                   // mean longitude
  double g = wrap360(357.528 + 0.9856003 * n) * D2R;             // mean anomaly
  double lambda = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * D2R;  // ecliptic longitude
  double eps = (23.439 - 0.0000004 * n) * D2R;                   // obliquity
  double ra = atan2(cos(eps) * sin(lambda), cos(lambda));        // right ascension (rad)
  double dec = asin(sin(eps) * sin(lambda));                     // declination (rad)
  double gmstH = fmod(18.697374558 + 24.06570982441908 * n, 24.0);
  if (gmstH < 0) gmstH += 24.0;
  double lstDeg = wrap360(gmstH * 15.0 + lonDeg);
  double ha = lstDeg * D2R - ra;                                 // hour angle
  double lat = latDeg * D2R;
  double sinEl = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha);
  return (float)(asin(sinEl) / D2R);
}

time_t Sun::nextCrossing(time_t from, double latDeg, double lonDeg, float elevDeg, bool rising) {
  const int step = 60;
  float prev = elevationDeg(from, latDeg, lonDeg);
  for (time_t t = from + step; t <= from + 48 * 3600; t += step) {
    float cur = elevationDeg(t, latDeg, lonDeg);
    bool crossed = rising ? (prev < elevDeg && cur >= elevDeg) : (prev > elevDeg && cur <= elevDeg);
    if (crossed) {
      time_t lo = t - step, hi = t;                                 // bisect to ~1 s
      while (hi - lo > 1) {
        time_t mid = lo + (hi - lo) / 2;
        float em = elevationDeg(mid, latDeg, lonDeg);
        bool before = rising ? em < elevDeg : em > elevDeg;
        if (before) lo = mid; else hi = mid;
      }
      return hi;
    }
    prev = cur;
  }
  return 0;
}
