#pragma once
#include <Arduino.h>

// HC-SR04 class ranger, interrupt-timed echo (no pulseIn blocking).
// Pipeline: raw -> range check -> jump rejection -> median(N) -> presence FSM -> envelope
class Ultrasonic {
 public:
  void begin();
  void update(uint32_t now);
  bool healthy() const { return healthy_; }
  float distanceCm() const { return filtered_; }     // -1 if unknown
  float rawCm() const { return raw_; }
  bool presence() const { return presence_; }        // hysteresis-latched "someone is in range"
  bool takeTrigger();                                // one-shot: true once per scare trigger
  float reactivity() const { return react_; }        // 0..1 for effects
  uint32_t cooldownRemainingMs(uint32_t now) const;
  void simulateTrigger(uint32_t now);                // from web/MQTT "scare" button
  void externalPresence(bool active, uint32_t now);  // PIR etc.: triggers through the same cooldown
 private:
  static void IRAM_ATTR echoIsr(void* arg);
  void ping();
  float raw_ = -1, filtered_ = -1, react_ = 0, distReact_ = 0;
  float win_[9]; uint8_t winN_ = 0, winI_ = 0;
  uint32_t nextPing_ = 0, lastValid_ = 0, lastTrig_ = 0, trigEnvStart_ = 0;
  uint8_t confirm_ = 0;
  bool presence_ = false, pendingTrig_ = false, healthy_ = false, pinged_ = false;
  volatile uint32_t echoStart_ = 0, echoUs_ = 0;
  volatile bool echoDone_ = false;
};
