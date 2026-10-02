#pragma once
#include <Arduino.h>
#include <time.h>

// NTP + POSIX-TZ wall clock. DST is handled entirely by the TZ string.
// Clock keeps running across WiFi loss (RTC slow clock, ~±5 min/day worst case);
// re-syncs automatically when WiFi returns.
namespace Timekeeper {
  void begin();                     // configures SNTP + TZ; harmless before WiFi is up
  bool valid();                     // true once synced at least once since power-on
  time_t nowUtc();
  bool localTm(struct tm& out);     // local broken-down time; false if !valid()
  const char* localString();        // "YYYY-MM-DD HH:MM:SS TZ" or "unsynced" (main-task use; static buffer)
  void formatLocal(time_t utc, char* buf, size_t len, const char* fmt = "%a %H:%M");  // thread-safe, "-" if 0
  uint32_t secondsSinceSync();      // 0xFFFFFFFF if never
}
