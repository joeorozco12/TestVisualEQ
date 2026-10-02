#pragma once
#include <Arduino.h>
#include "light_sensor.h"

// Decides whether the lantern should be lit.
//   Source priority: SUN (needs valid clock) > LIGHT (needs healthy sensor) > FALLBACK (config)
//   Manual override via setMode(): FORCE_ON / FORCE_OFF / AUTO
class DuskDawn {
 public:
  enum class Mode : uint8_t { AUTO, FORCE_ON, FORCE_OFF };
  enum class Source : uint8_t { NONE, SUN, LIGHT, FALLBACK, MANUAL };

  void begin(LightSensor* light);
  void update(uint32_t now);
  bool lightsOn() const { return on_; }
  Source source() const { return src_; }
  const char* sourceName() const;
  Mode mode() const { return mode_; }
  void setMode(Mode m);
  float sunElevation() const { return elev_; }      // NAN if clock invalid
  time_t nextSunset() const { return nextSet_; }     // 0 if unknown
  time_t nextSunrise() const { return nextRise_; }
 private:
  bool evalSun(uint32_t now);
  bool evalLight(uint32_t now);
  LightSensor* light_ = nullptr;
  Mode mode_ = Mode::AUTO;
  Source src_ = Source::NONE;
  bool on_ = false, sunState_ = false, sunInit_ = false, lightState_ = false, lightInit_ = false;
  float elev_ = NAN;
  uint32_t next_ = 0, eventsAt_ = 0, lightCandSince_ = 0;
  bool lightCand_ = false;
  time_t nextSet_ = 0, nextRise_ = 0;
};
