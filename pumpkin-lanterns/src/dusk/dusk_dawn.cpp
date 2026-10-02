#include "dusk_dawn.h"
#include "config.h"
#include "sun.h"
#include "timekeeper.h"
#include "sys/log.h"

void DuskDawn::begin(LightSensor* light) {
  light_ = light;
  on_ = FALLBACK_LIGHTS_ON; src_ = Source::FALLBACK;
}

const char* DuskDawn::sourceName() const {
  switch (src_) {
    case Source::SUN: return "sun";
    case Source::LIGHT: return "light";
    case Source::FALLBACK: return "fallback";
    case Source::MANUAL: return "manual";
    default: return "none";
  }
}

void DuskDawn::setMode(Mode m) {
  if (m == mode_) return;  // sync beacons repeat the mode every few seconds; ignore no-ops
  mode_ = m; next_ = 0;    // re-evaluate immediately
  LOGI("dusk", "mode=%s", m == Mode::AUTO ? "auto" : m == Mode::FORCE_ON ? "force_on" : "force_off");
}

// Hysteresis on sun elevation: ON below SUN_ON_ELEVATION_DEG, OFF above SUN_OFF_ELEVATION_DEG.
bool DuskDawn::evalSun(uint32_t now) {
  time_t t = Timekeeper::nowUtc();
  elev_ = Sun::elevationDeg(t, GEO_LAT, GEO_LON);
  if (!sunInit_) {
    sunState_ = elev_ < (SUN_ON_ELEVATION_DEG + SUN_OFF_ELEVATION_DEG) / 2.0f;
    sunInit_ = true;
  } else if (elev_ < SUN_ON_ELEVATION_DEG) sunState_ = true;
  else if (elev_ > SUN_OFF_ELEVATION_DEG) sunState_ = false;
  // Refresh next-event times every 10 min (cheap, but no need to do it every tick)
  if (eventsAt_ == 0 || now - eventsAt_ > 600000) {
    eventsAt_ = now;
    nextSet_ = Sun::nextCrossing(t, GEO_LAT, GEO_LON, SUN_ON_ELEVATION_DEG, false);
    nextRise_ = Sun::nextCrossing(t, GEO_LAT, GEO_LON, SUN_OFF_ELEVATION_DEG, true);
    struct tm a, b; char sa[16] = "-", sb[16] = "-";
    if (nextSet_) { localtime_r(&nextSet_, &a); strftime(sa, sizeof(sa), "%a %H:%M", &a); }
    if (nextRise_) { localtime_r(&nextRise_, &b); strftime(sb, sizeof(sb), "%a %H:%M", &b); }
    LOGI("dusk", "sun el=%.1f deg -> %s; next on %s, next off %s", elev_, sunState_ ? "ON" : "OFF", sa, sb);
  }
  return sunState_;
}

// Hysteresis + debounce on lux: a candidate state must persist LIGHT_DEBOUNCE_MS
// (car headlights / lightning / a hand over the sensor won't flip it).
bool DuskDawn::evalLight(uint32_t now) {
  float lux = light_->lux();
  bool cand = lightState_;
  if (lux < LIGHT_DARK_ON_LUX) cand = true;
  else if (lux > LIGHT_DARK_OFF_LUX) cand = false;
  if (!lightInit_) { lightState_ = lux < (LIGHT_DARK_ON_LUX + LIGHT_DARK_OFF_LUX) / 2; lightInit_ = true; lightCand_ = lightState_; lightCandSince_ = now; }
  if (cand != lightCand_) { lightCand_ = cand; lightCandSince_ = now; }
  if (lightCand_ != lightState_ && now - lightCandSince_ >= LIGHT_DEBOUNCE_MS) {
    lightState_ = lightCand_;
    LOGI("dusk", "light sensor: %.1f lux -> %s", lux, lightState_ ? "ON" : "OFF");
  }
  return lightState_;
}

void DuskDawn::update(uint32_t now) {
  if ((int32_t)(now - next_) < 0) return;
  next_ = now + SCHEDULE_EVAL_MS;
  bool prevOn = on_; Source prevSrc = src_;

  if (mode_ == Mode::FORCE_ON)       { on_ = true;  src_ = Source::MANUAL; }
  else if (mode_ == Mode::FORCE_OFF) { on_ = false; src_ = Source::MANUAL; }
  else if (Timekeeper::valid())      { on_ = evalSun(now); src_ = Source::SUN;
                                       if (light_ && light_->healthy()) evalLight(now); } // keep light state warm
  else if (light_ && light_->healthy()) { on_ = evalLight(now); src_ = Source::LIGHT; elev_ = NAN; }
  else                               { on_ = FALLBACK_LIGHTS_ON; src_ = Source::FALLBACK; elev_ = NAN; }

  if (on_ != prevOn || src_ != prevSrc)
    LOGI("dusk", "lights %s (source=%s)", on_ ? "ON" : "OFF", sourceName());
}
