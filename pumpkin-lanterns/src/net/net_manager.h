#pragma once
#include <Arduino.h>
#include "commands.h"

// WiFi (non-blocking reconnect), mDNS, web UI, MQTT, UDP node sync.
// Runs on its own task (core 0). Main loop talks to it via:
//   - Net::popCommand()      commands from any remote source
//   - Net::setState()        periodic snapshot for UI/MQTT/beacons
//   - Net::broadcast()       re-send a locally applied command to the fleet
// Every function is a no-op when NET_ENABLED is false or WiFi is down.
namespace Net {
  void begin();
  bool connected();
  bool popCommand(Command& out);           // non-blocking
  void setState(const NodeState& s);       // copy under mutex
  void broadcast(const Command& c);        // UDP CMD to all nodes
  bool isLeader();                         // lowest node id seen on the LAN (or alone)
  uint8_t nodesSeen();
  const char* ip();
  int rssi();
  bool mqttConnected();
}
