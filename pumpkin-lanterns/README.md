# Pumpkin Lanterns

ESP32 lantern nodes (3–6), each with a 5×5 WS2812B grid inside a pumpkin. Dusk-to-dawn
from NTP + solar position (light-sensor fallback), ultrasonic scare trigger with optional
WAV playback, WiFi sync / web UI / MQTT, fully standalone when WiFi is absent.

Firmware: PlatformIO, `espressif32@6.9.0` (arduino-esp32 2.0.17). Everything tunable is in
[`include/config.h`](include/config.h); WiFi/MQTT credentials in `include/secrets.h`
(copy from `secrets.example.h`).

```
pio run -e node -t upload && pio device monitor      # full firmware
pio run -e bench_leds -t upload                      # one of 7 bench sketches (see docs/bringup.md)
```

## Block diagram

```mermaid
flowchart LR
  subgraph PWR[USB power bank 5V/2A]
  end
  subgraph ESP[ESP32 DevKit]
    C1[core 1: loop\nLEDs · sensors · scheduler\n(task WDT)]
    C0[core 0: net task\nWiFi · web · MQTT · UDP sync]
    AT[core 0: audio task\nSD → WAV → I2S DAC]
  end
  LS[74AHCT125\nlevel shift] --> GRID[5×5 WS2812B]
  C1 -- GPIO27 --> LS
  US[HC-SR04] -- ECHO ÷ (1k/2k) GPIO33 --> C1
  C1 -- TRIG GPIO32 --> US
  LDR[LDR + 10k] -- GPIO34 ADC1 --> C1
  SD[(µSD VSPI)] --> AT
  AT -- DAC1 GPIO25 --> AMP[PAM8302] --> SPK((4–8 Ω))
  C1 -- SD GPIO26 (mute) --> AMP
  C0 <-- cmd queue / state snapshot --> C1
  C0 <-- UDP 4210 broadcast --> PEERS[other nodes]
  C0 <--> WIFI((WiFi / NTP / MQTT))
  PWR --> ESP & GRID & AMP & US
```

## Wiring / pin assignment (ESP32 DevKit, classic WROOM)

Point-to-point build order with part pinouts: [docs/hookup.md](docs/hookup.md).

| Signal | GPIO | Notes |
|---|---|---|
| LED data | 27 | → 74AHCT125 (5 V side) → 330 Ω → grid DIN. 1000 µF ≥6.3 V across grid 5 V/GND, as close to the grid as possible. Keep data lead < 15 cm before the shifter. |
| Ultrasonic TRIG | 32 | 3.3 V drive is fine for HC-SR04. |
| Ultrasonic ECHO | 33 | 5 V output: 1 kΩ series + 2 kΩ to GND divider. Never direct. |
| LDR | 34 (ADC1_CH6) | LDR from 3V3 to pin, 10 kΩ pin to GND, 100 nF pin to GND. ADC1 only: ADC2 is dead while WiFi runs. |
| BH1750 (optional) | 21 SDA / 22 SCL | Set `LIGHT_SENSOR_TYPE` to BH1750. |
| Audio out | 25 (DAC1) | → 1 kΩ → PAM8302 A+, A− to GND. Internal DAC, no external codec needed. |
| Amp shutdown | 26 | PAM8302 SD. Held low unless playing: kills idle hiss, saves ~5 mA. |
| µSD | 5 CS / 18 SCK / 19 MISO / 23 MOSI | Use a 3.3 V SD breakout (no onboard level shifter; those are flaky at 20 MHz). FAT32, `/sounds/*.wav`. |
| Button | 0 (BOOT) | short = next effect, long = toggle force-on. |
| Status LED | 2 | slow blink = WiFi up, fast = standalone. |
| Keep-alive dump load (optional) | 4 | NPN/MOSFET + 22 Ω/2 W. Default off; LEDs provide the pulse. |

Pins avoided: 6–11 (flash), 12 (strap, boot fails if high), 15 (strap), 25/26 are DACs so
they are reserved for audio, ADC2 pins for analog.

Why a level shifter: WS2812B VIH = 0.7·VDD = 3.5 V at 5 V. A 3.3 V output "usually works",
then stops working when the bank's cable drop is small and VDD is a full 5.0 V. Not worth
debugging at 9 pm on Halloween.

## BOM per node (what you have vs. what to order)

| Item | Have? | Notes |
|---|---|---|
| ESP32 DevKit (WROOM) | yes | WROVER also works, but GPIO16/17 are then PSRAM: fine, we don't use them. |
| 5×5 WS2812B grid | yes | Grid PCBs carry the per-pixel 100 nF. |
| HC-SR04 | yes | Transducers hate moisture: mount under a lip, face slightly down. |
| PAM8302 + 4–8 Ω speaker | yes | Trimpot on the module ≈ ⅓ turn; software volume does the rest. |
| µSD module + card | yes | Class 10, FAT32, ≤32 GB. |
| USB power bank ≥10 Ah, 2 A port | yes | Must not auto-off at ~150 mA; `bench_power` tells you. |
| 74AHCT125 (or 74HCT245 / SN74AHCT1G125) | **order** | One per node. |
| 1000 µF electrolytic, 330 Ω, 1 kΩ ×2, 2 kΩ, 10 kΩ, 100 nF | **order** | Through-hole is fine. |
| LDR (GL5528 class) | **order** unless in the sensor box | Any LDR; thresholds are calibrated on the bench. |
| Inline USB current meter | **order** | For `bench_power` and the current table. |
| Small weatherproof box (IP54+) + silica gel pack | **order** | Electronics + bank live in the box inside the pumpkin. |
| Conformal coat or clear nail varnish | **order** | LED grid back and ESP32 board edges. |

## Project structure

```
pumpkin-lanterns/
├── platformio.ini            envs: node + 7 bench_* envs (same module code, different main)
├── include/config.h          every pin, count, threshold, timing, schedule
├── include/secrets.h         WiFi/MQTT creds (gitignored; copy secrets.example.h)
├── src/main.cpp              integration: loop on core 1, dispatches commands, scheduler
├── src/sys/                  log, node identity + task watchdog + loop health, button
├── src/effects/              effect.h (plugin interface) · effect_table.cpp (registry)
│                             engine.cpp (frame timing, crossfade, scare overlay, power cap)
│                             fx_flame / fx_pulse / fx_motion / fx_spooky (13 effects)
├── src/dusk/                 sun.cpp (NOAA solar) · timekeeper (NTP+POSIX TZ) · light_sensor · dusk_dawn (FSM)
├── src/proximity/            ultrasonic (ISR-timed, median, outlier reject, hysteresis, cooldown, envelope)
├── src/audio/                audio_player (SD WAV → I2S built-in DAC, own task on core 0)
├── src/net/                  net_manager (WiFi FSM, web, MQTT, UDP sync, leader election) · web_page.h · commands.h
├── src/bench/                bench_leds / ultrasonic / light / sun / audio / net / power
├── data/sounds/              put scare1.wav here, copy to the SD card
└── docs/                     hookup.md (point-to-point wiring) · bringup.md (test plan) · power.md (budget, drop, enclosure)
```

## Design choices (one line each)

- **FastLED on RMT, dithering off.** RMT output is hardware-timed, immune to WiFi ISR jitter; dithering plus WiFi produces visible shimmer on small grids.
- **Loop on core 1, net + audio on core 0.** The LED frame never waits on a socket or an SD read. Cross-core traffic is a FreeRTOS queue of `Command` and a mutex-copied `NodeState`.
- **Sun elevation with hysteresis, not sunrise/sunset times.** One formula, no date edge cases, DST-free because it works in UTC; the POSIX TZ string only exists for logs and the UI.
- **No leader election protocol, just "lowest id beacons".** Any node whose id is the lowest it has heard in 16 s beacons effect + phase + seed every 5 s; followers align if drift > 400 ms. Nodes that never hear anyone are their own leader, so standalone is the same code path.
- **Internal DAC → PAM8302** instead of an I2S DAC: you own the PAM8302, 8-bit effective resolution is plenty for a growl, and it frees the I2S pins.
- **Scare on neighbours is lights-only.** One trigger, one sound; six pumpkins all screaming is a different project.
- **Fallback = lights on.** If both the clock and the light sensor are dead, a dark pumpkin is the worse failure.

## Interfaces

- Web: `http://pumpkin-<id>.local/` (mDNS) — state, effect picker, mode, scare, sliders, sounds.
- HTTP API: `GET /api/state` (JSON), `GET /api/cmd?type=effect|next|brightness|mode|scare|speed|hue|rotate|play|volume|reboot&value=N&str=S`.
- MQTT: subscribe `pumpkin/<id>/cmd` and `pumpkin/all/cmd` with JSON like
  `{"effect":"fire"}`, `{"brightness":120}`, `{"mode":"on"|"off"|"auto"}`, `{"scare":true}`, `{"next":true}`, `{"rotate":false}`, `{"volume":60}`, `{"play":"/sounds/x.wav"}`.
  State on `pumpkin/<id>/state` every 30 s; LWT `pumpkin/<id>/status` online/offline.
- Adding an effect: write `fx_<name>_render(EffectCtx&)` (+ optional `_init`), add one line to `EFFECT_TABLE`, add the name to `EFFECT_PLAYLIST` if it should rotate.
