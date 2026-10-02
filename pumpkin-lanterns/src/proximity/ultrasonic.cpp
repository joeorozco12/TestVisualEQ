#include "ultrasonic.h"
#include "config.h"
#include "sys/log.h"

void IRAM_ATTR Ultrasonic::echoIsr(void* arg) {
  Ultrasonic* u = static_cast<Ultrasonic*>(arg);
  uint32_t t = micros();
  if (digitalRead(US_ECHO_PIN)) u->echoStart_ = t;
  else if (u->echoStart_) { u->echoUs_ = t - u->echoStart_; u->echoDone_ = true; u->echoStart_ = 0; }
}

void Ultrasonic::begin() {
  pinMode(US_TRIG_PIN, OUTPUT); digitalWrite(US_TRIG_PIN, LOW);
  pinMode(US_ECHO_PIN, INPUT);
  attachInterruptArg(digitalPinToInterrupt(US_ECHO_PIN), echoIsr, this, CHANGE);
  nextPing_ = millis() + 200;
  LOGI("us", "trig=%d echo=%d trigger<%dcm release>%dcm cooldown=%dms", US_TRIG_PIN, US_ECHO_PIN,
       US_TRIGGER_CM, US_RELEASE_CM, US_COOLDOWN_MS);
}

void Ultrasonic::ping() {
  echoDone_ = false; echoStart_ = 0;
  digitalWrite(US_TRIG_PIN, HIGH);
  delayMicroseconds(10);              // the only blocking call in this module
  digitalWrite(US_TRIG_PIN, LOW);
  pinged_ = true;
}

void Ultrasonic::update(uint32_t now) {
  // --- collect echo from previous ping ---
  if (pinged_ && echoDone_) {
    pinged_ = false;
    uint32_t us = echoUs_;
    float cm = us / 58.0f;
    raw_ = cm;
    bool ok = us < US_ECHO_TIMEOUT_US && cm >= US_MIN_CM && cm <= US_MAX_CM;
    if (ok && filtered_ >= 0 && fabsf(cm - filtered_) > US_MAX_JUMP_CM && winN_ >= US_FILTER_N) {
      ok = false;                      // single outlier; next sample will confirm if real
      winN_ = winN_ > 1 ? winN_ - 1 : 0;   // but let a persistent jump win within a few samples
    }
    if (ok) {
      win_[winI_] = cm; winI_ = (winI_ + 1) % US_FILTER_N; if (winN_ < US_FILTER_N) winN_++;
      float tmp[9]; memcpy(tmp, win_, winN_ * sizeof(float));
      for (uint8_t i = 1; i < winN_; i++) { float k = tmp[i]; int8_t j = i - 1; while (j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = k; }
      filtered_ = tmp[winN_ / 2];
      lastValid_ = now;
      if (!healthy_) LOGI("us", "sensor healthy (%.0f cm)", filtered_);
      healthy_ = true;
    } else if (us >= US_ECHO_TIMEOUT_US || cm > US_MAX_CM) {
      // "nothing in range" is a valid reading for presence purposes
      lastValid_ = now;
      healthy_ = true;
      filtered_ = US_MAX_CM;
      winN_ = 0; winI_ = 0;
    }
  } else if (pinged_ && (int32_t)(now - (nextPing_ - US_PING_PERIOD_MS)) > (int32_t)(US_ECHO_TIMEOUT_US / 1000 + 10)) {
    pinged_ = false;                   // no echo edge at all (sensor absent / ECHO wiring open)
  }
  if (healthy_ && now - lastValid_ > US_SENSOR_FAIL_MS) {
    healthy_ = false; presence_ = false; filtered_ = -1; winN_ = 0;
    LOGW("us", "sensor unhealthy: no echo for %d ms (effects run without proximity)", US_SENSOR_FAIL_MS);
  }

  // --- presence FSM with hysteresis + confirm count ---
  if (healthy_ && filtered_ >= 0) {
    if (!presence_) {
      if (filtered_ < US_TRIGGER_CM) { if (++confirm_ >= US_CONFIRM_SAMPLES) {
          presence_ = true; confirm_ = 0;
          if (now - lastTrig_ >= US_COOLDOWN_MS || lastTrig_ == 0) { lastTrig_ = now; pendingTrig_ = true; trigEnvStart_ = now;
            LOGI("us", "TRIGGER at %.0f cm", filtered_); }
          else LOGD("us", "presence but in cooldown (%lu ms left)", (unsigned long)cooldownRemainingMs(now));
        } }
      else confirm_ = 0;
    } else if (filtered_ > US_RELEASE_CM) { presence_ = false; LOGD("us", "released"); }
  }

  // --- reactivity envelope: distance-driven (smoothed) OR trigger decay, whichever is larger ---
  float dr = 0;
  if (healthy_ && filtered_ >= 0) {
    dr = 1.0f - (filtered_ - 30.0f) / (US_RELEASE_CM - 30.0f);
    dr = dr < 0 ? 0 : dr > 0.6f ? 0.6f : dr;        // distance alone never reaches "awake" (0.7)
  }
  distReact_ += 0.08f * (dr - distReact_);
  float tr = 0;
  if (trigEnvStart_) {
    uint32_t dt = now - trigEnvStart_;
    tr = dt >= SCARE_DURATION_MS ? 0 : 1.0f - (float)dt / SCARE_DURATION_MS;
    if (tr == 0) trigEnvStart_ = 0;
  }
  react_ = tr > distReact_ ? tr : distReact_;

  // --- next ping ---
  if ((int32_t)(now - nextPing_) >= 0) { nextPing_ = now + US_PING_PERIOD_MS; ping(); }
}

bool Ultrasonic::takeTrigger() { bool t = pendingTrig_; pendingTrig_ = false; return t; }

uint32_t Ultrasonic::cooldownRemainingMs(uint32_t now) const {
  if (!lastTrig_) return 0;
  uint32_t dt = now - lastTrig_;
  return dt >= US_COOLDOWN_MS ? 0 : US_COOLDOWN_MS - dt;
}

void Ultrasonic::simulateTrigger(uint32_t now) {
  lastTrig_ = now; pendingTrig_ = true; trigEnvStart_ = now;
  LOGI("us", "simulated trigger");
}
