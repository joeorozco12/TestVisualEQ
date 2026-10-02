#pragma once
#include <Arduino.h>

// Optional extras, each gated by config: microphone envelope (ADC1), PIR (digital), VBUS sense (ADC1).
namespace AuxSensors {
  void begin();
  void update(uint32_t now);
  float mic();            // 0..1 normalised sound envelope (0 when disabled)
  bool pir();             // true while PIR output is high (false when disabled)
  uint16_t vbusMv();      // 0 when disabled
  float vbusBrightnessFactor();   // 1.0 normally; 0.6 below VBUS_DIM_MV; 0.3 below VBUS_CRIT_MV
}
