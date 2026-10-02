#pragma once
#include <Arduino.h>

// Node identity + health + watchdog helpers.
namespace Node {
  void begin();                      // resolves id/name from MAC table, inits WDT on calling task
  uint8_t id();
  const char* name();                // "pumpkin-<id>" or table name
  const char* hostname();            // "pumpkin-<id>" always (mDNS-safe)
  const uint8_t* mac();

  void wdtAddCurrentTask();          // call from any task that must be watchdog-supervised
  void wdtFeed();                    // call every loop iteration

  // Loop health: call once per loop iteration. Logs overruns and low heap.
  void loopTick();
  uint32_t loopMaxMs();              // worst iteration since last read (then resets)
  uint32_t uptimeS();
  const char* resetReason();
}
