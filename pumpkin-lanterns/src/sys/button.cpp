#include "button.h"

void Button::begin(int pin, uint32_t longMs) {
  pin_ = pin; longMs_ = longMs;
  if (pin_ >= 0) pinMode(pin_, INPUT_PULLUP);
}

Button::Event Button::update() {
  if (pin_ < 0) return NONE;
  uint32_t now = millis();
  bool raw = digitalRead(pin_);
  if (raw != lastRaw_) { lastRaw_ = raw; tChange_ = now; }
  if (now - tChange_ < 30) return NONE;  // debounce window
  bool pressed = !raw;
  Event ev = NONE;
  if (pressed && !down_) { down_ = true; longFired_ = false; tDown_ = now; }
  else if (pressed && down_ && !longFired_ && now - tDown_ >= longMs_) { longFired_ = true; ev = LONG; }
  else if (!pressed && down_) { down_ = false; if (!longFired_) ev = SHORT; }
  return ev;
}
