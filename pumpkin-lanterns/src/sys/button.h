#pragma once
#include <Arduino.h>

// Debounced active-low button with short/long press events (poll from loop).
class Button {
 public:
  enum Event : uint8_t { NONE, SHORT, LONG };
  void begin(int pin, uint32_t longMs);
  Event update();                 // returns one event per press
 private:
  int pin_ = -1;
  uint32_t longMs_ = 1500, tDown_ = 0, tChange_ = 0;
  bool down_ = false, longFired_ = false, lastRaw_ = true;
};
