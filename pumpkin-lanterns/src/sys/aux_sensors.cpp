#include "aux_sensors.h"
#include "config.h"
#include "sys/log.h"

namespace {
  [[maybe_unused]] float micDc = 2048, micEnv = 0, micFloor = 30, micPeak = 300;
  float micLevel = 0;
  [[maybe_unused]] uint32_t micNext = 0, vbusNext = 0, pirLogAt = 0;
  uint16_t vbus = 0; bool pirState = false;
}

void AuxSensors::begin() {
#if MIC_ENABLED
  analogSetPinAttenuation(MIC_PIN, ADC_11db); pinMode(MIC_PIN, INPUT);
  LOGI("aux", "mic on GPIO%d", MIC_PIN);
#endif
#if PIR_ENABLED
  pinMode(PIR_PIN, INPUT_PULLDOWN);
  LOGI("aux", "PIR on GPIO%d", PIR_PIN);
#endif
#if VBUS_SENSE_ENABLED
  analogSetPinAttenuation(VBUS_SENSE_PIN, ADC_11db); pinMode(VBUS_SENSE_PIN, INPUT);
  LOGI("aux", "VBUS sense on GPIO%d (x%.1f)", VBUS_SENSE_PIN, VBUS_DIVIDER);
#endif
}

void AuxSensors::update(uint32_t now) {
#if MIC_ENABLED
  if ((int32_t)(now - micNext) >= 0) {                 // ~1 kHz envelope follower
    micNext = now + 1;
    int raw = analogRead(MIC_PIN);
    micDc += 0.005f * (raw - micDc);
    float a = fabsf(raw - micDc);
    micEnv += (a > micEnv ? MIC_ATTACK : MIC_DECAY) * (a - micEnv);
    micFloor += MIC_FLOOR_ADAPT * (micEnv - micFloor);               // slow noise-floor tracker
    if (micEnv > micPeak) micPeak = micEnv; else micPeak -= 0.0005f * (micPeak - micFloor * 3);
    float span = micPeak - micFloor * 1.5f;
    micLevel = span > 20 ? constrain((micEnv - micFloor * 1.5f) / span, 0.0f, 1.0f) : 0;
  }
#endif
#if PIR_ENABLED
  bool p = digitalRead(PIR_PIN);
  if (p != pirState && now - pirLogAt > 2000) { pirLogAt = now; LOGD("aux", "PIR %s", p ? "HIGH" : "low"); }
  pirState = p;
#endif
#if VBUS_SENSE_ENABLED
  if ((int32_t)(now - vbusNext) >= 0) {
    vbusNext = now + 2000;
    uint32_t mv = 0; for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(VBUS_SENSE_PIN);
    uint16_t v = (uint16_t)((mv / 8) * VBUS_DIVIDER);
    if (vbus && v < VBUS_DIM_MV && vbus >= VBUS_DIM_MV) LOGW("aux", "VBUS sag: %u mV", v);
    vbus = v;
  }
#endif
}

float AuxSensors::mic() { return micLevel * MIC_GAIN > 1.0f ? 1.0f : micLevel * MIC_GAIN; }
bool AuxSensors::pir() { return pirState; }
uint16_t AuxSensors::vbusMv() { return vbus; }
float AuxSensors::vbusBrightnessFactor() {
#if VBUS_SENSE_ENABLED
  if (vbus == 0) return 1.0f;
  if (vbus < VBUS_CRIT_MV) return 0.3f;
  if (vbus < VBUS_DIM_MV) return 0.6f;
#endif
  return 1.0f;
}
