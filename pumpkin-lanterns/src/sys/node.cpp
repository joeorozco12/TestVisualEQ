#include "node.h"
#include "log.h"
#include "config.h"
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

namespace {
  uint8_t s_mac[6];
  uint8_t s_id = 0;
  char s_name[24] = "pumpkin-0";
  char s_host[16] = "pumpkin-0";
  uint32_t s_lastLoop = 0, s_maxLoop = 0, s_lastHeapWarn = 0;
  const NodeMacEntry s_table[] = NODE_MAC_TABLE;
}

void Node::begin() {
  WiFi.macAddress(s_mac);
  s_id = s_mac[5];  // default: last MAC byte (unique enough on one LAN)
  const char* tname = nullptr;
  for (size_t i = 0; i < sizeof(s_table) / sizeof(s_table[0]); i++) {
    if (memcmp(s_table[i].mac, s_mac, 6) == 0) { s_id = s_table[i].id; tname = s_table[i].name; break; }
  }
  snprintf(s_host, sizeof(s_host), "pumpkin-%u", s_id);
  if (tname) snprintf(s_name, sizeof(s_name), "%s", tname);
  else       strncpy(s_name, s_host, sizeof(s_name));

  esp_task_wdt_init(WDT_TIMEOUT_S, true);  // panic -> reset on timeout
  wdtAddCurrentTask();
  LOGI("node", "%s %s id=%u mac=%02X:%02X:%02X:%02X:%02X:%02X reset=%s",
       FW_NAME, FW_VERSION, s_id, s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5], resetReason());
}

uint8_t Node::id() { return s_id; }
const char* Node::name() { return s_name; }
const char* Node::hostname() { return s_host; }
const uint8_t* Node::mac() { return s_mac; }

void Node::wdtAddCurrentTask() { esp_task_wdt_add(NULL); }
void Node::wdtFeed() { esp_task_wdt_reset(); }

void Node::loopTick() {
  uint32_t now = millis();
  if (s_lastLoop) {
    uint32_t dt = now - s_lastLoop;
    if (dt > s_maxLoop) s_maxLoop = dt;
    if (dt > LOOP_OVERRUN_WARN_MS) LOGW("node", "loop overrun %lu ms", (unsigned long)dt);
  }
  s_lastLoop = now;
  if (now - s_lastHeapWarn > 60000) {
    s_lastHeapWarn = now;
    uint32_t fh = ESP.getFreeHeap();
    if (fh < HEAP_MIN_FREE_WARN) LOGW("node", "low heap: %lu", (unsigned long)fh);
  }
}

uint32_t Node::loopMaxMs() { uint32_t m = s_maxLoop; s_maxLoop = 0; return m; }
uint32_t Node::uptimeS() { return millis() / 1000; }

const char* Node::resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default: return "other";
  }
}
