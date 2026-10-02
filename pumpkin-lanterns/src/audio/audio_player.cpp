#include "audio_player.h"
#include "config.h"
#include "sys/log.h"
#include <SD.h>
#include <SPI.h>
#include <driver/i2s.h>

namespace {
  struct Cmd { uint8_t type; char path[64]; };     // 1=play 2=stop 3=ambient on 4=ambient off
  volatile bool s_ambientOn = false; uint32_t s_ambientRetryAt = 0;
  QueueHandle_t s_q = nullptr;
  volatile bool s_playing = false, s_ok = false;
  float s_vol = AUDIO_VOLUME;
  char s_err[80] = "";
  char s_names[12][48]; uint8_t s_nNames = 0;

  struct WavInfo { uint32_t rate; uint16_t bits, ch; uint32_t dataOff, dataLen; };

  bool parseWav(File& f, WavInfo& w) {
    uint8_t h[12];
    if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { strcpy(s_err, "not RIFF/WAVE"); return false; }
    bool fmt = false;
    while (f.available()) {
      uint8_t ch[8]; if (f.read(ch, 8) != 8) break;
      uint32_t len = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
      if (!memcmp(ch, "fmt ", 4)) {
        uint8_t b[16]; if (f.read(b, 16) != 16) break;
        uint16_t af = b[0] | b[1] << 8; w.ch = b[2] | b[3] << 8;
        w.rate = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24; w.bits = b[14] | b[15] << 8;
        if (af != 1) { strcpy(s_err, "not PCM"); return false; }
        if (len > 16) f.seek(f.position() + len - 16);
        fmt = true;
      } else if (!memcmp(ch, "data", 4)) {
        w.dataOff = f.position(); w.dataLen = len; return fmt;
      } else f.seek(f.position() + len + (len & 1));
    }
    strcpy(s_err, "no data chunk"); return false;
  }

  void ampEnable(bool on) {
    if (AUDIO_AMP_SD_PIN >= 0) digitalWrite(AUDIO_AMP_SD_PIN, on ? HIGH : LOW);
  }

  // Convert a chunk of PCM to 16-bit unsigned stereo frames for the DAC and push to I2S.
  // Returns true and fills 'next' if interrupted by a new PLAY command.
  bool playFile(const char* path, Cmd& next, bool loop = false) {
    File f = SD.open(path);
    if (!f) { snprintf(s_err, sizeof(s_err), "open fail %s", path); LOGW("audio", "%s", s_err); return false; }
    WavInfo w{};
    if (!parseWav(f, w) || (w.bits != 8 && w.bits != 16) || w.ch < 1 || w.ch > 2 || w.rate < 8000 || w.rate > 48000) {
      LOGW("audio", "bad wav %s: %s (%u bit %u ch %lu Hz)", path, s_err, w.bits, w.ch, (unsigned long)w.rate); f.close(); return false;
    }
    LOGI("audio", "play %s %u-bit %uch %lu Hz %lu bytes", path, w.bits, w.ch, (unsigned long)w.rate, (unsigned long)w.dataLen);
    i2s_set_sample_rates(I2S_NUM_0, w.rate);
    i2s_zero_dma_buffer(I2S_NUM_0);
    ampEnable(true);
    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_WARMUP_MS));
    s_playing = true;
    f.seek(w.dataOff);
    static uint8_t in[512]; static uint16_t out[512];       // out: 256 frames * 2 ch
    uint32_t left = w.dataLen;
    uint16_t bps = (w.bits / 8) * w.ch;
    while (left > 0 || loop) {
      Cmd c; if (xQueueReceive(s_q, &c, 0) == pdTRUE) {       // interrupt?
        if (c.type == 2) { left = 0; break; }
        if (c.type == 3) s_ambientOn = true;
        if (c.type == 4) { s_ambientOn = false; if (loop) { left = 0; break; } }
        if (c.type == 1) { f.close(); i2s_zero_dma_buffer(I2S_NUM_0); s_playing = false; next = c; return true; }
      }
      if (loop && left == 0) { f.seek(w.dataOff); left = w.dataLen; }
      uint32_t want = min<uint32_t>(sizeof(in), left);
      want = min<uint32_t>(want, 256u * bps);                  // out[] holds 256 frames x 2 channels
      want -= want % bps;
      int got = f.read(in, want); if (got <= 0) break;
      left -= got;
      uint16_t frames = got / bps; uint16_t o = 0;
      for (uint16_t i = 0; i < frames; i++) {
        int32_t s = 0;
        for (uint16_t c2 = 0; c2 < w.ch; c2++) {
          if (w.bits == 16) s += (int16_t)(in[(i * w.ch + c2) * 2] | in[(i * w.ch + c2) * 2 + 1] << 8);
          else s += ((int16_t)in[i * w.ch + c2] - 128) << 8;
        }
        s = (int32_t)((s / w.ch) * s_vol);
        uint16_t u = (uint16_t)(s + 32768);                   // DAC wants unsigned; uses top 8 bits
        out[o++] = u; out[o++] = u;                            // both channels (DAC1 = right)
      }
      size_t written = 0;
      i2s_write(I2S_NUM_0, out, o * sizeof(uint16_t), &written, portMAX_DELAY);
    }
    f.close();
    i2s_zero_dma_buffer(I2S_NUM_0);
    vTaskDelay(pdMS_TO_TICKS(60));                            // drain DMA before muting
    ampEnable(false);
    s_playing = false;
    return false;
  }

  void audioTask(void*) {
    Cmd c;
    for (;;) {
      // Idle: run the ambient loop if enabled and the file exists; otherwise wait for a command.
      if (s_ambientOn && strlen(AMBIENT_SOUND_PATH) && (int32_t)(millis() - s_ambientRetryAt) >= 0) {
        Cmd next;
        if (playFile(AMBIENT_SOUND_PATH, next, true)) c = next;             // interrupted by a PLAY: handle it below
        else { if (s_ambientOn) s_ambientRetryAt = millis() + 60000; continue; }   // bad/missing file: pause a minute
      } else if (xQueueReceive(s_q, &c, pdMS_TO_TICKS(500)) != pdTRUE) continue;
      if (c.type == 3) { s_ambientOn = true; s_ambientRetryAt = 0; continue; }
      if (c.type == 4) { s_ambientOn = false; continue; }
      while (c.type == 1) { Cmd next; if (!playFile(c.path, next)) break; c = next; }   // chained interrupts, no recursion
    }
  }

  void scanSounds() {
    s_nNames = 0;
    File dir = SD.open("/sounds");
    if (!dir || !dir.isDirectory()) { LOGW("audio", "/sounds missing on SD"); return; }
    for (File e = dir.openNextFile(); e && s_nNames < 12; e = dir.openNextFile()) {
      const char* n = e.name();
      size_t l = strlen(n);
      if (!e.isDirectory() && l > 4 && strcasecmp(n + l - 4, ".wav") == 0 && n[0] != '.') {
        snprintf(s_names[s_nNames++], 48, "%s%s", n[0] == '/' ? "" : "/sounds/", n);
      }
    }
    LOGI("audio", "%u sounds on SD", s_nNames);
  }
}

bool Audio::begin() {
#if !AUDIO_ENABLED
  return false;
#endif
  if (AUDIO_AMP_SD_PIN >= 0) { pinMode(AUDIO_AMP_SD_PIN, OUTPUT); ampEnable(false); }
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) { strcpy(s_err, "SD mount failed"); LOGW("audio", "%s (audio disabled)", s_err); return false; }
  LOGI("audio", "SD %lu MB", (unsigned long)(SD.cardSize() / (1024 * 1024)));
  scanSounds();

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = 22050;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = AUDIO_DMA_BUF_COUNT;
  cfg.dma_buf_len = AUDIO_DMA_BUF_LEN;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) { strcpy(s_err, "i2s install failed"); LOGW("audio", "%s", s_err); return false; }
  i2s_set_dac_mode(AUDIO_DAC_PIN == 25 ? I2S_DAC_CHANNEL_RIGHT_EN : I2S_DAC_CHANNEL_LEFT_EN);
  i2s_set_pin(I2S_NUM_0, nullptr);                              // internal DAC
  i2s_zero_dma_buffer(I2S_NUM_0);

  s_q = xQueueCreate(4, sizeof(Cmd));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, AUDIO_TASK_PRIO, nullptr, AUDIO_TASK_CORE);
  s_ok = true;
  LOGI("audio", "I2S->DAC GPIO%d, amp SD GPIO%d, task on core %d", AUDIO_DAC_PIN, AUDIO_AMP_SD_PIN, AUDIO_TASK_CORE);
  return true;
}

bool Audio::available() { return s_ok; }
void Audio::play(const char* path) {
  if (!s_ok || !path || !*path) return;
  Cmd c{1, {0}}; strncpy(c.path, path, sizeof(c.path) - 1);
  xQueueSend(s_q, &c, 0);
}
void Audio::stop() { if (!s_ok) return; Cmd c{2, {0}}; xQueueSend(s_q, &c, 0); }
void Audio::playRandom(const char* prefix) {
  if (!s_ok || !prefix) return;
  uint8_t idx[12], n = 0;
  for (uint8_t i = 0; i < s_nNames; i++) if (strncasecmp(s_names[i], prefix, strlen(prefix)) == 0) idx[n++] = i;
  if (n == 0) { LOGW("audio", "no sound matching %s*", prefix); return; }
  play(s_names[idx[esp_random() % n]]);
}
void Audio::setAmbient(bool on) { if (!s_ok) return; Cmd c{(uint8_t)(on ? 3 : 4), {0}}; xQueueSend(s_q, &c, 0); }
bool Audio::ambientEnabled() { return s_ambientOn; }
bool Audio::hasSound(const char* path) { for (uint8_t i = 0; i < s_nNames; i++) if (!strcasecmp(s_names[i], path)) return true; return false; }
bool Audio::isPlaying() { return s_playing; }
void Audio::setVolume(float v) { s_vol = v < 0 ? 0 : v > 1 ? 1 : v; }
float Audio::volume() { return s_vol; }
uint8_t Audio::soundCount() { return s_nNames; }
const char* Audio::soundName(uint8_t i) { return i < s_nNames ? s_names[i] : ""; }
const char* Audio::lastError() { return s_err; }
