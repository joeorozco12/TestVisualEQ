// Spooky-family: haunted glitch, eyes, off
#include "effect.h"

// ---------------- haunted / glitchy flicker ----------------------------------------
// Sickly green base that "loses signal": random blocks of pixels drop out, blink
// white, or shift hue; occasional full blackouts. Reactivity raises glitch rate.
namespace { uint32_t blackoutUntil = 0; uint32_t glitchUntil = 0; uint16_t glitchMask = 0; }
void fx_haunted_init(EffectCtx& c) { blackoutUntil = 0; glitchUntil = 0; glitchMask = 0; }
void fx_haunted_render(EffectCtx& c) {
  uint32_t t = c.elapsed();
  uint8_t rate = 4 + (uint8_t)(c.reactivity * 40);
  if (c.now > blackoutUntil && fxRand8(c, 255) < 1) blackoutUntil = c.now + 80 + (fxRand16(c) % 400);
  if (c.now > glitchUntil && fxRand8(c, 255) < rate) { glitchUntil = c.now + 30 + fxRand8(c, 120); glitchMask = fxRand16(c); }
  bool blackout = c.now < blackoutUntil;
  bool glitch = c.now < glitchUntil;
  for (uint16_t i = 0; i < c.n; i++) {
    if (blackout) { c.leds[i] = CRGB::Black; continue; }
    uint8_t n = inoise8(i * 45, t * 2);
    CRGB px = CHSV(96 + scale8(n, 20), 230, 40 + scale8(n, 140));   // green-teal
    if (glitch && ((glitchMask >> (i & 15)) & 1)) {
      uint8_t kind = (uint8_t)(glitchMask + i) % 3;
      px = kind == 0 ? CRGB::Black : kind == 1 ? CRGB(200, 220, 200) : CRGB(CHSV(200, 255, 180));
    }
    c.leds[i] = px;
  }
}

// ---------------- eyes (grid) -------------------------------------------------------
// Two glowing eyes that blink and glance left/right. On a strip: two pixel pairs.
namespace { uint32_t nextBlink = 0, blinkEnd = 0, nextGlance = 0; int8_t glance = 0; }
void fx_eyes_init(EffectCtx& c) { nextBlink = c.now + 2000; blinkEnd = 0; nextGlance = c.now + 1000; glance = 0; }
void fx_eyes_render(EffectCtx& c) {
  if (c.now >= nextBlink) { blinkEnd = c.now + 120; nextBlink = c.now + 2000 + (fxRand16(c) % 5000); }
  if (c.now >= nextGlance) { glance = (int8_t)(fxRand8(c, 3)) - 1; nextGlance = c.now + 1200 + (fxRand16(c) % 4000); }
  if (c.alertDir) glance = c.alertDir;                           // someone tripped a neighbour: look that way
  bool closed = c.now < blinkEnd;
  uint8_t hue = c.reactivity > 0.5f ? 0 : (c.hue ? c.hue : 20);   // red when someone is close
  uint8_t v = 150 + (uint8_t)(c.reactivity * 105);
  CRGB eye = CHSV(hue, 255, v);
  CRGB pupil = CHSV(hue, 255, v / 4);
  fill_solid(c.leds, c.n, CRGB(2, 0, 4));                         // near-black purple
  if (closed) return;
  if (c.w >= 5 && c.h >= 3) {
    uint8_t y = c.h / 2 - (c.h >= 5 ? 1 : 0);
    uint8_t lx = 1, rx = c.w - 2;
    c.leds[c.xy(lx, y)] = eye; c.leds[c.xy(rx, y)] = eye;
    c.leds[c.xy(lx, y + 1)] = eye; c.leds[c.xy(rx, y + 1)] = eye;
    // pupil shifts with glance (stays within the 1-px-wide eye by dimming a side)
    if (glance < 0) { c.leds[c.xy(lx, y)] = pupil; c.leds[c.xy(rx, y)] = pupil; }
    if (glance > 0) { c.leds[c.xy(lx, y + 1)] = pupil; c.leds[c.xy(rx, y + 1)] = pupil; }
  } else {
    uint16_t a = c.n / 3, b = (c.n * 2) / 3;
    c.leds[a] = eye; c.leds[b] = eye;
    if (a + 1 < c.n) c.leds[a + 1] = glance > 0 ? eye : pupil;
    if (b > 0) c.leds[b - 1] = glance < 0 ? eye : pupil;
  }
}

// ---------------- off ---------------------------------------------------------------
void fx_off_init(EffectCtx& c) {}
void fx_off_render(EffectCtx& c) { fill_solid(c.leds, c.n, CRGB::Black); }
