// BENCH: SD + WAV + I2S DAC + amp. Lists /sounds, plays each on key '0'-'9', 's' stops, '+'/'-' volume, 't' 440 Hz tone test.
#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "sys/log.h"
#include "audio/audio_player.h"

static void tone440(uint32_t ms) {   // bypasses SD: proves DAC -> amp -> speaker path alone
  i2s_set_sample_rates(I2S_NUM_0, 22050);
  if (AUDIO_AMP_SD_PIN >= 0) digitalWrite(AUDIO_AMP_SD_PIN, HIGH);
  static uint16_t buf[512]; uint32_t n = 22050UL * ms / 1000; float ph = 0;
  for (uint32_t i = 0; i < n; i += 256) {
    for (int j = 0; j < 256; j++) { uint16_t v = (uint16_t)(32768 + 12000 * sinf(ph)); ph += 2 * PI * 440 / 22050; buf[2 * j] = v; buf[2 * j + 1] = v; }
    size_t w; i2s_write(I2S_NUM_0, buf, sizeof(buf), &w, portMAX_DELAY);
  }
  i2s_zero_dma_buffer(I2S_NUM_0); delay(50);
  if (AUDIO_AMP_SD_PIN >= 0) digitalWrite(AUDIO_AMP_SD_PIN, LOW);
}

void setup() {
  Serial.begin(SERIAL_BAUD); delay(300);
  bool ok = Audio::begin();
  Serial.printf("audio %s (%s)\n", ok ? "OK" : "FAILED", Audio::lastError());
  for (uint8_t i = 0; i < Audio::soundCount(); i++) Serial.printf("  %u: %s\n", i, Audio::soundName(i));
  Serial.println("keys: 0-9 play | s stop | +/- volume | t tone");
}

void loop() {
  if (!Serial.available()) { delay(10); return; }
  char k = Serial.read();
  if (k >= '0' && k <= '9' && (k - '0') < Audio::soundCount()) Audio::play(Audio::soundName(k - '0'));
  else if (k == 's') Audio::stop();
  else if (k == '+') { Audio::setVolume(Audio::volume() + 0.1f); Serial.printf("vol %.1f\n", Audio::volume()); }
  else if (k == '-') { Audio::setVolume(Audio::volume() - 0.1f); Serial.printf("vol %.1f\n", Audio::volume()); }
  else if (k == 't') { if (Audio::available()) tone440(1000); else Serial.println("I2S not initialised"); }
}
