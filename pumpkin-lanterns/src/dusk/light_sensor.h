#pragma once
#include <Arduino.h>

// Ambient light with EMA smoothing. LDR divider (ADC1) or BH1750 (I2C).
// Reports pseudo-lux for LDR (calibrate thresholds on the bench, see docs).
class LightSensor {
 public:
  void begin();
  void update(uint32_t now);
  bool healthy() const { return healthy_; }
  float lux() const { return lux_; }          // smoothed
  float rawLux() const { return raw_; }       // last sample
  uint16_t rawAdc() const { return adc_; }    // LDR only
 private:
  float readLdrLux();
  bool readBh1750(float& lux);
  float lux_ = -1, raw_ = -1;
  uint16_t adc_ = 0;
  uint32_t next_ = 0, lastGood_ = 0;
  bool healthy_ = false;
};
