// BENCH: ultrasonic. Prints raw/filtered distance, presence FSM, triggers, reactivity. LEDs mirror reactivity.
#include <Arduino.h>
#include "config.h"
#include "sys/log.h"
#include "proximity/ultrasonic.h"
#include "effects/engine.h"

static Ultrasonic us; static EffectEngine engine; static uint32_t nextPrint = 0;

void setup() {
  Serial.begin(SERIAL_BAUD); delay(300);
  engine.begin(); engine.setEffect("wakeup", false);
  us.begin();
  Serial.println("Walk toward/away from the sensor. Expect: stable cm, TRIGGER once, cooldown, release past hysteresis.");
}

void loop() {
  uint32_t now = millis();
  us.update(now);
  if (us.takeTrigger()) Serial.println(">>> TRIGGER (scare would fire here)");
  engine.setReactivity(us.reactivity());
  engine.update(now);
  if (now >= nextPrint) {
    nextPrint = now + 250;
    Serial.printf("raw=%6.1f filt=%6.1f presence=%d react=%.2f healthy=%d cooldown=%lu\n",
                  us.rawCm(), us.distanceCm(), us.presence(), us.reactivity(), us.healthy(), (unsigned long)us.cooldownRemainingMs(now));
  }
}
