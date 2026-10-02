// =============================================================================
// Pumpkin Lantern node — main integration
//   core 1 (this loop): LEDs, sensors, scheduler, command dispatch   (watchdog-fed)
//   core 0: net task (WiFi/portal/OTA/web/MQTT/sync/weather), audio task (SD->I2S)
// Nothing in loop() blocks longer than the 10 us ultrasonic trigger pulse.
// =============================================================================
#include <Arduino.h>
#include "config.h"
#include "sys/log.h"
#include "sys/node.h"
#include "sys/button.h"
#include "sys/aux_sensors.h"
#include "effects/engine.h"
#include "dusk/timekeeper.h"
#include "dusk/light_sensor.h"
#include "dusk/dusk_dawn.h"
#include "proximity/ultrasonic.h"
#include "audio/audio_player.h"
#include "net/net_manager.h"

static EffectEngine engine;
static LightSensor light;
static DuskDawn dusk;
static Ultrasonic ranger;
static Button button;

// ---- playlists & scenes --------------------------------------------------------
static const char* const PL_FRIENDLY[] = PLAYLIST_FRIENDLY;
static const char* const PL_HAUNTED[] = PLAYLIST_HAUNTED;
static const char* const PL_DEFAULT[] = EFFECT_PLAYLIST;
struct Playlist { const char* const* names; uint8_t n; };
static const Playlist PLAYLISTS[] = {
  { PL_FRIENDLY, sizeof(PL_FRIENDLY) / sizeof(PL_FRIENDLY[0]) },
  { PL_HAUNTED,  sizeof(PL_HAUNTED) / sizeof(PL_HAUNTED[0]) },
  { PL_DEFAULT,  sizeof(PL_DEFAULT) / sizeof(PL_DEFAULT[0]) },
};
static const SceneDef SCENES[] = SCENE_TABLE;
static const uint8_t SCENE_N = sizeof(SCENES) / sizeof(SCENES[0]);
static const ScareLevel LEVELS[] = SCARE_LEVELS;
static const uint8_t LEVEL_N = sizeof(LEVELS) / sizeof(LEVELS[0]);

static const Playlist* playlist = &PLAYLISTS[2];
static int8_t sceneIdx = -1;
static uint8_t sceneBrightness = LED_BRIGHTNESS_DEFAULT;
static bool scaresEnabled = true;
static uint8_t playIdx = 0;
static bool rotate = EFFECT_ROTATE;
static uint32_t nextRotate = 0, nextStateTx = 0, nextKeepAlive = 0, keepAliveOff = 0, nextSceneEval = 0;

// ---- scare escalation / alert / idle ---------------------------------------------
static uint8_t scareLevel = 0;
static uint32_t lastTriggerAt = 0, alertUntil = 0;
static int8_t alertDir = 0;
static bool idle = false;
static uint32_t idleWakeUntil = 0;
static char textBuf[24] = TEXT_MESSAGE;
static bool userBrightness = false;      // slider touched since last scene change
static uint8_t userBrightnessVal = LED_BRIGHTNESS_DEFAULT;
static uint32_t nextBrightnessEval = 0;
static bool ambientUser = true, ambientApplied = false;   // user wants ambient; what the audio task currently has

// ---- helpers ------------------------------------------------------------------
static void applyBrightness() {
  float f = AuxSensors::vbusBrightnessFactor();
  uint8_t b = idle ? IDLE_SLEEP_BRIGHTNESS : (userBrightness ? userBrightnessVal : sceneBrightness);
  engine.setBrightness((uint8_t)max(LED_BRIGHTNESS_MIN, (int)(b * f)));
}

static void setPlaylistEffect(uint32_t now, bool broadcast) {
  engine.setEffect(playlist->names[playIdx]);
  nextRotate = now + EFFECT_ROTATE_S * 1000UL;
  if (broadcast) { Command c; c.type = Command::EFFECT; strncpy(c.str, playlist->names[playIdx], sizeof(c.str) - 1); Net::broadcast(c); }
}

static void nextEffect(uint32_t now, bool broadcast) {
  // Weather bias: at full rain, WEATHER_LIGHTNING_BIAS of rotation steps jump to lightning
  float r = engine.rain();
  if (r > 0 && (esp_random() % 1000) < (uint32_t)(r * WEATHER_LIGHTNING_BIAS * 1000)) {
    engine.setEffect("lightning"); nextRotate = now + EFFECT_ROTATE_S * 1000UL;
    if (broadcast) { Command c; c.type = Command::EFFECT; strncpy(c.str, "lightning", sizeof(c.str) - 1); Net::broadcast(c); }
    return;
  }
  playIdx = (playIdx + 1) % playlist->n;
  setPlaylistEffect(now, broadcast);
}

static void wakeFromIdle(uint32_t now) {
  if (!idle) return;
  idle = false; idleWakeUntil = now + IDLE_WAKE_MS;
  applyBrightness(); setPlaylistEffect(now, false);
  LOGI("sched", "idle -> awake");
}

// level: 0..LEVEL_N-1; withSound only for the node that was actually triggered
static void runScare(uint8_t level, bool withSound, uint32_t now) {
  if (level >= LEVEL_N) level = LEVEL_N - 1;
  const ScareLevel& L = LEVELS[level];
  engine.triggerScare(L.effect, L.durationMs);
  if (withSound && L.sound && Audio::available()) Audio::playRandom(SCARE_SOUND_PREFIX);
  scareLevel = level;
}

static void localTrigger(uint32_t now, bool broadcast) {
  wakeFromIdle(now);
  if (!scaresEnabled) { LOGD("sched", "trigger ignored: scene has scares off"); lastTriggerAt = now; return; }
  uint8_t level = (lastTriggerAt && now - lastTriggerAt < SCARE_ESCALATION_WINDOW_MS) ? min<uint8_t>(scareLevel + 1, LEVEL_N - 1) : 0;
  lastTriggerAt = now;
  LOGI("sched", "scare level %u/%u", level + 1, LEVEL_N);
  runScare(level, true, now);
  if (broadcast) { Command c; c.type = Command::SCARE; c.value = level; c.aux = Net::slot(); Net::broadcast(c); }   // lights-only for peers
}

static void remoteScare(const Command& c, uint32_t now) {
  uint8_t level = c.value & 0x7F;
  bool sound = c.value & 0x80;                   // web/MQTT "scare" button asks for sound; fleet fan-out does not
  if (c.origin == 0) { localTrigger(now, true); return; }
  wakeFromIdle(now);
  alertUntil = now + SCARE_ALERT_MS;
  alertDir = c.aux < Net::slot() ? -1 : (c.aux > Net::slot() ? 1 : 0);
  if (scaresEnabled) runScare(level, sound, now);
}

static void evalScene(uint32_t now) {
  if ((int32_t)(now - nextSceneEval) < 0) return;
  nextSceneEval = now + 60000;
  int8_t best = -1; int bestMin = -1;
  struct tm t;
  if (Timekeeper::localTm(t)) {
    int nowMin = t.tm_hour * 60 + t.tm_min;
    for (uint8_t i = 0; i < SCENE_N; i++) {
      if (SCENES[i].hh == 255) continue;
      int sm = SCENES[i].hh * 60 + SCENES[i].mm;
      int d = nowMin - sm; if (d < 0) d += 1440;         // minutes since this scene started (wraps)
      if (bestMin < 0 || d < bestMin) { bestMin = d; best = i; }
    }
  }
  if (best < 0) for (uint8_t i = 0; i < SCENE_N; i++) if (SCENES[i].hh == 255) best = i;
  if (best < 0 || best == sceneIdx) return;
  sceneIdx = best;
  const SceneDef& S = SCENES[best];
  sceneBrightness = S.brightness; scaresEnabled = S.scares; userBrightness = false;
  playlist = &PLAYLISTS[S.playlist < 3 ? S.playlist : 2];
  playIdx = 0;
  applyBrightness();
  if (!engine.scareActive() && !idle) setPlaylistEffect(now, Net::isLeader());
  LOGI("sched", "scene -> %s (brightness %u, scares %s, playlist %u)", S.name, S.brightness, S.scares ? "on" : "off", S.playlist);
}

static void evalIdle(uint32_t now) {
#if IDLE_SLEEP_ENABLED
  struct tm t;
  if (!Timekeeper::localTm(t)) return;
  bool window = IDLE_SLEEP_AFTER_HOUR <= IDLE_SLEEP_BEFORE_HOUR
                  ? (t.tm_hour >= IDLE_SLEEP_AFTER_HOUR && t.tm_hour < IDLE_SLEEP_BEFORE_HOUR)
                  : (t.tm_hour >= IDLE_SLEEP_AFTER_HOUR || t.tm_hour < IDLE_SLEEP_BEFORE_HOUR);
  bool quiet = now - lastTriggerAt > IDLE_SLEEP_NO_TRIGGER_MS && (int32_t)(now - idleWakeUntil) > 0;
  if (window && quiet && !idle && !engine.scareActive()) {
    idle = true; applyBrightness(); engine.setEffect(IDLE_SLEEP_EFFECT);
    LOGI("sched", "idle: %s at %u (no trigger for %lu min)", IDLE_SLEEP_EFFECT, IDLE_SLEEP_BRIGHTNESS, (unsigned long)(IDLE_SLEEP_NO_TRIGGER_MS / 60000));
  } else if (!window && idle) wakeFromIdle(now);
#endif
}

static void apply(const Command& c, uint32_t now) {
  bool remote = c.origin != 0;
  if (!remote && c.type != Command::SCARE) wakeFromIdle(now);
  switch (c.type) {
    case Command::EFFECT:
      if (remote && c.value > 0) {                       // leader beacon: follow effect + phase + seed
        if (idle) break;
        if (strcmp(engine.effectName(), c.str) != 0) engine.setEffect(c.str);
        if (!engine.scareActive()) {
          int32_t drift = (int32_t)engine.effectElapsed(now) - c.value;
          if (drift > 400 || drift < -400) engine.alignStart(c.value, now, c.aux);
        }
      } else {
        engine.setEffect(c.str);
        nextRotate = now + EFFECT_ROTATE_S * 1000UL;
        for (uint8_t i = 0; i < playlist->n; i++) if (!strcasecmp(playlist->names[i], c.str)) playIdx = i;
      }
      break;
    case Command::NEXT:       nextEffect(now, !remote); return;
    case Command::BRIGHTNESS: userBrightness = true; userBrightnessVal = (uint8_t)constrain(c.value, LED_BRIGHTNESS_MIN, 255); applyBrightness(); break;
    case Command::MODE:       dusk.setMode(c.value == 1 ? DuskDawn::Mode::FORCE_ON : c.value == 2 ? DuskDawn::Mode::FORCE_OFF : DuskDawn::Mode::AUTO); break;
    case Command::SCARE:      remoteScare(c, now); return;
    case Command::SPEED:      engine.setSpeed((uint8_t)constrain(c.value, 1, 255)); break;
    case Command::HUE:        engine.setHue((uint8_t)constrain(c.value, 0, 255)); break;
    case Command::ROTATE:     rotate = c.value != 0; nextRotate = now + EFFECT_ROTATE_S * 1000UL; break;
    case Command::TEXT:       strncpy(textBuf, c.str, sizeof(textBuf) - 1); engine.setText(textBuf); LOGI("sched", "text='%s'", textBuf); break;
    case Command::AMBIENT:    ambientUser = c.value != 0; break;
    case Command::PLAY:       Audio::play(c.str); return;                     // local only (per-node sounds)
    case Command::VOLUME:     Audio::setVolume(c.value / 100.0f); break;
    case Command::PORTAL:     Net::requestPortal(); LOGW("main", "portal requested; rebooting"); delay(100); ESP.restart(); return;
    case Command::REBOOT:     LOGW("main", "reboot requested"); delay(100); ESP.restart(); return;
    default: return;
  }
  if (!remote) Net::broadcast(c);
}

static void publishState(uint32_t now) {
  NodeState s;
  strncpy(s.effect, engine.effectName(), sizeof(s.effect) - 1);
  s.effectIdx = engine.effectIndex() < 0 ? 0 : engine.effectIndex();
  s.effectElapsedMs = engine.effectElapsed(now); s.seed = engine.seed();
  s.brightness = engine.brightness(); s.speed = engine.speed(); s.hue = engine.hue();
  s.mode = (uint8_t)dusk.mode(); s.lightsOn = dusk.lightsOn(); s.rotate = rotate; s.scare = engine.scareActive();
  strncpy(s.source, dusk.sourceName(), sizeof(s.source) - 1);
  s.sunElev = dusk.sunElevation(); s.lux = light.lux(); s.distanceCm = ranger.distanceCm(); s.reactivity = ranger.reactivity();
  s.usHealthy = ranger.healthy(); s.lightHealthy = light.healthy(); s.audioOk = Audio::available(); s.playing = Audio::isPlaying();
  s.ledMilliamps = engine.estimatedMilliamps();
  s.nextSunset = dusk.nextSunset(); s.nextSunrise = dusk.nextSunrise();
  s.slot = Net::slot(); s.fleet = Net::nodesSeen(); s.scareLevel = scareLevel;
  s.idle = idle; s.alert = (int32_t)(now - alertUntil) < 0; s.scaresEnabled = scaresEnabled;
  strncpy(s.scene, sceneIdx >= 0 ? SCENES[sceneIdx].name : "default", sizeof(s.scene) - 1);
  s.vbusMv = AuxSensors::vbusMv(); s.mic = AuxSensors::mic();
  float w, r, wk, rm; if (Net::weather(w, r, wk, rm)) { s.wind = w; s.rain = r; }
  strncpy(s.text, textBuf, sizeof(s.text) - 1);
  Net::setState(s);
}

// ---- arduino ------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);
  pinMode(STATUS_LED_PIN, OUTPUT);
  Node::begin();
  engine.begin();
  engine.setText(textBuf);
  engine.setEffect(EFFECT_DEFAULT, false);
  for (uint8_t i = 0; i < playlist->n; i++) if (!strcasecmp(playlist->names[i], EFFECT_DEFAULT)) playIdx = i;
  light.begin();
  dusk.begin(&light);
  ranger.begin();
  AuxSensors::begin();
  button.begin(BUTTON_PIN, BUTTON_LONG_MS);
#if PORTAL_ENABLED
  // Press BOOT within 1.5 s of power-up and hold it ~1 s -> provisioning AP this boot.
  // (Holding it *before* power-up puts the chip in the serial bootloader instead.)
  { uint32_t t0 = millis(), lowSince = 0;
    while (millis() - t0 < 1500 + PORTAL_HOLD_MS) {
      bool low = !digitalRead(BUTTON_PIN);
      if (low && !lowSince) lowSince = millis();
      if (!low) { if (lowSince) break; lowSince = 0; if (millis() - t0 > 1500) break; }
      if (low && millis() - lowSince >= PORTAL_HOLD_MS) { Net::requestPortal(); LOGW("main", "BOOT held: setup portal requested"); break; }
      delay(10);
    } }
#endif
  Audio::begin();
  Timekeeper::begin();
  Net::begin();
  nextRotate = millis() + EFFECT_ROTATE_S * 1000UL;
  LOGI("main", "setup done; lights=%s default=%s rotate=%s scenes=%u levels=%u", dusk.lightsOn() ? "on" : "off", EFFECT_DEFAULT, rotate ? "yes" : "no", SCENE_N, LEVEL_N);
}

void loop() {
  uint32_t now = millis();
  Node::wdtFeed();
  Node::loopTick();

  // --- inputs ---
  Button::Event ev = button.update();
  if (ev == Button::SHORT) { wakeFromIdle(now); nextEffect(now, true); }
  if (ev == Button::LONG)  { Command c; c.type = Command::MODE; c.value = dusk.mode() == DuskDawn::Mode::AUTO ? 1 : 0; apply(c, now); }

  light.update(now);
  dusk.update(now);
  ranger.update(now);
  AuxSensors::update(now);
#if PIR_ENABLED
  ranger.externalPresence(AuxSensors::pir(), now);
#endif

  Command c;
  while (Net::popCommand(c)) apply(c, now);

  // --- scheduler ---
  bool on = dusk.lightsOn();
  evalScene(now);
  if (on) evalIdle(now);
  engine.setEnabled(on);
  float react = ranger.reactivity();
  if (AuxSensors::pir()) react = max(react, (float)PIR_REACTIVITY);
  react = max(react, AuxSensors::mic());
  bool alert = (int32_t)(now - alertUntil) < 0;
  if (alert) react = max(react, (float)SCARE_ALERT_FLOOR);
  engine.setAlertDir(alert ? alertDir : 0);
  engine.setReactivity(on ? react : 0);
  engine.setFleet(Net::slot(), Net::nodesSeen());
  { float w, r, wk, rm; if (Net::weather(w, r, wk, rm)) engine.setWeather(w, r); }
  if (on && ranger.takeTrigger()) localTrigger(now, SYNC_FOLLOW_SCARE);
  if (on && rotate && !idle && !engine.scareActive() && Net::isLeader() && (int32_t)(now - nextRotate) >= 0) nextEffect(now, true);
  if ((int32_t)(now - nextBrightnessEval) >= 0) { nextBrightnessEval = now + 5000; applyBrightness(); }   // VBUS-driven cap
  { bool want = on && !idle && ambientUser && strlen(AMBIENT_SOUND_PATH) && Audio::hasSound(AMBIENT_SOUND_PATH);
    if (want != ambientApplied) { ambientApplied = want; Audio::setAmbient(want); } }

  // --- power-bank keep-alive while dark ---
#if KEEPALIVE_ENABLED
  if (!on) {
    if ((int32_t)(now - nextKeepAlive) >= 0) { nextKeepAlive = now + KEEPALIVE_PERIOD_MS; keepAliveOff = now + KEEPALIVE_PULSE_MS;
      if (KEEPALIVE_LOAD_PIN >= 0) { pinMode(KEEPALIVE_LOAD_PIN, OUTPUT); digitalWrite(KEEPALIVE_LOAD_PIN, HIGH); } }
    bool pulse = (int32_t)(now - keepAliveOff) < 0;
    engine.setKeepAlivePulse(pulse);
    if (!pulse && KEEPALIVE_LOAD_PIN >= 0) digitalWrite(KEEPALIVE_LOAD_PIN, LOW);
  } else engine.setKeepAlivePulse(false);
#endif

  // --- render ---
  engine.update(now);

  // --- status ---
  digitalWrite(STATUS_LED_PIN, Net::portalActive() ? ((now / 100) & 1) : Net::connected() ? ((now / 1000) & 1) : ((now / 200) & 1));
  if ((int32_t)(now - nextStateTx) >= 0) { nextStateTx = now + 500; publishState(now); }
}
