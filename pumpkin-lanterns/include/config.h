// =============================================================================
// Pumpkin Lanterns — single source of truth for pins, counts, thresholds, timing
// -----------------------------------------------------------------------------
// Everything tunable lives here. No module may hard-code a pin or threshold.
// Secrets (WiFi/MQTT credentials) live in include/secrets.h (gitignored);
// copy include/secrets.example.h and edit.
// =============================================================================
#pragma once
#include <stdint.h>

// ---- Build identity ---------------------------------------------------------
#define FW_NAME     "pumpkin-lantern"
#define FW_VERSION  "0.1.0"

// ---- Node identity ----------------------------------------------------------
// Node id is resolved at boot: MAC lookup in NODE_MAC_TABLE, else last MAC byte.
// Hostname becomes "pumpkin-<id>". 0 is reserved for "unassigned".
struct NodeMacEntry { uint8_t mac[6]; uint8_t id; const char* name; };
#define NODE_MAC_TABLE                                                         \
  {                                                                            \
    /* {{0x24,0x6F,0x28,0xAA,0xBB,0x01}, 1, "porch-left"},  */                 \
    /* {{0x24,0x6F,0x28,0xAA,0xBB,0x02}, 2, "porch-right"}, */                 \
  }
#define NODE_MAX_COUNT          8      // sizing for sync tables

// ---- LED cluster ------------------------------------------------------------
#define LED_PIN                 27     // data out -> 74AHCT125 -> 330R -> DIN
#define LED_COUNT               25
#define LED_GRID_W              5      // 0 width = treat as linear strip
#define LED_GRID_H              5
#define LED_GRID_SERPENTINE     true   // row-major, alternate rows reversed
#define LED_CHIPSET             WS2812B
#define LED_COLOR_ORDER         GRB
#define LED_FRAME_MS            20     // 50 fps budget; effects may request slower

// ---- Power budget (see docs/power.md) ----------------------------------------
// WS2812B worst case ~60 mA/px at full white. FastLED's power limiter scales
// global brightness so the *computed* frame never exceeds LED_PSU_MA.
#define LED_PSU_VOLTS           5
#define LED_PSU_MA              900    // LED-rail share of a 2 A bank port (ESP32+amp need the rest)
#define LED_BRIGHTNESS_MAX      160    // 0..255 hard cap (applied before the limiter)
#define LED_BRIGHTNESS_DEFAULT  128
#define LED_BRIGHTNESS_MIN      8      // never fully dark in "on" state (keeps bank alive)

// ---- Power-bank keep-alive ----------------------------------------------------
// Most banks drop the port below ~60-100 mA for ~10-30 s. During DAY (LEDs off)
// pulse a load so the bank stays up. LED pulse is dim warm-white on all pixels.
#define KEEPALIVE_ENABLED       true
#define KEEPALIVE_PERIOD_MS     8000
#define KEEPALIVE_PULSE_MS      600
#define KEEPALIVE_PULSE_LEVEL   60     // R channel 0..255 (G=2/3, B=1/4): 25 px * (60+40+15)/255 * 20 mA ≈ 225 mA + ESP32
#define KEEPALIVE_LOAD_PIN      -1     // optional NPN/MOSFET + 22R dump resistor; -1 = LEDs only

// ---- Ultrasonic (HC-SR04 class, 5 V, ECHO through 1k/2k divider) -------------
#define US_TRIG_PIN             32
#define US_ECHO_PIN             33
#define US_PING_PERIOD_MS       80     // >= 60 ms per datasheet to avoid echo overlap
#define US_ECHO_TIMEOUT_US      25000  // ~4.3 m; longer = no echo = treat as "far"
#define US_MIN_CM               3
#define US_MAX_CM               350
#define US_FILTER_N             5      // median window (odd)
#define US_MAX_JUMP_CM          120    // reject single-sample jumps larger than this
#define US_TRIGGER_CM           120    // closer than this = presence
#define US_RELEASE_CM           160    // must back off past this to re-arm (hysteresis)
#define US_CONFIRM_SAMPLES      3      // consecutive filtered samples below trigger
#define US_COOLDOWN_MS          8000   // min time between scare triggers
#define US_SENSOR_FAIL_MS       15000  // no valid echo for this long = sensor marked dead

// ---- Ambient light (fallback dusk sensor) ------------------------------------
// Option A: LDR divider on ADC1 (input-only pin, immune to WiFi ADC2 lockout).
// Option B: BH1750 on I2C. Set LIGHT_SENSOR_TYPE accordingly.
#define LIGHT_SENSOR_NONE       0
#define LIGHT_SENSOR_LDR        1
#define LIGHT_SENSOR_BH1750     2
#define LIGHT_SENSOR_TYPE       LIGHT_SENSOR_LDR
#define LDR_PIN                 34     // ADC1_CH6, input only
#define LDR_PULLDOWN_OHMS       10000  // LDR to 3V3, 10k to GND -> brighter = higher ADC
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22
#define BH1750_ADDR             0x23
#define LIGHT_SAMPLE_MS         1000
#define LIGHT_EMA_ALPHA         0.1f   // per-sample smoothing
#define LIGHT_DARK_ON_LUX       15.0f  // below -> dusk   (LDR: pseudo-lux from divider)
#define LIGHT_DARK_OFF_LUX      40.0f  // above -> dawn   (hysteresis band)
#define LIGHT_DEBOUNCE_MS       90000  // must stay past threshold this long (headlights!)

// ---- Dusk/dawn from solar position -------------------------------------------
#define GEO_LAT                 42.3314   // CHANGE ME (Detroit placeholder)
#define GEO_LON                 -83.0458  // CHANGE ME (west = negative)
// POSIX TZ string handles DST transitions without any code. Examples:
//   "EST5EDT,M3.2.0,M11.1.0"  "CST6CDT,M3.2.0,M11.1.0"  "PST8PDT,M3.2.0,M11.1.0"
//   "CET-1CEST,M3.5.0,M10.5.0/3"   "UTC0"
#define TZ_POSIX                "EST5EDT,M3.2.0,M11.1.0"
#define NTP_SERVER_1            "pool.ntp.org"
#define NTP_SERVER_2            "time.nist.gov"
#define SUN_ON_ELEVATION_DEG    -4.0f  // turn on when sun below this (0 = geometric sunset, -6 civil)
#define SUN_OFF_ELEVATION_DEG   -2.0f  // turn off when sun above this (hysteresis: on ≠ off)
#define TIME_VALID_MIN_YEAR     2024   // epoch below this = clock never synced
#define TIME_RESYNC_S           3600   // NTP refresh interval
#define SCHEDULE_EVAL_MS        10000  // how often the dusk/dawn state machine re-evaluates

// ---- Safe default when neither time nor light sensor is usable -----------------
#define FALLBACK_LIGHTS_ON      true   // a dark pumpkin is the worse failure on Halloween

// ---- Effects / schedule ----------------------------------------------------------
#define EFFECT_DEFAULT          "candle"
#define EFFECT_ROTATE           true
#define EFFECT_ROTATE_S         90     // seconds per effect in rotation
#define EFFECT_FADE_MS          1200   // crossfade between effects
#define EFFECT_PLAYLIST         { "candle", "ember", "breathe", "haunted", "fire", "sparkle", "rainbow", "heartbeat", "eyes", "lightning", "wipe" }
#define SCARE_EFFECT            "wakeup"
#define SCARE_DURATION_MS       6000
#define SCARE_SOUND             "/sounds/scare1.wav"
#define AMBIENT_SOUND           ""     // optional loop while on, "" = none

// ---- Audio (I2S -> internal DAC1 GPIO25 -> PAM8302 A+) -------------------------
#define AUDIO_ENABLED           true
#define AUDIO_DAC_PIN           25     // fixed by silicon: DAC1=25 (I2S right), DAC2=26
#define AUDIO_AMP_SD_PIN        26     // PAM8302 SD: low = shutdown (kills idle hiss)
#define AUDIO_VOLUME            0.7f   // 0..1 software gain
#define AUDIO_TASK_CORE         0      // keep off the LED core
#define AUDIO_TASK_PRIO         2
#define AUDIO_DMA_BUF_COUNT     8
#define AUDIO_DMA_BUF_LEN       256    // samples per DMA buffer
#define AUDIO_AMP_WARMUP_MS     30     // SD high -> first sample (pop suppression)

// ---- SD card (VSPI) ----------------------------------------------------------------
#define SD_CS_PIN               5
#define SD_SCK_PIN              18
#define SD_MISO_PIN             19
#define SD_MOSI_PIN             23
#define SD_SPI_HZ               20000000

// ---- Networking ------------------------------------------------------------------
#define NET_ENABLED             true
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RETRY_MS           30000  // backoff between reconnect attempts (non-blocking)
#define WEB_PORT                80
#define MQTT_ENABLED            true
#define MQTT_PORT               1883
#define MQTT_BASE_TOPIC         "pumpkin"       // pumpkin/<id>/state, pumpkin/<id>/cmd, pumpkin/all/cmd
#define MQTT_RETRY_MS           15000
#define MQTT_STATE_PERIOD_MS    30000
#define SYNC_ENABLED            true
#define SYNC_UDP_PORT           4210
#define SYNC_HEARTBEAT_MS       5000   // leader beacon period
#define SYNC_LEADER_TIMEOUT_MS  16000  // no beacon -> become leader (lowest id wins on collision)
#define SYNC_FOLLOW_SCARE       true   // neighbours flinch when one node is triggered

// ---- Inputs / misc -----------------------------------------------------------------
#define BUTTON_PIN              0      // BOOT button: short = next effect, long = toggle force-on
#define BUTTON_LONG_MS          1500
#define STATUS_LED_PIN          2
#define SERIAL_BAUD             115200

// ---- Robustness ---------------------------------------------------------------------
#define WDT_TIMEOUT_S           12     // task watchdog on loop + net task (net task may block ~9 s on MQTT connect)
#define LOOP_OVERRUN_WARN_MS    100    // log if a loop iteration exceeds this
#define HEAP_MIN_FREE_WARN      20000
