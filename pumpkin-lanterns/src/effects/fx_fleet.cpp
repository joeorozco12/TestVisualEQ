// Fleet-aware effects: chase (travelling pulse across nodes), text (3x5 font scroller)
#include "effect.h"
#include "config.h"
#include <string.h>

// ---------------- chase ---------------------------------------------------------------
// A bright pulse travels pumpkin-to-pumpkin: node k flashes at phase k*STEP within a
// period of N*STEP + rest. Needs synced t0 (leader beacons) — standalone it just pulses.
void fx_chase_init(EffectCtx& c) {}
void fx_chase_render(EffectCtx& c) {
  uint32_t step = (uint32_t)(SYNC_WAVE_STEP_MS / c.speedScale());
  uint32_t period = step * c.slotCount + 1500;
  uint32_t ph = c.elapsed() % period;
  uint32_t mine = c.slotDelayMs() * step / SYNC_WAVE_STEP_MS;
  int32_t d = (int32_t)ph - (int32_t)mine;
  float env = 0;
  if (d >= 0 && d < (int32_t)(step * 2)) env = 1.0f - (float)d / (step * 2);   // sharp attack, 2-step decay
  uint8_t hue = c.hue ? c.hue : 24;
  uint8_t v = 14 + (uint8_t)(env * 240);
  for (uint16_t i = 0; i < c.n; i++) {
    uint8_t sp = scale8(inoise8(i * 40, c.elapsed() / 5), 40);
    c.leds[i] = CHSV(hue + (uint8_t)(env * 20), 255 - (uint8_t)(env * 120), qadd8(v, sp));
  }
  if (c.reactivity > 0.6f) for (uint16_t i = 0; i < c.n; i++) c.leds[i] |= CRGB(60, 0, 0);
}

// ---------------- text ----------------------------------------------------------------
// 3x5 glyphs, column-major bits (bit0 = top). Scrolls ctx.text right-to-left on a grid
// (needs h >= 5). On a strip: flashes one letter's worth of pixels per glyph (fallback).
namespace {
  struct Glyph { char ch; uint8_t col[3]; };
  const Glyph FONT[] = {
    {'A',{0x1E,0x05,0x1E}},{'B',{0x1F,0x15,0x0A}},{'C',{0x0E,0x11,0x11}},{'D',{0x1F,0x11,0x0E}},{'E',{0x1F,0x15,0x11}},
    {'F',{0x1F,0x05,0x01}},{'G',{0x0E,0x11,0x1D}},{'H',{0x1F,0x04,0x1F}},{'I',{0x11,0x1F,0x11}},{'J',{0x08,0x10,0x0F}},
    {'K',{0x1F,0x04,0x1B}},{'L',{0x1F,0x10,0x10}},{'M',{0x1F,0x02,0x1F}},{'N',{0x1F,0x01,0x1E}},{'O',{0x0E,0x11,0x0E}},
    {'P',{0x1F,0x05,0x02}},{'Q',{0x0E,0x19,0x1E}},{'R',{0x1F,0x05,0x1A}},{'S',{0x12,0x15,0x09}},{'T',{0x01,0x1F,0x01}},
    {'U',{0x0F,0x10,0x0F}},{'V',{0x07,0x18,0x07}},{'W',{0x1F,0x08,0x1F}},{'X',{0x1B,0x04,0x1B}},{'Y',{0x03,0x1C,0x03}},
    {'Z',{0x19,0x15,0x13}},{'0',{0x0E,0x11,0x0E}},{'1',{0x12,0x1F,0x10}},{'2',{0x19,0x15,0x12}},{'3',{0x11,0x15,0x0A}},
    {'4',{0x07,0x04,0x1F}},{'5',{0x17,0x15,0x09}},{'6',{0x0E,0x15,0x08}},{'7',{0x01,0x1D,0x03}},{'8',{0x0A,0x15,0x0A}},
    {'9',{0x02,0x15,0x0E}},{'!',{0x00,0x17,0x00}},{'?',{0x01,0x15,0x02}},{'-',{0x04,0x04,0x04}},{'.',{0x00,0x10,0x00}},
    {' ',{0x00,0x00,0x00}},
  };
  const uint8_t* glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    for (auto& g : FONT) if (g.ch == ch) return g.col;
    return FONT[sizeof(FONT) / sizeof(FONT[0]) - 1].col;   // space
  }
  // Column k of the rendered message (glyph columns + 1 blank each), 0 if past the end.
  uint8_t msgColumn(const char* msg, int k, int& total) {
    int len = strlen(msg); total = len * 4;
    if (k < 0 || k >= total) return 0;
    int g = k / 4, cidx = k % 4;
    return cidx == 3 ? 0 : glyph(msg[g])[cidx];
  }
}
void fx_text_init(EffectCtx& c) {}
void fx_text_render(EffectCtx& c) {
  const char* msg = c.text && *c.text ? c.text : TEXT_MESSAGE;
  uint32_t stepMs = (uint32_t)(TEXT_SCROLL_MS / c.speedScale());
  int total; msgColumn(msg, 0, total);
  int span = total + (c.w ? c.w : 1);
  int offset = (int)((c.elapsed() / stepMs) % span) - (c.w ? c.w : 1);   // start fully off the right edge
  uint8_t hue = c.hue ? c.hue : 20;
  fill_solid(c.leds, c.n, CRGB(3, 1, 0));
  if (c.w >= 3 && c.h >= 5) {
    uint8_t y0 = (c.h - 5) / 2;
    for (uint8_t x = 0; x < c.w; x++) {
      uint8_t col = msgColumn(msg, offset + x, total);
      for (uint8_t y = 0; y < 5; y++)
        if (col & (1 << y)) c.leds[c.xy(x, y0 + y)] = CHSV(hue + x * 6, 255, 230);
    }
  } else {  // strip fallback: light up proportional to glyph density
    uint8_t col = msgColumn(msg, offset + 2, total);
    uint8_t bits = __builtin_popcount(col);
    for (uint16_t i = 0; i < c.n; i++) c.leds[i] = (i % 5) < bits ? CRGB(CHSV(hue, 255, 200)) : CRGB(3, 1, 0);
  }
}
