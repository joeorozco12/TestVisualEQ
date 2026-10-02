// Motion-family: rainbow chase, colour wipe, sparkle, lightning
#include "effect.h"

// ---------------- rainbow chase ----------------------------------------------------
// Hue gradient scrolls along the strip, or diagonally across a grid.
void fx_rainbow_init(EffectCtx& c) {}
void fx_rainbow_render(EffectCtx& c) {
  uint8_t base = (uint8_t)((c.elapsed() * c.speedScale()) / 12);
  base += (uint8_t)((255 / c.slotCount) * c.slot);       // fleet: each pumpkin a step along the rainbow
  if (c.w) {
    for (uint8_t y = 0; y < c.h; y++)
      for (uint8_t x = 0; x < c.w; x++)
        c.leds[c.xy(x, y)] = CHSV(base + (x + y) * (255 / (c.w + c.h)), 255, 220);
  } else {
    fill_rainbow(c.leds, c.n, base, 255 / c.n);
  }
}

// ---------------- colour wipe ------------------------------------------------------
// Sweeps a new colour across (row by row on grid), holds, then sweeps the next.
namespace { uint8_t wipeHue = 0; uint8_t wipeNext = 0; }
void fx_wipe_init(EffectCtx& c) { wipeHue = c.hue ? c.hue : 20; wipeNext = wipeHue + 96; }
void fx_wipe_render(EffectCtx& c) {
  uint16_t steps = c.w ? c.h : c.n;
  uint32_t stepMs = (uint32_t)(120 / c.speedScale());
  uint32_t holdMs = 1500;
  uint32_t cycle = steps * stepMs + holdMs;
  uint32_t ph = c.elapsed() % cycle;
  uint32_t cyc = c.elapsed() / cycle;
  static uint32_t lastCyc = 0xFFFFFFFF;
  if (cyc != lastCyc) { lastCyc = cyc; wipeHue = wipeNext; wipeNext = wipeHue + 64 + fxRand8(c, 128); }
  uint16_t pos = min<uint32_t>(ph / stepMs, steps);
  for (uint16_t s = 0; s < steps; s++) {
    CRGB col = CHSV(s < pos ? wipeNext : wipeHue, 255, 200);
    if (c.w) for (uint8_t x = 0; x < c.w; x++) c.leds[c.xy(x, s)] = col;
    else c.leds[s] = col;
  }
}

// ---------------- sparkle -----------------------------------------------------------
// Dark amber bed; random pixels pop to white/gold and decay. Density follows reactivity.
namespace { uint8_t sparkV[256]; }
void fx_sparkle_init(EffectCtx& c) { memset(sparkV, 0, sizeof(sparkV)); }
void fx_sparkle_render(EffectCtx& c) {
  uint8_t chance = 6 + (uint8_t)(c.reactivity * 60);    // per-frame, per-strip
  uint8_t decay = 10 + (uint8_t)(c.speedScale() * 8);
  if (fxRand8(c, 255) < chance) { uint16_t i = fxRand16(c) % c.n; if (i < 256) sparkV[i] = 255; }
  if (c.reactivity > 0.5f && fxRand8(c, 255) < chance) { uint16_t i = fxRand16(c) % c.n; if (i < 256) sparkV[i] = 255; }
  for (uint16_t i = 0; i < c.n && i < 256; i++) {
    sparkV[i] = qsub8(sparkV[i], decay);
    CRGB bed = CHSV(c.hue ? c.hue : 28, 255, 18);
    CRGB sp = CHSV(40, 255 - sparkV[i], sparkV[i]);     // bright = whiter
    c.leds[i] = bed + sp;
  }
}

// ---------------- lightning storm -------------------------------------------------
// Dark blue-purple haze; random multi-strike flashes with afterglow; rumble gap between.
namespace {
  uint32_t nextStrike = 0;
  uint8_t strikesLeft = 0;
  uint32_t strikeEnd = 0;
  uint8_t glow = 0;
}
void fx_lightning_init(EffectCtx& c) { nextStrike = c.now + 1500; strikesLeft = 0; strikeEnd = 0; glow = 0; }
void fx_lightning_render(EffectCtx& c) {
  uint32_t t = c.elapsed();
  if (c.frame == 1) nextStrike += c.slotDelayMs();             // fleet: storm rolls down the row
  if (c.now >= nextStrike && strikesLeft == 0) {
    strikesLeft = 2 + fxRand8(c, 5);                         // 2..6 strikes per storm cell
    nextStrike = c.now;
  }
  bool flashing = c.now < strikeEnd;
  if (strikesLeft && !flashing && c.now >= nextStrike) {
    strikeEnd = c.now + 20 + fxRand8(c, 80);                  // 20..100 ms flash
    strikesLeft--;
    nextStrike = strikesLeft ? c.now + 60 + fxRand8(c, 250)   // inter-strike gap
                             : c.now + (uint32_t)((2500 + fxRand16(c) % 6000) / c.speedScale());
    glow = 200;
    flashing = true;
  }
  glow = qsub8(glow, 6);
  for (uint16_t i = 0; i < c.n; i++) {
    if (flashing) {
      c.leds[i] = (fxRand8(c, 255) < 230) ? CRGB(230, 230, 255) : CRGB(120, 120, 180);
    } else {
      uint8_t haze = inoise8(i * 30, t / 8);
      c.leds[i] = CHSV(170 + scale8(haze, 30), 220, 6 + scale8(haze, 20));
      c.leds[i] += CHSV(165, 120, glow);
    }
  }
}
