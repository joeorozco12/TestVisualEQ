#include "log.h"
#include <stdarg.h>

static SemaphoreHandle_t s_mutex = nullptr;

void logPrintf(char lvl, const char* tag, const char* fmt, ...) {
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    Serial.printf("[%8lu] %c %-5s %s\n", (unsigned long)millis(), lvl, tag, buf);
    xSemaphoreGive(s_mutex);
  }
}
