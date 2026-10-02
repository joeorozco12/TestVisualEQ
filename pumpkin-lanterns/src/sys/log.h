#pragma once
#include <Arduino.h>

// Tiny tagged logger. Compiles to Serial.printf with uptime + tag prefix.
// Usage: LOGI("net", "connected rssi=%d", rssi);
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERR  1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DBG  4
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

void logPrintf(char lvl, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

#if LOG_LEVEL >= LOG_LEVEL_ERR
#define LOGE(tag, ...) logPrintf('E', tag, __VA_ARGS__)
#else
#define LOGE(tag, ...) do {} while (0)
#endif
#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOGW(tag, ...) logPrintf('W', tag, __VA_ARGS__)
#else
#define LOGW(tag, ...) do {} while (0)
#endif
#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOGI(tag, ...) logPrintf('I', tag, __VA_ARGS__)
#else
#define LOGI(tag, ...) do {} while (0)
#endif
#if LOG_LEVEL >= LOG_LEVEL_DBG
#define LOGD(tag, ...) logPrintf('D', tag, __VA_ARGS__)
#else
#define LOGD(tag, ...) do {} while (0)
#endif
