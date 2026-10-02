#include "engine.h"
#include "sys/log.h"

void EffectEngine::begin() {
  FastLED.addLeds<LED_CHIPSET, LED_PIN, LED_COLOR_ORDER>(leds_, LED_COUNT).setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_PSU_VOLTS, LED_PSU_MA);   // scales brightness if a frame would exceed budget
  FastLED.setDither(0);                                                 // dithering + RMT + WiFi = visible shimmer; off
  fill_solid(leds_, LED_COUNT, CRGB::Black);
  fill_solid(prev_, LED_COUNT, CRGB::Black);
  ctx_.leds = leds_; ctx_.n = LED_COUNT; ctx_.w = LED_GRID_W; ctx_.h = LED_GRID_H;
  ctx_.serpentine = LED_GRID_SERPENTINE;
  ctx_.speed = 128; ctx_.hue = 0; ctx_.seed = 0xACE1; ctx_.reactivity = 0;
  setBrightness(LED_BRIGHTNESS_DEFAULT);
  FastLED.show();
  LOGI("fx", "engine: %u px, %ux%u, %u effects, cap=%u, budget=%u mA",
       LED_COUNT, LED_GRID_W, LED_GRID_H, EFFECT_COUNT, LED_BRIGHTNESS_MAX, LED_PSU_MA);
}

void EffectEngine::setBrightness(uint8_t b) {
  brightness_ = b > LED_BRIGHTNESS_MAX ? LED_BRIGHTNESS_MAX : b;
  FastLED.setBrightness(brightness_);
}

void EffectEngine::setEnabled(bool on) {
  if (on != enabled_) LOGI("fx", "output %s", on ? "ON" : "OFF");
  enabled_ = on;
}

bool EffectEngine::setEffect(int idx, bool crossfade) {
  if (idx < 0 || idx >= EFFECT_COUNT) return false;
  if (idx == base_ && !scareActive()) return true;
  base_ = idx;
  if (scareActive()) return true;          // takes effect when scare ends
  if (crossfade) { memcpy(prev_, leds_, sizeof(leds_)); fadeStart_ = millis(); }
  cur_ = idx;
  ctx_.t0 = millis(); ctx_.frame = 0;
  if (EFFECT_TABLE[cur_].init) EFFECT_TABLE[cur_].init(ctx_);
  LOGI("fx", "effect -> %s", EFFECT_TABLE[cur_].name);
  return true;
}

void EffectEngine::alignStart(uint32_t elapsedMs, uint32_t now, uint16_t seed) {
  ctx_.t0 = now - elapsedMs;
  ctx_.seed = seed;
}

void EffectEngine::triggerScare(int idx, uint32_t durationMs) {
  if (idx < 0 || idx >= EFFECT_COUNT) return;
  uint32_t now = millis();
  if (!scareActive()) { memcpy(prev_, leds_, sizeof(leds_)); fadeStart_ = now; }
  scare_ = idx; scareUntil_ = now + durationMs;
  cur_ = idx; ctx_.t0 = now; ctx_.frame = 0;
  if (EFFECT_TABLE[cur_].init) EFFECT_TABLE[cur_].init(ctx_);
  LOGI("fx", "scare -> %s for %lu ms", EFFECT_TABLE[cur_].name, (unsigned long)durationMs);
}

void EffectEngine::update(uint32_t now) {
  // Scare expiry -> return to base with crossfade
  if (scareActive() && (int32_t)(now - scareUntil_) >= 0) {
    scareUntil_ = 0; scare_ = -1;
    memcpy(prev_, leds_, sizeof(leds_)); fadeStart_ = now;
    cur_ = base_; ctx_.t0 = now; ctx_.frame = 0;
    if (cur_ >= 0 && EFFECT_TABLE[cur_].init) EFFECT_TABLE[cur_].init(ctx_);
  }
  if (cur_ < 0) return;
  if ((int32_t)(now - nextFrame_) < 0) return;         // not due
  uint16_t fm = EFFECT_TABLE[cur_].frameMs;
  nextFrame_ = now + (fm < LED_FRAME_MS ? LED_FRAME_MS : fm);

  // Soft gate for enable/disable so dusk/dawn transitions aren't a hard cut
  uint8_t target = enabled_ ? 255 : 0;
  if (fadeOut_ != target) fadeOut_ = target > fadeOut_ ? qadd8(fadeOut_, 3) : qsub8(fadeOut_, 3);
  if (fadeOut_ == 0 && !enabled_) {
    if (keepAlive_) {                                   // power-bank keep-alive load pulse
      FastLED.setBrightness(255);                       // bypass user cap so the pulse draws real current
      fill_solid(leds_, LED_COUNT, CRGB(KEEPALIVE_PULSE_LEVEL, KEEPALIVE_PULSE_LEVEL * 2 / 3, KEEPALIVE_PULSE_LEVEL / 4));
      FastLED.show(); FastLED.setBrightness(brightness_);
      blackShown_ = false; frames_++;
    } else if (!blackShown_) {                          // push black once, then idle (no bus traffic)
      fill_solid(leds_, LED_COUNT, CRGB::Black); FastLED.show(); blackShown_ = true; frames_++;
    }
    return;
  }
  blackShown_ = false;

  ctx_.now = now; ctx_.frame++;
  EFFECT_TABLE[cur_].render(ctx_);

  // Crossfade from snapshot
  if (fadeStart_) {
    uint32_t dt = now - fadeStart_;
    if (dt >= EFFECT_FADE_MS) fadeStart_ = 0;
    else {
      uint8_t amt = (uint8_t)((dt * 255) / EFFECT_FADE_MS);
      for (uint16_t i = 0; i < LED_COUNT; i++) leds_[i] = blend(prev_[i], leds_[i], amt);
    }
  }
  if (fadeOut_ != 255) nscale8(leds_, LED_COUNT, fadeOut_);
  FastLED.show();
  frames_++;
}

void EffectEngine::showRaw() { FastLED.show(); }
void EffectEngine::setKeepAlivePulse(bool on) { keepAlive_ = on; }

uint32_t EffectEngine::estimatedMilliamps() const {
  // FastLED: ~20 mA per colour channel at full scale + 1 mA idle per pixel (WS2812B)
  uint32_t sum = 0;
  for (uint16_t i = 0; i < LED_COUNT; i++) sum += leds_[i].r + leds_[i].g + leds_[i].b;
  sum = (sum * 20 * FastLED.getBrightness()) / (255UL * 255UL);
  return sum + LED_COUNT;
}
