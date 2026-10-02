// BENCH: power bank keep-alive + current budget. Run on the bank with an inline USB meter.
// Phase A (60 s): LEDs black, WiFi on  -> does the bank stay up? note current.
// Phase B (120 s): keep-alive pulses   -> does the bank stay up?
// Phase C: full-white at cap            -> peak current for the budget table.
// Repeats. Status LED blinks the phase. If the bank drops you'll see a reboot (reset reason printed).
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "secrets.h"
#include "sys/log.h"
#include "sys/node.h"
#include "effects/engine.h"

static EffectEngine engine; static uint32_t phaseStart = 0, nextPrint = 0; static char phase = 'A';

void setup() {
  Serial.begin(SERIAL_BAUD); delay(300);
  Node::begin(); engine.begin(); engine.setEffect("off", false);
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("reset reason: %s  <- 'poweron' after a phase means the bank cut power\n", Node::resetReason());
  phaseStart = millis();
}

void loop() {
  uint32_t now = millis(); Node::wdtFeed();
  uint32_t t = now - phaseStart;
  if (phase == 'A' && t > 60000) { phase = 'B'; phaseStart = now; }
  else if (phase == 'B' && t > 120000) { phase = 'C'; phaseStart = now; }
  else if (phase == 'C' && t > 20000) { phase = 'A'; phaseStart = now; }
  engine.setEnabled(phase == 'C');
  if (phase == 'B') { uint32_t p = t % KEEPALIVE_PERIOD_MS; engine.setKeepAlivePulse(p < KEEPALIVE_PULSE_MS); }
  else engine.setKeepAlivePulse(false);
  if (phase == 'C') { fill_solid(engine.leds(), LED_COUNT, CRGB::White); engine.setBrightness(LED_BRIGHTNESS_MAX); engine.showRaw(); }
  else engine.update(now);
  if (now >= nextPrint) {
    nextPrint = now + 2000;
    Serial.printf("phase %c t=%lus wifi=%d est LED=%lu mA (+ ESP32 ~%d mA)\n", phase, (unsigned long)(t / 1000), WiFi.status() == WL_CONNECTED,
                  (unsigned long)engine.estimatedMilliamps(), 120);
  }
  delay(2);
}
