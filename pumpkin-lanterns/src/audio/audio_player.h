#pragma once
#include <Arduino.h>

// WAV (PCM 8/16-bit, mono/stereo, 8-48 kHz) from SD -> I2S0 -> internal DAC (GPIO25) -> PAM8302.
// All SD + I2S work happens on a dedicated task on AUDIO_TASK_CORE, so the LED
// loop on core 1 never waits on a card read or a DMA buffer.
namespace Audio {
  bool begin();                              // mounts SD, installs I2S, starts task. false = audio disabled
  bool available();                          // SD mounted + I2S ok
  void play(const char* path);               // interrupts anything playing
  void stop();
  bool isPlaying();
  void setVolume(float v);                   // 0..1
  float volume();
  uint8_t soundCount();                      // cached listing of /sounds/*.wav at begin()
  const char* soundName(uint8_t i);          // full path
  const char* lastError();
}
