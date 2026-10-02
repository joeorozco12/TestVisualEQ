#include "timekeeper.h"
#include "config.h"
#include "sys/log.h"
#include <esp_sntp.h>

namespace {
  volatile uint32_t s_lastSyncMs = 0;
  volatile bool s_synced = false;
  char s_buf[40];
  void onSync(struct timeval*) {
    s_synced = true; s_lastSyncMs = millis();
    LOGI("time", "NTP sync: %s", Timekeeper::localString());
  }
}

void Timekeeper::begin() {
  sntp_set_time_sync_notification_cb(onSync);
  sntp_set_sync_interval(TIME_RESYNC_S * 1000UL);
  configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2);
  LOGI("time", "TZ=%s ntp=%s", TZ_POSIX, NTP_SERVER_1);
}

bool Timekeeper::valid() {
  if (s_synced) return true;
  // Clock may also be valid after a soft reset (RTC keeps time across esp_restart)
  time_t t = time(nullptr);
  const time_t minEpoch = (time_t)(TIME_VALID_MIN_YEAR - 1970) * 365 * 86400;
  return t > minEpoch;
}

time_t Timekeeper::nowUtc() { return time(nullptr); }

bool Timekeeper::localTm(struct tm& out) {
  if (!valid()) return false;
  time_t t = time(nullptr);
  localtime_r(&t, &out);
  return true;
}

const char* Timekeeper::localString() {
  struct tm tm;
  if (!localTm(tm)) return "unsynced";
  strftime(s_buf, sizeof(s_buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
  return s_buf;
}

uint32_t Timekeeper::secondsSinceSync() { return s_synced ? (millis() - s_lastSyncMs) / 1000 : 0xFFFFFFFF; }

void Timekeeper::formatLocal(time_t utc, char* buf, size_t len, const char* fmt) {
  if (!utc || !valid()) { strncpy(buf, "-", len); return; }
  struct tm tm; localtime_r(&utc, &tm);
  strftime(buf, len, fmt, &tm);
}
