// BENCH: WiFi reconnect policy, web UI, MQTT, UDP sync, leader election. No sensors needed.
// Open http://pumpkin-<id>.local, send MQTT to pumpkin/all/cmd, run on 2+ boards and watch leader/follower.
#include <Arduino.h>
#include "config.h"
#include "sys/log.h"
#include "sys/node.h"
#include "net/net_manager.h"
#include "dusk/timekeeper.h"
#include "effects/engine.h"

static EffectEngine engine; static uint32_t nextPrint = 0, nextState = 0;

void setup() {
  Serial.begin(SERIAL_BAUD); delay(300);
  Node::begin(); engine.begin(); engine.setEffect("rainbow", false);
  Timekeeper::begin(); Net::begin();
  Serial.println("Pull the router / walk out of range to test reconnect; LEDs must never stutter.");
}

void loop() {
  uint32_t now = millis();
  Node::wdtFeed();
  Command c;
  while (Net::popCommand(c)) {
    Serial.printf("cmd type=%u value=%ld str='%s' origin=%u aux=%u\n", c.type, (long)c.value, c.str, c.origin, c.aux);
    if (c.type == Command::EFFECT) { engine.setEffect(c.str); if (c.origin && c.value > 0) engine.alignStart(c.value, now, c.aux); }
    if (c.type == Command::BRIGHTNESS) engine.setBrightness(c.value);
    if (c.type == Command::NEXT) { engine.setEffect((engine.effectIndex() + 1) % EFFECT_COUNT); if (!c.origin) Net::broadcast(c); }
    if (c.type == Command::SCARE) engine.triggerScare("wakeup", 3000);
    if (c.type == Command::REBOOT) ESP.restart();
  }
  engine.update(now);
  if (now >= nextState) {
    nextState = now + 500;
    NodeState s; strncpy(s.effect, engine.effectName(), sizeof(s.effect) - 1); s.effectIdx = engine.effectIndex();
    s.effectElapsedMs = engine.effectElapsed(now); s.seed = engine.seed(); s.brightness = engine.brightness(); s.lightsOn = true; s.rotate = false;
    Net::setState(s);
  }
  if (now >= nextPrint) {
    nextPrint = now + 5000;
    Serial.printf("wifi=%d ip=%s rssi=%d mqtt=%d leader=%d nodes=%u time=%s heap=%lu loopmax=%lu ms\n",
                  Net::connected(), Net::ip(), Net::rssi(), Net::mqttConnected(), Net::isLeader(), Net::nodesSeen(),
                  Timekeeper::localString(), (unsigned long)ESP.getFreeHeap(), (unsigned long)Node::loopMaxMs());
  }
  Node::loopTick();
}
