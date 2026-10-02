// BENCH: NTP + TZ/DST + solar calc. Needs WiFi (secrets.h). Prints local time, sun elevation, next on/off.
// Also runs an offline self-test of the solar math against known values (no WiFi needed).
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "secrets.h"
#include "sys/log.h"
#include "dusk/sun.h"
#include "dusk/timekeeper.h"

static uint32_t nextPrint = 0;

static void selfTest() {
  // 2024-06-21 12:00 UTC at lat 0, lon 0: sun near zenith-ish (dec ~23.4 -> el ~66.5). 00:00 UTC -> well below horizon.
  struct tm t{}; t.tm_year = 124; t.tm_mon = 5; t.tm_mday = 21; t.tm_hour = 12;
  time_t noon = mktime(&t) - 0;  // TZ not set yet -> mktime treats as UTC-ish (TZ default UTC0)
  float e1 = Sun::elevationDeg(noon, 0, 0), e2 = Sun::elevationDeg(noon - 12 * 3600, 0, 0);
  Serial.printf("selftest: equator noon el=%.1f (expect ~66.5), midnight el=%.1f (expect ~-66.5)\n", e1, e2);
  // Detroit 2024-10-31: sunset ~18:28 EDT = 22:28 UTC. Expect geometric (-0.83 deg) crossing near that.
  struct tm d{}; d.tm_year = 124; d.tm_mon = 9; d.tm_mday = 31; d.tm_hour = 12;
  time_t from = mktime(&d);
  time_t set = Sun::nextCrossing(from, 42.3314, -83.0458, -0.833f, false);
  struct tm r; gmtime_r(&set, &r);
  Serial.printf("selftest: Detroit 2024-10-31 sunset = %02d:%02d UTC (expect ~22:28)\n", r.tm_hour, r.tm_min);
}

void setup() {
  Serial.begin(SERIAL_BAUD); delay(300);
  setenv("TZ", "UTC0", 1); tzset();
  selfTest();
  Timekeeper::begin();
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("connecting to %s ... lat=%.4f lon=%.4f TZ=%s\n", WIFI_SSID, GEO_LAT, GEO_LON, TZ_POSIX);
}

void loop() {
  uint32_t now = millis();
  if (now >= nextPrint) {
    nextPrint = now + 5000;
    if (!Timekeeper::valid()) { Serial.printf("wifi=%d time unsynced (boot without RTC: this is the state the fallback must cover)\n", WiFi.status() == WL_CONNECTED); return; }
    time_t t = Timekeeper::nowUtc();
    float el = Sun::elevationDeg(t, GEO_LAT, GEO_LON);
    time_t on = Sun::nextCrossing(t, GEO_LAT, GEO_LON, SUN_ON_ELEVATION_DEG, false);
    time_t off = Sun::nextCrossing(t, GEO_LAT, GEO_LON, SUN_OFF_ELEVATION_DEG, true);
    char a[32], b[32];
    Timekeeper::formatLocal(on, a, sizeof(a), "%a %H:%M"); Timekeeper::formatLocal(off, b, sizeof(b), "%a %H:%M");
    Serial.printf("%s | sun el=%.2f deg | next ON %s | next OFF %s | synced %lus ago\n", Timekeeper::localString(), el, a, b, (unsigned long)Timekeeper::secondsSinceSync());
  }
}
