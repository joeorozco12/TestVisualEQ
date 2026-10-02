// BENCH: LED cluster. Validates wiring, colour order, grid mapping, power limiter, every effect.
// Serial keys: 'r' 'g' 'b' 'w' solid colours | 'x' walk pixels | 'm' grid map test | 'p' power ramp
//              '0'-'9' effect by index | 'n' next effect | '+'/'-' brightness | '?' help
#include <Arduino.h>
#include "config.h"
#include "sys/log.h"
#include "effects/engine.h"

static EffectEngine engine;
static int fx = -1; static uint32_t walkT = 0; static uint16_t walkI = 0; static char mode = 'n';

static void help() {
  Serial.println("\n== LED bench ==\n r/g/b/w solid | x walk | m map | p power ramp | n next fx | 0-9 fx | +/- brightness | ? help");
  for (uint8_t i = 0; i < EFFECT_COUNT; i++) Serial.printf("  %u: %s\n", i, EFFECT_TABLE[i].name);
}

void setup() {
  Serial.begin(SERIAL_BAUD); delay(300);
  engine.begin();
  LOGI("bench", "LED_PIN=%d LED_COUNT=%d grid=%dx%d serp=%d order=%s", LED_PIN, LED_COUNT, LED_GRID_W, LED_GRID_H, LED_GRID_SERPENTINE, "GRB?");
  help();
  engine.setEffect("candle", false); fx = engine.effectIndex(); mode = 'n';
}

void loop() {
  uint32_t now = millis();
  if (Serial.available()) {
    char k = Serial.read();
    if (k == '?') help();
    else if (k == '+') { engine.setBrightness(engine.brightness() + 16); Serial.printf("brightness %u\n", engine.brightness()); }
    else if (k == '-') { engine.setBrightness(engine.brightness() - 16); Serial.printf("brightness %u\n", engine.brightness()); }
    else if (k == 'n') { fx = (fx + 1) % EFFECT_COUNT; engine.setEffect(fx); mode = 'n'; }
    else if (k >= '0' && k <= '9' && (k - '0') < EFFECT_COUNT) { fx = k - '0'; engine.setEffect(fx); mode = 'n'; }
    else if (k == 'r' || k == 'g' || k == 'b' || k == 'w' || k == 'x' || k == 'm' || k == 'p') { mode = k; engine.setEffect("off", false); }
    if (k > ' ') Serial.printf("mode=%c fx=%s\n", mode, engine.effectName());
  }
  if (mode == 'n') { engine.update(now); engine.setReactivity(((now / 1000) % 20) < 4 ? 1.0f : 0.0f); return; }  // bursts of reactivity every 20 s
  CRGB* l = engine.leds();
  if (mode == 'r') fill_solid(l, LED_COUNT, CRGB::Red);
  if (mode == 'g') fill_solid(l, LED_COUNT, CRGB::Green);
  if (mode == 'b') fill_solid(l, LED_COUNT, CRGB::Blue);
  if (mode == 'w') fill_solid(l, LED_COUNT, CRGB::White);        // power limiter should cap this; measure!
  if (mode == 'x' && now - walkT > 150) { walkT = now; fill_solid(l, LED_COUNT, CRGB::Black); l[walkI] = CRGB::White; Serial.printf("px %u\n", walkI); walkI = (walkI + 1) % LED_COUNT; }
  if (mode == 'm' && now - walkT > 400) {                          // lights (x,y) in reading order: verifies serpentine map
    walkT = now; fill_solid(l, LED_COUNT, CRGB::Black);
    uint8_t x = walkI % (LED_GRID_W ? LED_GRID_W : LED_COUNT), y = LED_GRID_W ? walkI / LED_GRID_W : 0;
    EffectCtx c{}; c.w = LED_GRID_W; c.h = LED_GRID_H; c.serpentine = LED_GRID_SERPENTINE; c.n = LED_COUNT;
    l[c.xy(x, y)] = CRGB::Orange; Serial.printf("x=%u y=%u -> idx %u\n", x, y, c.xy(x, y)); walkI = (walkI + 1) % LED_COUNT;
  }
  if (mode == 'p') {                                               // ramp white 0..cap over 10 s; log estimated mA vs your meter
    uint8_t v = (now / 40) % 256; fill_solid(l, LED_COUNT, CRGB(v, v, v));
    if (now - walkT > 1000) { walkT = now; Serial.printf("white=%u est=%lu mA (limiter active if dimmer than expected)\n", v, (unsigned long)engine.estimatedMilliamps()); }
  }
  if (now - walkT > 20 || mode == 'r' || mode == 'g' || mode == 'b' || mode == 'w') engine.showRaw();
  delay(5);
}
