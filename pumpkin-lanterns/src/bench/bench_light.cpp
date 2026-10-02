// BENCH: ambient light sensor + hysteresis/debounce. Cover/uncover the sensor; watch state flip only after LIGHT_DEBOUNCE_MS.
// Use the printed lux at real dusk to set LIGHT_DARK_ON_LUX / LIGHT_DARK_OFF_LUX.
#include <Arduino.h>
#include "config.h"
#include "sys/log.h"
#include "dusk/light_sensor.h"
#include "dusk/dusk_dawn.h"

static LightSensor light; static DuskDawn dusk; static uint32_t nextPrint = 0;

void setup() {
  Serial.begin(SERIAL_BAUD); delay(300);
  light.begin(); dusk.begin(&light);
  Serial.printf("thresholds: on<%.0f off>%.0f debounce=%d ms (no NTP here, so source should be 'light')\n", LIGHT_DARK_ON_LUX, LIGHT_DARK_OFF_LUX, LIGHT_DEBOUNCE_MS);
}

void loop() {
  uint32_t now = millis();
  light.update(now); dusk.update(now);
  if (now >= nextPrint) {
    nextPrint = now + 1000;
    Serial.printf("adc=%4u mV raw=%8.1f lux ema=%8.1f healthy=%d | lights=%s source=%s\n",
                  light.rawAdc(), light.rawLux(), light.lux(), light.healthy(), dusk.lightsOn() ? "ON " : "OFF", dusk.sourceName());
  }
}
