#include "light_sensor.h"
#include "config.h"
#include "sys/log.h"
#include <Wire.h>
#include <math.h>

void LightSensor::begin() {
#if LIGHT_SENSOR_TYPE == LIGHT_SENSOR_LDR
  analogSetPinAttenuation(LDR_PIN, ADC_11db);     // full 0..3.3 V range
  pinMode(LDR_PIN, INPUT);
  LOGI("light", "LDR on GPIO%d", LDR_PIN);
#elif LIGHT_SENSOR_TYPE == LIGHT_SENSOR_BH1750
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.beginTransmission(BH1750_ADDR); Wire.write(0x01); Wire.endTransmission();  // power on
  Wire.beginTransmission(BH1750_ADDR); Wire.write(0x10);                           // cont. H-res
  if (Wire.endTransmission() != 0) LOGW("light", "BH1750 not responding at 0x%02X", BH1750_ADDR);
  else LOGI("light", "BH1750 ok");
#else
  LOGI("light", "no light sensor configured");
#endif
}

float LightSensor::readLdrLux() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(LDR_PIN);
  mv /= 8;
  adc_ = (uint16_t)mv;
  if (mv < 5) return -1;                                      // open / shorted to GND
  if (mv > 3250) return 1.0e5f;                               // saturated bright (or shorted to 3V3)
  // Divider: LDR from 3V3 to node, LDR_PULLDOWN to GND. R_ldr = Rpd * (3300 - mv) / mv
  float rLdr = (float)LDR_PULLDOWN_OHMS * (3300.0f - mv) / (float)mv;
  // GL5528-ish: R ≈ 10k @ 10 lux, gamma ≈ 0.7  ->  lux ≈ 10 * (10k/R)^(1/0.7)
  return 10.0f * powf(10000.0f / rLdr, 1.43f);
}

bool LightSensor::readBh1750(float& lux) {
  if (Wire.requestFrom((int)BH1750_ADDR, 2) != 2) return false;
  uint16_t raw = (Wire.read() << 8) | Wire.read();
  lux = raw / 1.2f;
  return true;
}

void LightSensor::update(uint32_t now) {
  if ((int32_t)(now - next_) < 0) return;
  next_ = now + LIGHT_SAMPLE_MS;
  float v = -1; bool ok = false;
#if LIGHT_SENSOR_TYPE == LIGHT_SENSOR_LDR
  v = readLdrLux(); ok = v >= 0;
#elif LIGHT_SENSOR_TYPE == LIGHT_SENSOR_BH1750
  ok = readBh1750(v);
#endif
  if (ok) {
    raw_ = v; lastGood_ = now;
    lux_ = lux_ < 0 ? v : lux_ + LIGHT_EMA_ALPHA * (v - lux_);
    if (!healthy_) LOGI("light", "sensor healthy (%.1f lux)", v);
    healthy_ = true;
  } else if (healthy_ && now - lastGood_ > 10000) {
    healthy_ = false; LOGW("light", "sensor unhealthy");
  }
}
