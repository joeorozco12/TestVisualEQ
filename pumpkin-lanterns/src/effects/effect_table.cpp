#include "effect.h"
#include "config.h"
#include <string.h>

// ---- registry: name, render, init, frame ms, reactive ----------------------
#define FX(name) void fx_##name##_render(EffectCtx&); void fx_##name##_init(EffectCtx&);
FX(candle) FX(ember) FX(heartbeat) FX(lightning) FX(rainbow) FX(breathe)
FX(haunted) FX(wipe) FX(sparkle) FX(wakeup) FX(fire) FX(eyes) FX(off)
#undef FX

const EffectDef EFFECT_TABLE[] = {
  // name         render               init               ms    reactive
  { "candle",     fx_candle_render,    fx_candle_init,    25,   true  },
  { "ember",      fx_ember_render,     fx_ember_init,     40,   true  },
  { "heartbeat",  fx_heartbeat_render, fx_heartbeat_init, 20,   true  },
  { "lightning",  fx_lightning_render, fx_lightning_init, 20,   false },
  { "rainbow",    fx_rainbow_render,   fx_rainbow_init,   25,   false },
  { "breathe",    fx_breathe_render,   fx_breathe_init,   25,   true  },
  { "haunted",    fx_haunted_render,   fx_haunted_init,   30,   true  },
  { "wipe",       fx_wipe_render,      fx_wipe_init,      30,   false },
  { "sparkle",    fx_sparkle_render,   fx_sparkle_init,   25,   true  },
  { "wakeup",     fx_wakeup_render,    fx_wakeup_init,    20,   true  },
  { "fire",       fx_fire_render,      fx_fire_init,      35,   true  },
  { "eyes",       fx_eyes_render,      fx_eyes_init,      40,   true  },
  { "off",        fx_off_render,       fx_off_init,       200,  false },
};
const uint8_t EFFECT_COUNT = sizeof(EFFECT_TABLE) / sizeof(EFFECT_TABLE[0]);

int effectIndexByName(const char* name) {
  if (!name) return -1;
  for (uint8_t i = 0; i < EFFECT_COUNT; i++)
    if (strcasecmp(EFFECT_TABLE[i].name, name) == 0) return i;
  return -1;
}

// ---- shared helpers ----------------------------------------------------------
uint16_t EffectCtx::xy(uint8_t x, uint8_t y) const {
  if (w == 0) return x;
  if (serpentine && (y & 1)) x = (w - 1) - x;
  uint16_t i = (uint16_t)y * w + x;
  return i < n ? i : n - 1;
}

// xorshift16 seeded from ctx.seed + frame so synced nodes draw identical frames
uint16_t fxRand16(EffectCtx& c) {
  uint16_t s = c.seed ? c.seed : 0xACE1;
  s ^= s << 7; s ^= s >> 9; s ^= s << 8;
  c.seed = s;
  return s;
}
uint8_t fxRand8(EffectCtx& c, uint8_t lim) { return lim ? (uint8_t)(fxRand16(c) % lim) : 0; }
