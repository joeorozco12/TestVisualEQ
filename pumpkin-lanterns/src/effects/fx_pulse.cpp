// Pulse-family: heartbeat, breathe, wakeup (proximity-reactive)
#include "effect.h"

// ---------------- heartbeat -----------------------------------------------------
// lub-dub: two gaussian-ish bumps per period, red. Rate climbs with reactivity (60 -> 150 bpm).
void fx_heartbeat_init(EffectCtx& c) {}
void fx_heartbeat_render(EffectCtx& c) {
  float bpm = 55.0f + c.reactivity * 95.0f;
  uint32_t period = (uint32_t)(60000.0f / (bpm * c.speedScale()));
  uint32_t ph = c.elapsed() % period;
  float p = (float)ph / period;                   // 0..1
  auto bump = [](float x, float centre, float width) {
    float d = (x - centre) / width; return expf(-d * d * 4.0f);
  };
  float env = bump(p, 0.12f, 0.08f) * 1.0f + bump(p, 0.32f, 0.10f) * 0.7f;
  uint8_t v = 20 + (uint8_t)(min(env, 1.0f) * 225);
  uint8_t hue = 0 - (uint8_t)(c.reactivity * 10);  // slight shift toward crimson
  fill_solid(c.leds, c.n, CHSV(hue, 255, v));
}

// ---------------- breathe --------------------------------------------------------
// Smooth sinusoidal breathing, hue from ctx.hue (default pumpkin orange). ~4 s period.
void fx_breathe_init(EffectCtx& c) {}
void fx_breathe_render(EffectCtx& c) {
  uint32_t period = (uint32_t)(4200 / c.speedScale());
  uint8_t ph = (uint8_t)((c.elapsed() % period) * 256 / period);
  uint8_t v = 12 + scale8(sin8(ph), 230);
  v = qadd8(v, (uint8_t)(c.reactivity * 60));
  uint8_t hue = c.hue ? c.hue : 24;
  fill_solid(c.leds, c.n, CHSV(hue, 240, v));
}

// ---------------- wakeup (scare) --------------------------------------------------
// Driven by ctx.reactivity. Stages:
//   r < 0.3 : sleeping — dim slow-breathing deep red
//   r < 0.7 : stirring — brightness and flicker rise, colour shifts to orange
//   r >= 0.7: awake   — bright strobe-ish eruption with white flashes, then settles
// Flash cadence decays with time since wake so it doesn't sit at full strobe.
namespace { uint32_t wakeAt = 0; bool awake = false; }
void fx_wakeup_init(EffectCtx& c) { wakeAt = 0; awake = false; }
void fx_wakeup_render(EffectCtx& c) {
  float r = c.reactivity;
  uint32_t t = c.elapsed();
  if (r >= 0.7f && !awake) { awake = true; wakeAt = c.now; }
  if (r < 0.2f) awake = false;

  if (!awake) {
    uint8_t breath = sin8((t / 12) & 0xFF);                       // ~3 s
    uint8_t base = 6 + scale8(breath, 30);
    uint8_t v = base + (uint8_t)(r * 160);
    uint8_t hue = 0 + (uint8_t)(r * 26);                         // red -> orange
    for (uint16_t i = 0; i < c.n; i++) {
      uint8_t fl = r > 0.3f ? scale8(inoise8(i * 50, t * 3), (uint8_t)(r * 120)) : 0;
      c.leds[i] = CHSV(hue, 255, qadd8(v, fl));
    }
    return;
  }
  uint32_t since = c.now - wakeAt;
  uint16_t flashPeriod = 60 + min<uint32_t>(since / 10, 400);     // 60 ms -> 460 ms
  bool flash = (since % flashPeriod) < 35 && since < 4000;
  for (uint16_t i = 0; i < c.n; i++) {
    if (flash) c.leds[i] = CRGB(255, 240, 200);
    else {
      uint8_t fl = inoise8(i * 70, t * 5);
      uint8_t v = 150 + scale8(fl, 105);
      c.leds[i] = CHSV(fxRand8(c, 255) < 3 ? 160 : 10, 255, v);  // mostly red-orange, rare blue sparks
    }
  }
}
