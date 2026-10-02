#pragma once
#include <Arduino.h>

// Commands flow: (web | MQTT | UDP sync | button) -> queue -> main loop applies.
// Local-origin commands are rebroadcast over UDP so every node follows.
struct Command {
  enum Type : uint8_t { NONE = 0, EFFECT, NEXT, BRIGHTNESS, MODE, SCARE, SPEED, HUE, ROTATE, PLAY, VOLUME, REBOOT };
  Type type = NONE;
  int32_t value = 0;          // brightness / mode (0 auto,1 on,2 off) / speed / hue / rotate / volume*100 / scare: 1=with sound
  char str[32] = {0};         // effect name / sound path
  uint8_t origin = 0;         // 0 = local, else node id of sender
  uint16_t aux = 0;           // sync beacons: leader's PRNG seed
};

// Snapshot of what the node is doing, for the web UI / MQTT / sync beacons.
struct NodeState {
  char effect[16] = "none";
  uint8_t effectIdx = 0;
  uint32_t effectElapsedMs = 0;
  uint16_t seed = 0;
  uint8_t brightness = 0, speed = 128, hue = 0;
  uint8_t mode = 0;            // 0 auto, 1 force on, 2 force off
  bool lightsOn = false, rotate = true, scare = false;
  char source[10] = "none";    // sun/light/fallback/manual
  float sunElev = NAN, lux = -1, distanceCm = -1, reactivity = 0;
  bool usHealthy = false, lightHealthy = false, audioOk = false, playing = false;
  uint32_t ledMilliamps = 0;
  time_t nextSunset = 0, nextSunrise = 0;
};
