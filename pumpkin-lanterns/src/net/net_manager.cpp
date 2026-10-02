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
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

namespace {
  // ---- shared with main ----
  QueueHandle_t s_cmdQ = nullptr, s_txQ = nullptr;
  SemaphoreHandle_t s_mx = nullptr;
  NodeState s_state;
  volatile bool s_connected = false, s_mqttOk = false, s_portal = false, s_portalReq = false;
  char s_ip[16] = "0.0.0.0";
  struct { float windKmh = 0, rainMm = 0; bool valid = false; uint32_t at = 0; } s_wx;

  // ---- provisioning (NVS, falls back to secrets.h) ----
  struct { char ssid[33], pass[65], mqttHost[64], mqttUser[32], mqttPass[64]; } s_cfg;
  Preferences s_prefs;
  void loadCfg() {
    s_prefs.begin("net", true);
    strlcpy(s_cfg.ssid, s_prefs.getString("ssid", WIFI_SSID).c_str(), sizeof(s_cfg.ssid));
    strlcpy(s_cfg.pass, s_prefs.getString("pass", WIFI_PASS).c_str(), sizeof(s_cfg.pass));
    strlcpy(s_cfg.mqttHost, s_prefs.getString("mh", MQTT_HOST).c_str(), sizeof(s_cfg.mqttHost));
    strlcpy(s_cfg.mqttUser, s_prefs.getString("mu", MQTT_USER).c_str(), sizeof(s_cfg.mqttUser));
    strlcpy(s_cfg.mqttPass, s_prefs.getString("mp", MQTT_PASSWORD).c_str(), sizeof(s_cfg.mqttPass));
    s_prefs.end();
  }
  void saveCfg(const String& ssid, const String& pass, const String& mh, const String& mu, const String& mp) {
    s_prefs.begin("net", false);
    s_prefs.putString("ssid", ssid); if (pass.length()) s_prefs.putString("pass", pass);
    s_prefs.putString("mh", mh); s_prefs.putString("mu", mu); if (mp.length()) s_prefs.putString("mp", mp);
    s_prefs.end();
  }

  // ---- net-task-only objects ----
  WebServer* s_web = nullptr;
  DNSServer* s_dns = nullptr;
  WiFiClient s_wifiClient;
  PubSubClient* s_mqtt = nullptr;
  WiFiUDP s_udp;
  uint32_t s_nextWifiRetry = 0, s_nextMqtt = 0, s_nextMqttState = 0, s_nextBeacon = 0, s_nextWx = 0, s_staSince = 0;
  bool s_servicesUp = false, s_webUp = false, s_otaUp = false;
  char s_topicCmd[48], s_topicAll[48], s_topicState[48], s_topicStatus[48];

  // ---- sync ----
  struct __attribute__((packed)) SyncPacket {
    uint32_t magic; uint8_t ver, type, nodeId;
    uint8_t effect, brightness, mode, speed, hue, rotate;
    uint16_t seed; uint32_t elapsedMs;
    uint8_t cmdType; int32_t cmdValue; uint16_t cmdAux; char cmdStr[32];
  };
  constexpr uint32_t SYNC_MAGIC = 0x4E4B4D50;  // 'PMKN'
  constexpr uint8_t PKT_BEACON = 1, PKT_CMD = 2;
  struct Peer { uint8_t id; uint32_t lastSeen; };
  Peer s_peers[NODE_MAX_COUNT]; uint8_t s_nPeers = 0;

  void notePeer(uint8_t id, uint32_t now) {
    for (uint8_t i = 0; i < s_nPeers; i++) if (s_peers[i].id == id) { s_peers[i].lastSeen = now; return; }
    if (s_nPeers < NODE_MAX_COUNT) { s_peers[s_nPeers++] = {id, now}; LOGI("sync", "peer %u joined (%u nodes)", id, s_nPeers + 1); }
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
  uint8_t slotNow() {
    uint8_t s = 0;
    for (uint8_t i = 0; i < s_nPeers; i++) if (s_peers[i].id < Node::id()) s++;
    return s;
  }

  void pushCmd(const Command& c) { if (s_cmdQ) xQueueSend(s_cmdQ, &c, 0); }

  NodeState snapshot() {
    NodeState s;
    if (xSemaphoreTake(s_mx, pdMS_TO_TICKS(50)) == pdTRUE) { s = s_state; xSemaphoreGive(s_mx); }
    return s;
  }

  // ---- command parsing (shared by web + MQTT) ----
  Command::Type typeFromStr(const char* t) {
    static const struct { const char* n; Command::Type t; } T[] = {
      {"effect", Command::EFFECT}, {"next", Command::NEXT}, {"brightness", Command::BRIGHTNESS}, {"mode", Command::MODE},
      {"scare", Command::SCARE}, {"speed", Command::SPEED}, {"hue", Command::HUE}, {"rotate", Command::ROTATE},
      {"play", Command::PLAY}, {"volume", Command::VOLUME}, {"reboot", Command::REBOOT}, {"text", Command::TEXT},
      {"ambient", Command::AMBIENT}, {"portal", Command::PORTAL}};
    for (auto& e : T) if (!strcmp(t, e.n)) return e.t;
    return Command::NONE;
  }

  void buildStateJson(String& out, bool forHa = false) {
    NodeState s = snapshot();
    JsonDocument d;
    d["name"] = Node::name(); d["id"] = Node::id(); d["fw"] = FW_NAME " " FW_VERSION;
    d["state"] = s.lightsOn ? "ON" : "OFF";                      // Home Assistant json schema
    d["effect"] = s.effect; d["elapsed"] = s.effectElapsedMs; d["scare"] = s.scare; d["scareLevel"] = s.scareLevel;
    d["brightness"] = s.brightness; d["speed"] = s.speed; d["hue"] = s.hue; d["rotate"] = s.rotate;
    d["mode"] = s.mode; d["lightsOn"] = s.lightsOn; d["source"] = s.source;
    d["scene"] = s.scene; d["scaresEnabled"] = s.scaresEnabled; d["idle"] = s.idle; d["alert"] = s.alert;
    d["slot"] = s.slot; d["nodes"] = s.fleet; d["text"] = s.text;
    if (!isnan(s.sunElev)) d["sunElev"] = s.sunElev; else d["sunElev"] = nullptr;
    d["lux"] = s.lux; d["distance"] = s.distanceCm; d["reactivity"] = s.reactivity; d["mic"] = s.mic;
    d["usOk"] = s.usHealthy; d["lightOk"] = s.lightHealthy; d["audioOk"] = s.audioOk; d["playing"] = s.playing;
    d["ambient"] = Audio::ambientEnabled(); d["volume"] = (int)(Audio::volume() * 100); d["ledMa"] = s.ledMilliamps;
    d["vbus"] = s.vbusMv; d["wxOk"] = s_wx.valid; d["windKmh"] = s_wx.windKmh; d["rainMm"] = s_wx.rainMm;
    char b[24]; Timekeeper::formatLocal(s.nextSunset, b, sizeof(b)); d["nextSunset"] = b;
    Timekeeper::formatLocal(s.nextSunrise, b, sizeof(b)); d["nextSunrise"] = b;
    Timekeeper::formatLocal(Timekeeper::nowUtc(), b, sizeof(b), "%Y-%m-%d %H:%M:%S"); d["time"] = Timekeeper::valid() ? b : "unsynced";
    d["ip"] = s_ip; d["rssi"] = WiFi.RSSI(); d["mqtt"] = s_mqttOk; d["leader"] = leaderNow(); d["portal"] = s_portal;
    d["uptime"] = Node::uptimeS(); d["heap"] = ESP.getFreeHeap();
    if (!forHa) {
      JsonArray fx = d["effects"].to<JsonArray>();
      for (uint8_t i = 0; i < EFFECT_COUNT; i++) fx.add(EFFECT_TABLE[i].name);
      JsonArray snd = d["sounds"].to<JsonArray>();
      for (uint8_t i = 0; i < Audio::soundCount(); i++) snd.add(Audio::soundName(i));
    }
    serializeJson(d, out);
  }

  // ---- web + captive portal ----
  void sendSetupPage() {
    char buf[2400];
    snprintf(buf, sizeof(buf), SETUP_PAGE, Node::name(), s_cfg.ssid, s_cfg.mqttHost, s_cfg.mqttUser,
             s_connected ? s_ip : (s_portal ? "setup AP (not connected)" : "connecting"));
    s_web->send(200, "text/html", buf);
  }
  void webSetup() {
    if (s_webUp) return;
    s_web = new WebServer(WEB_PORT);
    s_web->on("/", HTTP_GET, []() { if (s_portal && !s_connected) sendSetupPage(); else s_web->send_P(200, "text/html", WEB_PAGE); });
    s_web->on("/api/state", HTTP_GET, []() { String j; buildStateJson(j); s_web->send(200, "application/json", j); });
    s_web->on("/api/cmd", HTTP_ANY, []() {
      Command c; c.type = typeFromStr(s_web->arg("type").c_str());
      c.value = s_web->arg("value").toInt();
      strncpy(c.str, s_web->arg("str").c_str(), sizeof(c.str) - 1);
      if (c.type == Command::NONE) { s_web->send(400, "text/plain", "bad type"); return; }
      pushCmd(c); s_web->send(200, "text/plain", "ok");
    });
    s_web->on("/setup", HTTP_GET, sendSetupPage);
    s_web->on("/setup", HTTP_POST, []() {
      saveCfg(s_web->arg("ssid"), s_web->arg("pass"), s_web->arg("mh"), s_web->arg("mu"), s_web->arg("mp"));
      s_web->send(200, "text/html", "<meta charset=utf-8><body style='font-family:sans-serif;background:#120a06;color:#f3e6d0;padding:20px'>Saved. Rebooting…</body>");
      LOGI("net", "credentials saved, rebooting"); delay(400); ESP.restart();
    });
    // Captive-portal probes: answer with a redirect so phones pop the sign-in sheet
    for (const char* u : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/ncsi.txt", "/connecttest.txt", "/fwlink", "/success.txt"})
      s_web->on(u, [](){ s_web->sendHeader("Location", "http://192.168.4.1/setup", true); s_web->send(302, "text/plain", ""); });
    s_web->onNotFound([]() {
      if (s_portal && !s_connected) { s_web->sendHeader("Location", "http://192.168.4.1/setup", true); s_web->send(302, "text/plain", ""); }
      else s_web->send(404, "text/plain", "nope");
    });
    s_web->begin();
    s_webUp = true;
  }

  void portalStart() {
    if (s_portal) return;
    char ap[32]; snprintf(ap, sizeof(ap), "%s-setup", Node::hostname());
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap, PORTAL_AP_PASS);
    if (!s_dns) s_dns = new DNSServer();
    s_dns->start(53, "*", WiFi.softAPIP());
    webSetup();
    s_portal = true;
    LOGW("net", "setup AP '%s' pass '%s' -> http://192.168.4.1/", ap, PORTAL_AP_PASS);
  }
  void portalStop() {
    if (!s_portal) return;
    if (s_dns) s_dns->stop();
    WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
    s_portal = false; LOGI("net", "setup AP closed");
  }

  // ---- OTA ----
  void otaSetup() {
    if (s_otaUp || !OTA_ENABLED) return;
    ArduinoOTA.setHostname(Node::hostname());
    if (strlen(OTA_PASSWORD)) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() { esp_task_wdt_delete(NULL); Audio::stop(); LOGW("ota", "update starting"); });
    ArduinoOTA.onEnd([]() { LOGW("ota", "update done, rebooting"); });
    ArduinoOTA.onError([](ota_error_t e) { LOGE("ota", "error %u", e); esp_task_wdt_add(NULL); });
    ArduinoOTA.begin();
    s_otaUp = true;
    LOGI("ota", "ready: pio run -t upload --upload-port %s.local", Node::hostname());
  }

  // ---- MQTT ----
  void mqttCallback(char* topic, uint8_t* payload, unsigned int len) {
    JsonDocument d;
    if (deserializeJson(d, payload, len)) { LOGW("mqtt", "bad json on %s", topic); return; }
    Command c;
    auto push = [&](Command::Type t, int32_t v = 0, const char* s = nullptr) { c = Command(); c.type = t; c.value = v; if (s) strncpy(c.str, s, sizeof(c.str) - 1); pushCmd(c); };
    if (d["state"].is<const char*>()) { const char* st = d["state"]; push(Command::MODE, !strcasecmp(st, "ON") ? 1 : !strcasecmp(st, "OFF") ? 2 : 0); }  // HA
    if (d["effect"].is<const char*>())     push(Command::EFFECT, 0, d["effect"]);
    if (d["brightness"].is<int>())         push(Command::BRIGHTNESS, d["brightness"]);
    if (d["speed"].is<int>())              push(Command::SPEED, d["speed"]);
    if (d["hue"].is<int>())                push(Command::HUE, d["hue"]);
    if (d["rotate"].is<bool>())            push(Command::ROTATE, d["rotate"] ? 1 : 0);
    if (d["ambient"].is<bool>())           push(Command::AMBIENT, d["ambient"] ? 1 : 0);
    if (d["volume"].is<int>())             push(Command::VOLUME, d["volume"]);
    if (d["play"].is<const char*>())       push(Command::PLAY, 0, d["play"]);
    if (d["text"].is<const char*>())       push(Command::TEXT, 0, d["text"]);
    if (d["next"].is<bool>() && d["next"]) push(Command::NEXT);
    if (d["scare"].is<bool>() && d["scare"]) push(Command::SCARE, 0x80);
    if (d["scare"].is<int>())              push(Command::SCARE, 0x80 | (d["scare"].as<int>() & 0x7F));
    if (d["mode"].is<const char*>()) { const char* m = d["mode"]; push(Command::MODE, !strcmp(m, "on") ? 1 : !strcmp(m, "off") ? 2 : 0); }
    if (d["reboot"].is<bool>() && d["reboot"]) push(Command::REBOOT);
    if (d["portal"].is<bool>() && d["portal"]) push(Command::PORTAL);
  }

  void mqttPublishState() {
    if (!s_mqtt || !s_mqtt->connected()) return;
    String j; buildStateJson(j, true);
    s_mqtt->publish(s_topicState, j.c_str(), false);
  }

  void haDiscovery() {
#if HA_DISCOVERY
    JsonDocument d;
    char uid[24]; snprintf(uid, sizeof(uid), "pumpkin_%u", Node::id());
    d["name"] = Node::name(); d["uniq_id"] = uid; d["schema"] = "json";
    d["stat_t"] = s_topicState; d["cmd_t"] = s_topicCmd; d["avty_t"] = s_topicStatus;
    d["brightness"] = true; d["effect"] = true; d["bri_scl"] = 255;
    JsonArray fx = d["fx_list"].to<JsonArray>();
    for (uint8_t i = 0; i < EFFECT_COUNT; i++) fx.add(EFFECT_TABLE[i].name);
    JsonObject dev = d["dev"].to<JsonObject>();
    dev["ids"].to<JsonArray>().add(uid); dev["name"] = Node::name(); dev["mf"] = "DIY"; dev["mdl"] = FW_NAME; dev["sw"] = FW_VERSION;
    String j; serializeJson(d, j);
    char topic[64]; snprintf(topic, sizeof(topic), "%s/light/%s/config", HA_PREFIX, uid);
    s_mqtt->publish(topic, j.c_str(), true);
    LOGI("mqtt", "HA discovery published to %s", topic);
#endif
  }

  void mqttLoop(uint32_t now) {
#if MQTT_ENABLED
    if (!s_mqtt || strlen(s_cfg.mqttHost) == 0) return;
    if (!s_mqtt->connected()) {
      s_mqttOk = false;
      if ((int32_t)(now - s_nextMqtt) < 0) return;
      s_nextMqtt = now + MQTT_RETRY_MS;
      bool ok = s_mqtt->connect(Node::hostname(), strlen(s_cfg.mqttUser) ? s_cfg.mqttUser : nullptr,
                                strlen(s_cfg.mqttPass) ? s_cfg.mqttPass : nullptr, s_topicStatus, 1, true, "offline");
      if (!ok) { LOGW("mqtt", "connect failed rc=%d", s_mqtt->state()); return; }
      s_mqtt->publish(s_topicStatus, "online", true);
      s_mqtt->subscribe(s_topicCmd); s_mqtt->subscribe(s_topicAll);
      s_mqttOk = true; LOGI("mqtt", "connected to %s, cmd topics %s, %s", s_cfg.mqttHost, s_topicCmd, s_topicAll);
      haDiscovery();
      mqttPublishState();
    }
    s_mqtt->loop();
    if ((int32_t)(now - s_nextMqttState) >= 0) { s_nextMqttState = now + MQTT_STATE_PERIOD_MS; mqttPublishState(); }
#endif
  }

  // ---- weather (Open-Meteo, HTTPS, no key) ----
  void weatherFetch() {
#if WEATHER_ENABLED
    WiFiClientSecure cl; cl.setInsecure();          // we only read a public forecast; no need to pin the CA
    cl.setTimeout(8);
    HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(8000);
    char url[220];
    snprintf(url, sizeof(url), "https://%s/v1/forecast?latitude=%.4f&longitude=%.4f&current=precipitation,wind_speed_10m,weather_code",
             WEATHER_HOST, GEO_LAT, GEO_LON);
    if (!http.begin(cl, url)) { LOGW("wx", "begin failed"); return; }
    int code = http.GET();
    if (code != 200) { LOGW("wx", "HTTP %d", code); http.end(); return; }
    JsonDocument d;
    if (deserializeJson(d, http.getStream())) { LOGW("wx", "bad json"); http.end(); return; }
    http.end();
    s_wx.windKmh = d["current"]["wind_speed_10m"] | 0.0f;
    s_wx.rainMm = d["current"]["precipitation"] | 0.0f;
    s_wx.valid = true; s_wx.at = millis();
    LOGI("wx", "wind %.0f km/h, precip %.1f mm, code %d", s_wx.windKmh, s_wx.rainMm, (int)(d["current"]["weather_code"] | 0));
#endif
  }

  // ---- UDP sync ----
  void sendPacket(SyncPacket& p) {
    p.magic = SYNC_MAGIC; p.ver = 2; p.nodeId = Node::id();
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
      if (p.magic != SYNC_MAGIC || p.ver != 2 || p.nodeId == Node::id()) continue;
      notePeer(p.nodeId, now);
      if (p.type == PKT_BEACON) {
        if (p.nodeId < Node::id() && p.effect != 0xFF) {       // only follow a lower-id leader
          Command c; c.type = Command::EFFECT; c.origin = p.nodeId; c.value = (int32_t)p.elapsedMs; c.aux = p.seed;
          strncpy(c.str, p.effect < EFFECT_COUNT ? EFFECT_TABLE[p.effect].name : "", sizeof(c.str) - 1);
          pushCmd(c);
          Command m; m.type = Command::MODE; m.origin = p.nodeId; m.value = p.mode; pushCmd(m);
        }
      } else if (p.type == PKT_CMD) {
        Command c; c.type = (Command::Type)p.cmdType; c.value = p.cmdValue; c.origin = p.nodeId; c.aux = p.cmdAux;
        strncpy(c.str, p.cmdStr, sizeof(c.str) - 1); c.str[sizeof(c.str) - 1] = 0;
        if (c.type == Command::SCARE && !SYNC_FOLLOW_SCARE) continue;
        pushCmd(c);
      }
    }
    expirePeers(now);
    if (leaderNow() && (int32_t)(now - s_nextBeacon) >= 0) {
      s_nextBeacon = now + SYNC_HEARTBEAT_MS;
      NodeState s = snapshot();
      SyncPacket p{}; p.type = PKT_BEACON; p.effect = s.scare ? 0xFF : s.effectIdx; p.brightness = s.brightness; p.mode = s.mode;
      p.speed = s.speed; p.hue = s.hue; p.rotate = s.rotate; p.seed = s.seed; p.elapsedMs = s.effectElapsedMs;
      sendPacket(p);                                          // 0xFF effect = "in a scare, keep yours"
    }
#endif
  }

  void servicesUp() {
    snprintf(s_ip, sizeof(s_ip), "%s", WiFi.localIP().toString().c_str());
    LOGI("net", "WiFi up: %s rssi=%d", s_ip, WiFi.RSSI());
    portalStop();
    if (!s_servicesUp) {
      if (MDNS.begin(Node::hostname())) { MDNS.addService("http", "tcp", WEB_PORT); LOGI("net", "http://%s.local", Node::hostname()); }
      webSetup();
      s_udp.begin(SYNC_UDP_PORT);
      otaSetup();
      s_servicesUp = true;
    }
    s_nextMqtt = 0; s_nextWx = 0;
  }

  void wifiLoop(uint32_t now) {
    bool haveCreds = strlen(s_cfg.ssid) > 0;
    bool up = WiFi.status() == WL_CONNECTED;
    if (up && !s_connected) { s_connected = true; servicesUp(); }
    if (!up && s_connected) { s_connected = false; s_mqttOk = false; LOGW("net", "WiFi lost; standalone until it returns"); s_nextWifiRetry = now + WIFI_RETRY_MS; s_staSince = now; }
    if (!up && haveCreds && (int32_t)(now - s_nextWifiRetry) >= 0) {
      s_nextWifiRetry = now + WIFI_RETRY_MS;
      LOGI("net", "WiFi connecting to %s", s_cfg.ssid);
      WiFi.disconnect(false, false);
      WiFi.begin(s_cfg.ssid, s_cfg.pass);
    }
    // Portal: no credentials, requested at boot, or STA has failed for 2 minutes
    if (!up && !s_portal && (!haveCreds || s_portalReq || now - s_staSince > 120000)) portalStart();
  }

  void netTask(void*) {
    Node::wdtAddCurrentTask();
    loadCfg();
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(Node::hostname());
    WiFi.setSleep(false);               // no modem sleep: steadier UDP + keeps power bank loaded (~+60 mA)
    WiFi.setAutoReconnect(false);       // we own the retry policy
    s_nextWifiRetry = 0; s_staSince = millis();
#if MQTT_ENABLED
    s_mqtt = new PubSubClient(s_wifiClient);
    s_mqtt->setServer(s_cfg.mqttHost, MQTT_PORT); s_mqtt->setCallback(mqttCallback);
    s_mqtt->setBufferSize(1536); s_mqtt->setSocketTimeout(3); s_mqtt->setKeepAlive(30);
    s_wifiClient.setTimeout(3);
#endif
    for (;;) {
      Node::wdtFeed();
      uint32_t now = millis();
      wifiLoop(now);
      if (s_portal && s_dns) s_dns->processNextRequest();
      if (s_webUp) s_web->handleClient();
      if (s_connected) {
        if (s_otaUp) ArduinoOTA.handle();
        mqttLoop(now); syncLoop(now);
        SyncPacket tx; while (xQueueReceive(s_txQ, &tx, 0) == pdTRUE) sendPacket(tx);
        if (WEATHER_ENABLED && (int32_t)(now - s_nextWx) >= 0) { s_nextWx = now + WEATHER_PERIOD_MS; weatherFetch(); }
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
  xTaskCreatePinnedToCore(netTask, "net", 10240, nullptr, 1, nullptr, 0);
  LOGI("net", "task started on core 0");
#else
  LOGI("net", "disabled");
#endif
}

void Net::requestPortal() { s_portalReq = true; }
bool Net::connected() { return s_connected; }
bool Net::portalActive() { return s_portal; }
bool Net::popCommand(Command& out) { return s_cmdQ && xQueueReceive(s_cmdQ, &out, 0) == pdTRUE; }
void Net::setState(const NodeState& s) {
  if (s_mx && xSemaphoreTake(s_mx, pdMS_TO_TICKS(5)) == pdTRUE) { s_state = s; xSemaphoreGive(s_mx); }
}
void Net::broadcast(const Command& c) {
#if NET_ENABLED && SYNC_ENABLED
  if (!s_connected) return;
  SyncPacket p{}; p.type = PKT_CMD; p.cmdType = c.type; p.cmdValue = c.value; p.cmdAux = c.aux;
  strncpy(p.cmdStr, c.str, sizeof(p.cmdStr) - 1);
  xQueueSend(s_txQ, &p, 0);   // sent by the net task; WiFiUDP is not shared across tasks
#endif
}
bool Net::isLeader() { return leaderNow(); }
uint8_t Net::nodesSeen() { return s_nPeers + 1; }
uint8_t Net::slot() { return slotNow(); }
const char* Net::ip() { return s_ip; }
int Net::rssi() { return s_connected ? WiFi.RSSI() : 0; }
bool Net::mqttConnected() { return s_mqttOk; }
bool Net::weather(float& wind01, float& rain01, float& windKmh, float& rainMm) {
  windKmh = s_wx.windKmh; rainMm = s_wx.rainMm;
  wind01 = constrain(s_wx.windKmh / WEATHER_WIND_FULL_KMH, 0.0f, 1.0f);
  rain01 = constrain(s_wx.rainMm / WEATHER_RAIN_FULL_MM, 0.0f, 1.0f);
  return s_wx.valid;
}
