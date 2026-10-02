#pragma once
#include <FastLED.h>
#include "effect.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Frame-timed, non-blocking effects engine.
//   - owns the LED buffer + a snapshot buffer for crossfades
//   - "base" effect (playlist / user selected) + optional temporary "scare"
//     effect that returns to base after a duration
//   - global brightness cap + FastLED power limiter (see config.h)
// Call update() as often as possible from loop(); it only renders when due.
// ---------------------------------------------------------------------------
class EffectEngine {
 public:
  void begin();
  void update(uint32_t now);

  // Base effect control
  bool setEffect(int idx, bool crossfade = true);
  bool setEffect(const char* name, bool crossfade = true) { return setEffect(effectIndexByName(name), crossfade); }
  int  effectIndex() const { return cur_; }
  const char* effectName() const { return cur_ >= 0 ? EFFECT_TABLE[cur_].name : "none"; }
  uint32_t effectElapsed(uint32_t now) const { return now - ctx_.t0; }
  void alignStart(uint32_t elapsedMs, uint32_t now, uint16_t seed);   // sync: adopt leader's phase

  // Temporary scare overlay
  void triggerScare(int idx, uint32_t durationMs);
  void triggerScare(const char* name, uint32_t durationMs) { triggerScare(effectIndexByName(name), durationMs); }
  bool scareActive() const { return scareUntil_ != 0; }

  // Output state
  void setEnabled(bool on);               // false = black output (dusk/dawn gate)
  void setKeepAlivePulse(bool on);        // while disabled: dim warm pulse to keep a USB power bank awake
  bool enabled() const { return enabled_; }
  void setBrightness(uint8_t b);          // clamped to LED_BRIGHTNESS_MAX
  uint8_t brightness() const { return brightness_; }
  void setReactivity(float r) { ctx_.reactivity = r < 0 ? 0 : r > 1 ? 1 : r; }
  void setSpeed(uint8_t s) { ctx_.speed = s; }
  void setHue(uint8_t h) { ctx_.hue = h; }
  uint8_t speed() const { return ctx_.speed; }
  uint8_t hue() const { return ctx_.hue; }
  uint16_t seed() const { return ctx_.seed; }

  // Direct access for bench tests / keep-alive pulse
  CRGB* leds() { return leds_; }
  void showRaw();                          // push leds_ as-is (bypasses effect)
  uint32_t estimatedMilliamps() const;     // for the current frame at current brightness
  uint32_t framesRendered() const { return frames_; }

 private:
  void renderInto(CRGB* buf, int idx, EffectCtx& ctx);
  CRGB leds_[LED_COUNT];
  CRGB prev_[LED_COUNT];
  EffectCtx ctx_{};
  int cur_ = -1, base_ = -1, scare_ = -1;
  uint32_t scareUntil_ = 0;
  uint32_t fadeStart_ = 0;
  uint32_t nextFrame_ = 0;
  uint32_t frames_ = 0;
  uint8_t brightness_ = 128;
  bool enabled_ = true;
  bool keepAlive_ = false, blackShown_ = false;
  uint8_t fadeOut_ = 0;    // enable/disable soft gate 0..255
};
