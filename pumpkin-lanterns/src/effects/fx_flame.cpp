// Flame-family effects: candle, ember, fire (Fire2012 on grid columns)
#include "effect.h"

// ---------------- candle --------------------------------------------------------
// Low-frequency noise drives a global "flame height"; per-pixel high-freq noise
// adds flicker. Random gusts dip brightness. Reactivity brightens + reddens.
namespace {
  uint32_t candleGustUntil = 0;
  uint8_t  candleGustDepth = 0;
}
void fx_candle_init(EffectCtx& c) { candleGustUntil = 0; candleGustDepth = 0; }
void fx_candle_render(EffectCtx& c) {
  uint32_t t = (uint32_t)(c.elapsed() * c.speedScale());
  uint8_t global = inoise8(t / 3, 1000);               // slow wander 0..255
  global = scale8(global, 120) + 110;                   // 110..230
  uint8_t gustChance = 2 + (uint8_t)(c.wind * 12);      // weather: wind -> more, deeper gusts
  if (c.now > candleGustUntil && fxRand8(c, 255) < gustChance) {
    candleGustUntil = c.now + 150 + fxRand8(c, 250) + (uint16_t)(c.wind * 300);
    candleGustDepth = 60 + fxRand8(c, 120) + (uint8_t)(c.wind * 60);
  }
  if (c.now < candleGustUntil) global = qsub8(global, candleGustDepth);
  uint8_t react = (uint8_t)(c.reactivity * 255);
  for (uint16_t i = 0; i < c.n; i++) {
    uint8_t f = inoise8(i * 60, t * 4);                 // fast per-pixel flicker
    uint8_t v = scale8(global, 160 + scale8(f, 95));
    uint8_t hue = 18 + scale8(f, 14);                   // 18..32: deep orange -> amber
    hue = hue - scale8(react, 14);                      // nearer = redder
    v = qadd8(v, scale8(react, 40));
    c.leds[i] = CHSV(hue, 255 - scale8(f, 30), v);
  }
}

// ---------------- ember ---------------------------------------------------------
// Dim, slow-breathing bed of coals with occasional bright pops that decay.
namespace { uint8_t emberHeat[256]; }
void fx_ember_init(EffectCtx& c) { memset(emberHeat, 0, sizeof(emberHeat)); }
void fx_ember_render(EffectCtx& c) {
  uint32_t t = (uint32_t)(c.elapsed() * c.speedScale());
  uint8_t popChance = 3 + (uint8_t)(c.reactivity * 25);
  for (uint16_t i = 0; i < c.n && i < 256; i++) {
    uint8_t bed = inoise8(i * 40, t / 6);               // slow coal glow
    bed = scale8(bed, 70) + 25;                         // 25..95
    if (fxRand8(c, 255) < popChance) emberHeat[i] = qadd8(emberHeat[i], 120 + fxRand8(c, 100));
    emberHeat[i] = qsub8(emberHeat[i], 4);              // decay
    uint8_t heat = qadd8(bed, emberHeat[i]);
    c.leds[i] = HeatColor(heat);
  }
}

// ---------------- fire (Fire2012 per column) ----------------------------------
// On a grid: each column is an independent 1-D fire rising from the bottom row.
// On a strip: one fire along the strip.
namespace { uint8_t fireHeat[256]; }
void fx_fire_init(EffectCtx& c) { memset(fireHeat, 0, sizeof(fireHeat)); }
void fx_fire_render(EffectCtx& c) {
  uint8_t cooling = 70 - (uint8_t)(c.reactivity * 40);    // closer = taller flames
  uint8_t sparking = 110 + (uint8_t)(c.reactivity * 100);
  uint8_t cols = c.w ? c.w : 1;
  uint8_t rows = c.w ? c.h : (uint8_t)min<uint16_t>(c.n, 255);
  for (uint8_t x = 0; x < cols; x++) {
    uint8_t* h = &fireHeat[x * rows];
    for (uint8_t y = 0; y < rows; y++) h[y] = qsub8(h[y], fxRand8(c, ((cooling * 10) / rows) + 2));
    for (int8_t y = rows - 1; y >= 2; y--) h[y] = (h[y - 1] + h[y - 2] + h[y - 2]) / 3;
    if (fxRand8(c, 255) < sparking) { uint8_t y = fxRand8(c, min<uint8_t>(rows, 2)); h[y] = qadd8(h[y], 160 + fxRand8(c, 95)); }
    for (uint8_t y = 0; y < rows; y++) {
      // bottom row of the physical grid is y = h-1 (row 0 is top); flip so fire rises
      uint16_t idx = c.w ? c.xy(x, (c.h - 1) - y) : y;
      c.leds[idx] = HeatColor(h[y]);
    }
  }
}
