#include "net_manager.h"
#include "config.h"
#include "secrets.h"
#include "web_page.h"
#include "sys/log.h"
#include "sys/node.h"
#include "effects/effect.h"
#include "audio/audio_player.h"
#include "dusk/timekeeper.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

namespace {
  // ---- shared with main ----
  QueueHandle_t s_cmdQ = nullptr, s_txQ = nullptr;
  SemaphoreHandle_t s_mx = nullptr;
  NodeState s_state;
  volatile bool s_connected = false, s_mqttOk = false;
  char s_ip[16] = "0.0.0.0";

  // ---- net-task-only objects ----
  WebServer* s_web = nullptr;
  WiFiClient s_wifiClient;
  PubSubClient* s_mqtt = nullptr;
  WiFiUDP s_udp;
  uint32_t s_nextWifiRetry = 0, s_nextMqtt = 0, s_nextMqttState = 0, s_nextBeacon = 0;
  bool s_servicesUp = false;
  char s_topicCmd[48], s_topicAll[48], s_topicState[48], s_topicStatus[48];

  // ---- sync ----
  struct __attribute__((packed)) SyncPacket {
    uint32_t magic; uint8_t ver, type, nodeId;
    uint8_t effect, brightness, mode, speed, hue, rotate;
    uint16_t seed; uint32_t elapsedMs;
    uint8_t cmdType; int32_t cmdValue; char cmdStr[32];
  };
  constexpr uint32_t SYNC_MAGIC = 0x4E4B4D50;  // 'PMKN'
  constexpr uint8_t PKT_BEACON = 1, PKT_CMD = 2;
  struct Peer { uint8_t id; uint32_t lastSeen; };
  Peer s_peers[NODE_MAX_COUNT]; uint8_t s_nPeers = 0;

  void notePeer(uint8_t id, uint32_t now) {
    for (uint8_t i = 0; i < s_nPeers; i++) if (s_peers[i].id == id) { s_peers[i].lastSeen = now; return; }
    if (s_nPeers < NODE_MAX_COUNT) s_peers[s_nPeers++] = {id, now};
  }
  void expirePeers(uint32_t now) {
    for (uint8_t i = 0; i < s_nPeers;) {
      if (now - s_peers[i].lastSeen > SYNC_LEADER_TIMEOUT_MS) { LOGI("sync", "peer %u gone", s_peers[i].id); s_peers[i] = s_peers[--s_nPeers]; }
      else i++;
    }
  }
  bool leaderNow() {
    for (uint8_t i = 0; i < s_nPeers; i++) if (s_peers[i].id < Node::id()) return false;
    return true;
  }

  void pushCmd(const Command& c) { if (s_cmdQ) xQueueSend(s_cmdQ, &c, 0); }

  NodeState snapshot() {
    NodeState s;
    if (xSemaphoreTake(s_mx, pdMS_TO_TICKS(50)) == pdTRUE) { s = s_state; xSemaphoreGive(s_mx); }
    return s;
  }

  // ---- command parsing (shared by web + MQTT) ----
  Command::Type typeFromStr(const char* t) {
    if (!strcmp(t, "effect")) return Command::EFFECT;
    if (!strcmp(t, "next")) return Command::NEXT;
    if (!strcmp(t, "brightness")) return Command::BRIGHTNESS;
    if (!strcmp(t, "mode")) return Command::MODE;
    if (!strcmp(t, "scare")) return Command::SCARE;
    if (!strcmp(t, "speed")) return Command::SPEED;
    if (!strcmp(t, "hue")) return Command::HUE;
    if (!strcmp(t, "rotate")) return Command::ROTATE;
    if (!strcmp(t, "play")) return Command::PLAY;
    if (!strcmp(t, "volume")) return Command::VOLUME;
    if (!strcmp(t, "reboot")) return Command::REBOOT;
    return Command::NONE;
  }

  void buildStateJson(String& out) {
    NodeState s = snapshot();
    JsonDocument d;
    d["name"] = Node::name(); d["id"] = Node::id(); d["fw"] = FW_NAME " " FW_VERSION;
    d["effect"] = s.effect; d["elapsed"] = s.effectElapsedMs; d["scare"] = s.scare;
    d["brightness"] = s.brightness; d["speed"] = s.speed; d["hue"] = s.hue; d["rotate"] = s.rotate;
    d["mode"] = s.mode; d["lightsOn"] = s.lightsOn; d["source"] = s.source;
    if (!isnan(s.sunElev)) d["sunElev"] = s.sunElev; else d["sunElev"] = nullptr;
    d["lux"] = s.lux; d["distance"] = s.distanceCm; d["reactivity"] = s.reactivity;
    d["usOk"] = s.usHealthy; d["lightOk"] = s.lightHealthy; d["audioOk"] = s.audioOk; d["playing"] = s.playing;
    d["volume"] = (int)(Audio::volume() * 100); d["ledMa"] = s.ledMilliamps;
    char b[24]; Timekeeper::formatLocal(s.nextSunset, b, sizeof(b)); d["nextSunset"] = b;
    Timekeeper::formatLocal(s.nextSunrise, b, sizeof(b)); d["nextSunrise"] = b;
    Timekeeper::formatLocal(Timekeeper::nowUtc(), b, sizeof(b), "%Y-%m-%d %H:%M:%S"); d["time"] = Timekeeper::valid() ? b : "unsynced";
    d["ip"] = s_ip; d["rssi"] = WiFi.RSSI(); d["mqtt"] = s_mqttOk; d["leader"] = leaderNow(); d["nodes"] = s_nPeers + 1;
    d["uptime"] = Node::uptimeS(); d["heap"] = ESP.getFreeHeap();
    JsonArray fx = d["effects"].to<JsonArray>();
    for (uint8_t i = 0; i < EFFECT_COUNT; i++) fx.add(EFFECT_TABLE[i].name);
    JsonArray snd = d["sounds"].to<JsonArray>();
    for (uint8_t i = 0; i < Audio::soundCount(); i++) snd.add(Audio::soundName(i));
    serializeJson(d, out);
  }

  // ---- web ----
  void webSetup() {
    s_web = new WebServer(WEB_PORT);
    s_web->on("/", HTTP_GET, []() { s_web->send_P(200, "text/html", WEB_PAGE); });
    s_web->on("/api/state", HTTP_GET, []() { String j; buildStateJson(j); s_web->send(200, "application/json", j); });
    s_web->on("/api/cmd", HTTP_ANY, []() {
      Command c; c.type = typeFromStr(s_web->arg("type").c_str());
      c.value = s_web->arg("value").toInt();
      strncpy(c.str, s_web->arg("str").c_str(), sizeof(c.str) - 1);
      if (c.type == Command::NONE) { s_web->send(400, "text/plain", "bad type"); return; }
      pushCmd(c); s_web->send(200, "text/plain", "ok");
    });
    s_web->onNotFound([]() { s_web->send(404, "text/plain", "nope"); });
    s_web->begin();
  }

  // ---- MQTT ----
  void mqttCallback(char* topic, uint8_t* payload, unsigned int len) {
    JsonDocument d;
    if (deserializeJson(d, payload, len)) { LOGW("mqtt", "bad json on %s", topic); return; }
    Command c;
    if (d["effect"].is<const char*>())     { c.type = Command::EFFECT; strncpy(c.str, d["effect"], sizeof(c.str) - 1); pushCmd(c); }
    if (d["brightness"].is<int>())         { c = Command(); c.type = Command::BRIGHTNESS; c.value = d["brightness"]; pushCmd(c); }
    if (d["speed"].is<int>())              { c = Command(); c.type = Command::SPEED; c.value = d["speed"]; pushCmd(c); }
    if (d["hue"].is<int>())                { c = Command(); c.type = Command::HUE; c.value = d["hue"]; pushCmd(c); }
    if (d["rotate"].is<bool>())            { c = Command(); c.type = Command::ROTATE; c.value = d["rotate"] ? 1 : 0; pushCmd(c); }
    if (d["volume"].is<int>())             { c = Command(); c.type = Command::VOLUME; c.value = d["volume"]; pushCmd(c); }
    if (d["play"].is<const char*>())       { c = Command(); c.type = Command::PLAY; strncpy(c.str, d["play"], sizeof(c.str) - 1); pushCmd(c); }
    if (d["next"].is<bool>() && d["next"]) { c = Command(); c.type = Command::NEXT; pushCmd(c); }
    if (d["scare"].is<bool>() && d["scare"]) { c = Command(); c.type = Command::SCARE; c.value = 1; pushCmd(c); }
    if (d["mode"].is<const char*>()) {
      const char* m = d["mode"]; c = Command(); c.type = Command::MODE;
      c.value = !strcmp(m, "on") ? 1 : !strcmp(m, "off") ? 2 : 0; pushCmd(c);
    }
    if (d["reboot"].is<bool>() && d["reboot"]) { c = Command(); c.type = Command::REBOOT; pushCmd(c); }
  }

  void mqttPublishState() {
    if (!s_mqtt || !s_mqtt->connected()) return;
    String j; buildStateJson(j);
    s_mqtt->publish(s_topicState, j.c_str(), false);
  }

  void mqttLoop(uint32_t now) {
#if MQTT_ENABLED
    if (!s_mqtt || strlen(MQTT_HOST) == 0) return;
    if (!s_mqtt->connected()) {
      s_mqttOk = false;
      if ((int32_t)(now - s_nextMqtt) < 0) return;
      s_nextMqtt = now + MQTT_RETRY_MS;
      bool ok = s_mqtt->connect(Node::hostname(), strlen(MQTT_USER) ? MQTT_USER : nullptr,
                                strlen(MQTT_PASSWORD) ? MQTT_PASSWORD : nullptr, s_topicStatus, 1, true, "offline");
      if (!ok) { LOGW("mqtt", "connect failed rc=%d", s_mqtt->state()); return; }
      s_mqtt->publish(s_topicStatus, "online", true);
      s_mqtt->subscribe(s_topicCmd); s_mqtt->subscribe(s_topicAll);
      s_mqttOk = true; LOGI("mqtt", "connected to %s, cmd topics %s, %s", MQTT_HOST, s_topicCmd, s_topicAll);
      mqttPublishState();
    }
    s_mqtt->loop();
    if ((int32_t)(now - s_nextMqttState) >= 0) { s_nextMqttState = now + MQTT_STATE_PERIOD_MS; mqttPublishState(); }
#endif
  }

  // ---- UDP sync ----
  void sendPacket(SyncPacket& p) {
    p.magic = SYNC_MAGIC; p.ver = 1; p.nodeId = Node::id();
    IPAddress bc = WiFi.localIP(); bc[3] = 255;      // /24 directed broadcast (works on consumer routers)
    s_udp.beginPacket(bc, SYNC_UDP_PORT); s_udp.write((uint8_t*)&p, sizeof(p)); s_udp.endPacket();
  }

  void syncLoop(uint32_t now) {
#if SYNC_ENABLED
    int len;
    while ((len = s_udp.parsePacket()) > 0) {
      SyncPacket p;
      if (len != sizeof(p)) { s_udp.flush(); continue; }
      s_udp.read((uint8_t*)&p, sizeof(p));
      if (p.magic != SYNC_MAGIC || p.ver != 1 || p.nodeId == Node::id()) continue;
      notePeer(p.nodeId, now);
      if (p.type == PKT_BEACON) {
        if (p.nodeId < Node::id()) {                       // only follow a lower-id leader
          Command c; c.type = Command::EFFECT; c.origin = p.nodeId; c.value = (int32_t)p.elapsedMs;
          strncpy(c.str, p.effect < EFFECT_COUNT ? EFFECT_TABLE[p.effect].name : "", sizeof(c.str) - 1);
          c.aux = p.seed;
          pushCmd(c);
          Command b; b.type = Command::BRIGHTNESS; b.origin = p.nodeId; b.value = p.brightness; pushCmd(b);
          Command m; m.type = Command::MODE; m.origin = p.nodeId; m.value = p.mode; pushCmd(m);
        }
      } else if (p.type == PKT_CMD) {
        Command c; c.type = (Command::Type)p.cmdType; c.value = p.cmdValue; c.origin = p.nodeId;
        strncpy(c.str, p.cmdStr, sizeof(c.str) - 1); c.str[sizeof(c.str) - 1] = 0;
        if (c.type == Command::SCARE && !SYNC_FOLLOW_SCARE) continue;
        pushCmd(c);
      }
    }
    expirePeers(now);
    if (leaderNow() && (int32_t)(now - s_nextBeacon) >= 0) {
      s_nextBeacon = now + SYNC_HEARTBEAT_MS;
      NodeState s = snapshot();
      SyncPacket p{}; p.type = PKT_BEACON; p.effect = s.effectIdx; p.brightness = s.brightness; p.mode = s.mode;
      p.speed = s.speed; p.hue = s.hue; p.rotate = s.rotate; p.seed = s.seed; p.elapsedMs = s.effectElapsedMs;
      if (s.scare) p.effect = 0xFF;                        // don't drag followers into our scare via beacon
      if (p.effect != 0xFF) sendPacket(p);
    }
#endif
  }

  void servicesUp() {
    snprintf(s_ip, sizeof(s_ip), "%s", WiFi.localIP().toString().c_str());
    LOGI("net", "WiFi up: %s rssi=%d", s_ip, WiFi.RSSI());
    if (!s_servicesUp) {
      if (MDNS.begin(Node::hostname())) { MDNS.addService("http", "tcp", WEB_PORT); LOGI("net", "http://%s.local", Node::hostname()); }
      webSetup();
      s_udp.begin(SYNC_UDP_PORT);
      s_servicesUp = true;
    }
    s_nextMqtt = 0;
  }

  void wifiLoop(uint32_t now) {
    bool up = WiFi.status() == WL_CONNECTED;
    if (up && !s_connected) { s_connected = true; servicesUp(); }
    if (!up && s_connected) { s_connected = false; s_mqttOk = false; LOGW("net", "WiFi lost; standalone until it returns"); s_nextWifiRetry = now + WIFI_RETRY_MS; }
    if (!up && (int32_t)(now - s_nextWifiRetry) >= 0) {
      s_nextWifiRetry = now + WIFI_RETRY_MS;
      LOGI("net", "WiFi connecting to %s", WIFI_SSID);
      WiFi.disconnect(false, false);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  void netTask(void*) {
    Node::wdtAddCurrentTask();
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(Node::hostname());
    WiFi.setSleep(false);               // no modem sleep: steadier UDP + keeps power bank loaded (~+60 mA)
    WiFi.setAutoReconnect(false);       // we own the retry policy
    s_nextWifiRetry = 0;
#if MQTT_ENABLED
    s_mqtt = new PubSubClient(s_wifiClient);
    s_mqtt->setServer(MQTT_HOST, MQTT_PORT); s_mqtt->setCallback(mqttCallback);
    s_mqtt->setBufferSize(1024); s_mqtt->setSocketTimeout(3); s_mqtt->setKeepAlive(30);
    s_wifiClient.setTimeout(3);
#endif
    for (;;) {
      Node::wdtFeed();
      uint32_t now = millis();
      wifiLoop(now);
      if (s_connected) {
        s_web->handleClient(); mqttLoop(now); syncLoop(now);
        SyncPacket tx; while (xQueueReceive(s_txQ, &tx, 0) == pdTRUE) sendPacket(tx);
      } else { SyncPacket tx; while (xQueueReceive(s_txQ, &tx, 0) == pdTRUE) {} }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void Net::begin() {
  s_cmdQ = xQueueCreate(16, sizeof(Command));
  s_txQ = xQueueCreate(8, sizeof(SyncPacket));
  s_mx = xSemaphoreCreateMutex();
  snprintf(s_topicCmd, sizeof(s_topicCmd), "%s/%u/cmd", MQTT_BASE_TOPIC, Node::id());
  snprintf(s_topicAll, sizeof(s_topicAll), "%s/all/cmd", MQTT_BASE_TOPIC);
  snprintf(s_topicState, sizeof(s_topicState), "%s/%u/state", MQTT_BASE_TOPIC, Node::id());
  snprintf(s_topicStatus, sizeof(s_topicStatus), "%s/%u/status", MQTT_BASE_TOPIC, Node::id());
#if NET_ENABLED
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
  LOGI("net", "task started on core 0");
#else
  LOGI("net", "disabled");
#endif
}

bool Net::connected() { return s_connected; }
bool Net::popCommand(Command& out) { return s_cmdQ && xQueueReceive(s_cmdQ, &out, 0) == pdTRUE; }
void Net::setState(const NodeState& s) {
  if (s_mx && xSemaphoreTake(s_mx, pdMS_TO_TICKS(5)) == pdTRUE) { s_state = s; xSemaphoreGive(s_mx); }
}
void Net::broadcast(const Command& c) {
#if NET_ENABLED && SYNC_ENABLED
  if (!s_connected) return;
  SyncPacket p{}; p.type = PKT_CMD; p.cmdType = c.type; p.cmdValue = c.value;
  strncpy(p.cmdStr, c.str, sizeof(p.cmdStr) - 1);
  xQueueSend(s_txQ, &p, 0);   // sent by the net task; WiFiUDP is not shared across tasks
#endif
}
bool Net::isLeader() { return leaderNow(); }
uint8_t Net::nodesSeen() { return s_nPeers + 1; }
const char* Net::ip() { return s_ip; }
int Net::rssi() { return s_connected ? WiFi.RSSI() : 0; }
bool Net::mqttConnected() { return s_mqttOk; }
