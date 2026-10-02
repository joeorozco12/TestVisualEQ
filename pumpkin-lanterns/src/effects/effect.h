#pragma once
#include <FastLED.h>

// ---------------------------------------------------------------------------
// Effect plug-in interface. An effect is two plain functions + metadata.
// Add an effect: write render() (and optional init()), add one line to the
// table in effect_table.cpp. Nothing else changes.
// ---------------------------------------------------------------------------
struct EffectCtx {
  CRGB* leds;            // frame buffer to fill (already cleared? NO — effect owns every pixel)
  uint16_t n;            // pixel count
  uint8_t w, h;          // grid dims (w==0 -> linear)
  bool serpentine;
  uint32_t now;          // millis() at frame start
  uint32_t t0;           // millis() when this effect (re)started
  uint32_t frame;        // frame counter since t0
  float reactivity;      // 0..1 proximity envelope (0 = nobody near)
  uint8_t speed;         // 0..255 user param (128 = nominal)
  uint8_t hue;           // 0..255 user param (effects that take a colour)
  uint16_t seed;         // shared across nodes in sync -> identical random sequences
  uint8_t slot;          // this node's position in the fleet (0..slotCount-1)
  uint8_t slotCount;     // nodes in the fleet (>=1)
  int8_t alertDir;       // -1 = something approaching from a lower slot, +1 higher, 0 none
  float wind;            // 0..1 weather bias (candle gusts)
  float rain;            // 0..1 weather bias (lightning)
  const char* text;      // message for the text scroller

  uint32_t elapsed() const { return now - t0; }
  uint16_t xy(uint8_t x, uint8_t y) const;   // grid -> index (handles serpentine)
  float speedScale() const { return 0.25f + (speed / 128.0f) * 0.75f; }  // 0.25..1.75
  uint32_t slotDelayMs() const;   // slot * SYNC_WAVE_STEP_MS
};

typedef void (*EffectInitFn)(EffectCtx&);
typedef void (*EffectRenderFn)(EffectCtx&);

struct EffectDef {
  const char* name;
  EffectRenderFn render;
  EffectInitFn init;       // may be nullptr
  uint16_t frameMs;        // requested frame period (engine clamps to >= LED_FRAME_MS)
  bool reactive;           // true = uses ctx.reactivity meaningfully
};

extern const EffectDef EFFECT_TABLE[];
extern const uint8_t EFFECT_COUNT;

int effectIndexByName(const char* name);   // -1 if unknown

// Seeded PRNG for effects (FastLED's random16 is global; this keeps per-node sync deterministic)
uint16_t fxRand16(EffectCtx& c);
uint8_t fxRand8(EffectCtx& c, uint8_t lim = 255);
