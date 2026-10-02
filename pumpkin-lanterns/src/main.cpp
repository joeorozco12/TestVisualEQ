// =============================================================================
// Pumpkin Lantern node — main integration
//   core 1 (this loop): LEDs, sensors, scheduler, command dispatch   (watchdog-fed)
//   core 0: net task (WiFi/web/MQTT/sync), audio task (SD->I2S)
// Nothing in loop() blocks longer than the 10 us ultrasonic trigger pulse.
// =============================================================================
#include <Arduino.h>
#include "config.h"
#include "sys/log.h"
#include "sys/node.h"
#include "sys/button.h"
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

static const char* const PLAYLIST[] = EFFECT_PLAYLIST;
static const uint8_t PLAYLIST_N = sizeof(PLAYLIST) / sizeof(PLAYLIST[0]);
static uint8_t playIdx = 0;
static bool rotate = EFFECT_ROTATE;
static uint32_t nextRotate = 0, nextStateTx = 0, nextKeepAlive = 0, keepAliveOff = 0;

// ---- helpers ------------------------------------------------------------------
static void startScare(bool withSound, bool broadcast) {
  engine.triggerScare(SCARE_EFFECT, SCARE_DURATION_MS);
  if (withSound && Audio::available()) Audio::play(SCARE_SOUND);
  if (broadcast) { Command c; c.type = Command::SCARE; c.value = 0; Net::broadcast(c); }  // neighbours: lights only
}

static void nextEffect(bool broadcast) {
  playIdx = (playIdx + 1) % PLAYLIST_N;
  engine.setEffect(PLAYLIST[playIdx]);
  nextRotate = millis() + EFFECT_ROTATE_S * 1000UL;
  if (broadcast) { Command c; c.type = Command::EFFECT; strncpy(c.str, PLAYLIST[playIdx], sizeof(c.str) - 1); Net::broadcast(c); }
}

static void apply(const Command& c, uint32_t now) {
  bool remote = c.origin != 0;
  switch (c.type) {
    case Command::EFFECT:
      if (remote && c.value > 0) {                       // leader beacon: follow effect + phase + seed
        if (strcmp(engine.effectName(), c.str) != 0) engine.setEffect(c.str);
        if (!engine.scareActive()) {
          int32_t drift = (int32_t)engine.effectElapsed(now) - c.value;
          if (drift > 400 || drift < -400) engine.alignStart(c.value, now, c.aux);
        }
      } else {
        engine.setEffect(c.str);
        nextRotate = now + EFFECT_ROTATE_S * 1000UL;
        for (uint8_t i = 0; i < PLAYLIST_N; i++) if (!strcasecmp(PLAYLIST[i], c.str)) playIdx = i;
      }
      break;
    case Command::NEXT:       nextEffect(!remote); return;
    case Command::BRIGHTNESS: engine.setBrightness((uint8_t)constrain(c.value, LED_BRIGHTNESS_MIN, 255)); break;
    case Command::MODE:       dusk.setMode(c.value == 1 ? DuskDawn::Mode::FORCE_ON : c.value == 2 ? DuskDawn::Mode::FORCE_OFF : DuskDawn::Mode::AUTO); break;
    case Command::SCARE:      startScare(c.value != 0 || !remote, !remote); return;
    case Command::SPEED:      engine.setSpeed((uint8_t)constrain(c.value, 1, 255)); break;
    case Command::HUE:        engine.setHue((uint8_t)constrain(c.value, 0, 255)); break;
    case Command::ROTATE:     rotate = c.value != 0; nextRotate = now + EFFECT_ROTATE_S * 1000UL; break;
    case Command::PLAY:       Audio::play(c.str); return;                     // local only (per-node sounds)
    case Command::VOLUME:     Audio::setVolume(c.value / 100.0f); break;
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
  Net::setState(s);
}

// ---- arduino ------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);
  pinMode(STATUS_LED_PIN, OUTPUT);
  Node::begin();
  engine.begin();
  engine.setEffect(EFFECT_DEFAULT, false);
  for (uint8_t i = 0; i < PLAYLIST_N; i++) if (!strcasecmp(PLAYLIST[i], EFFECT_DEFAULT)) playIdx = i;
  light.begin();
  dusk.begin(&light);
  ranger.begin();
  button.begin(BUTTON_PIN, BUTTON_LONG_MS);
  Audio::begin();
  Timekeeper::begin();
  Net::begin();
  nextRotate = millis() + EFFECT_ROTATE_S * 1000UL;
  LOGI("main", "setup done; lights=%s default=%s rotate=%s", dusk.lightsOn() ? "on" : "off", EFFECT_DEFAULT, rotate ? "yes" : "no");
}

void loop() {
  uint32_t now = millis();
  Node::wdtFeed();
  Node::loopTick();

  // --- inputs ---
  Button::Event ev = button.update();
  if (ev == Button::SHORT) nextEffect(true);
  if (ev == Button::LONG)  { Command c; c.type = Command::MODE; c.value = dusk.mode() == DuskDawn::Mode::AUTO ? 1 : 0; apply(c, now); }

  light.update(now);
  dusk.update(now);
  ranger.update(now);

  Command c;
  while (Net::popCommand(c)) apply(c, now);

  // --- scheduler ---
  bool on = dusk.lightsOn();
  engine.setEnabled(on);
  engine.setReactivity(on ? ranger.reactivity() : 0);
  if (on && ranger.takeTrigger()) startScare(true, SYNC_FOLLOW_SCARE);
  if (on && rotate && !engine.scareActive() && Net::isLeader() && (int32_t)(now - nextRotate) >= 0) nextEffect(true);

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
  digitalWrite(STATUS_LED_PIN, Net::connected() ? ((now / 1000) & 1) : ((now / 200) & 1));   // slow blink = WiFi ok
  if ((int32_t)(now - nextStateTx) >= 0) { nextStateTx = now + 500; publishState(now); }
}
