# Bring-up / test plan

Each bench env links the same module sources as the full firmware, swapping only `main.cpp`.
Pass each step before wiring the next. Serial monitor at 115200.

| # | Env | Hardware needed | Procedure | Pass criteria |
|---|---|---|---|---|
| 0 | — | ESP32 only | `pio run -e node` (compile only) | all 8 envs build; `secrets.h` present |
| 1 | `bench_leds` | ESP32 + shifter + grid + 1000 µF, bench PSU 5 V/2 A with ammeter | keys `r g b w` | solid pure red / green / blue (if not: fix `LED_COLOR_ORDER`). `w` current < 0.9 A even at cap (limiter). |
| 1b | `bench_leds` | same | key `x` then `m` | `x` walks every pixel once with no gaps/flicker (data integrity); `m` lights (x,y) in reading order top-left → bottom-right (serpentine map). |
| 1c | `bench_leds` | same | `n` through all 13 effects, `+`/`-` | no stutter, every effect looks intended, reactivity bursts every 20 s visibly change candle/ember/heartbeat/wakeup. |
| 2 | `bench_ultrasonic` | + HC-SR04 with ECHO divider | walk 3 m → 0.5 m → away | `raw` stable ±2 cm on a wall; one `TRIGGER` at < 120 cm, `presence=1`, no retrigger inside 8 s, release only beyond 160 cm. Wave a hand quickly: single outliers rejected. Unplug ECHO: `healthy=0` after 15 s, no crash. |
| 3 | `bench_light` | + LDR divider | cover/uncover; then run at real dusk | `healthy=1`; state flips only after 90 s past threshold; note lux at the moment you'd want lights on and set `LIGHT_DARK_ON_LUX` (ON) and ~2.5× that for OFF. |
| 4 | `bench_sun` | WiFi | boot, wait for NTP | self-test prints ≈66.5 / −66.5 and Detroit sunset ≈22:26 UTC with no WiFi; after sync, local time with correct DST suffix and tonight's ON time matches a weather app ± 5 min (we trigger at −4°, ~20 min after sunset). Pull WiFi: time keeps running. |
| 5 | `bench_audio` | + SD + PAM8302 + speaker, bench PSU | `t` then `0` | `t` = clean 440 Hz tone (DAC → amp path). `0` plays scare1.wav; no pop at start/stop (SD pin timing); `+`/`-` changes level. Pull the card: "SD mount failed", no reboot. |
| 6 | `bench_net` | WiFi, 2+ boards | open `http://pumpkin-<id>.local`, MQTT `pumpkin/all/cmd {"next":true}` | web page loads with live state; MQTT command changes both boards; lower-id board prints `leader=1`; `loopmax` stays < 30 ms while hammering the web page. Kill the AP: LEDs never stutter, reconnect within 30 s of AP return. |
| 7 | `bench_power` | power bank + inline USB meter | run the three phases ≥ 2 cycles | bank never drops out in phase A or B (if it drops in A but not B, keep-alive is required; if in both, shorten `KEEPALIVE_PERIOD_MS`). Phase C peak current → fill in `docs/power.md`. |
| 8 | `node` | everything, on the bench | force modes from web UI; `scare` button; cover LDR with time unsynced (no WiFi) | all of the above together; reset reason stays `poweron`/`sw` across a night (no `task_wdt`/`brownout`). |
| 9 | `node` | in the pumpkin | one night outdoors | turns on ~20 min after sunset, off ~15 min before sunrise; still on at dawn means the bank held. Check `heap` in the UI next morning (should not have trended down). |

## Round-2 features (all exercised from the web UI on the full `node` build)

| Feature | How to test | Pass |
|---|---|---|
| Escalation | Press **Scare!** three times within 60 s | haunted (no sound) → eyes + sound → full wakeup + sound; `scareLevel` in the UI climbs; waits 60 s → back to level 1 |
| Neighbour alert | Two nodes; trip one | the other shows `alert`, eyes glance toward the tripped node, reactivity floor visible in candle/ember |
| Fleet chase | Two or more nodes, effect `chase` | pulse travels low id → high id with ~350 ms steps; `rainbow` hues offset per node |
| Text | UI text box "BOO", effect `text` | scrolls right-to-left on the grid; strip build just blinks |
| Scenes | set `SCENE_TABLE` to a time 2 min ahead, watch log | `scene -> friendly` with brightness/scares as configured; Scare! does nothing in a scares-off scene |
| Idle | temporarily set `IDLE_SLEEP_NO_TRIGGER_MS 60000` and the hour window to now | ember at 40 after a minute; walk up → wakes to scene for 5 min |
| Weather | watch `wx` log line after WiFi; set `WEATHER_WIND_FULL_KMH 1` to force | candle gusts noticeably deeper; log shows wind/precip |
| Ambient | put `ambient.wav` in /sounds | loops while lit, stops when dark or idle, resumes after a scare |
| OTA | `pio run -e node -t upload --upload-port pumpkin-<id>.local --upload-flags --auth=pumpkin` | uploads, reboots, LEDs keep running until the reboot |
| HA discovery | MQTT broker + Home Assistant | a light entity `pumpkin-<id>` appears with the effect list; on/off maps to force on/off, brightness and effect work |
| Portal | hold BOOT ~1 s right after power-up | fast-blinking status LED; phone sees `pumpkin-<id>-setup`; sign-in page opens; save → reboots and joins |
| Mic / PIR / VBUS | enable in config, wire per hookup §6b | `mic` in UI rises with a clap; PIR trips a scare; `vbus` reads within 0.1 V of a meter and the cap drops below 4.6 V |

## Things that bite

- `FASTLED_RMT_BUILTIN_DRIVER=0` with arduino-esp32 3.x breaks; this project pins platform 6.9.0 on purpose. Do not "update".
- GPIO 34 has no pull-up/down: the 10 k is mandatory or the LDR reading floats.
- A DevKit powered from its own USB *and* a 5 V rail at the same time back-feeds through the Schottky; power everything from the bank and use USB only for serial with the 5 V pin of the USB-serial side disconnected, or just use the bank + a monitor over the DevKit USB while the bank is unplugged.
- PAM8302 pops on enable if the DAC is mid-scale while SD rises; `AUDIO_AMP_WARMUP_MS` + zeroed DMA buffer handles it. If it still pops, raise it to 60.
- If MQTT_HOST is unreachable, connect attempts take ~3 s every 15 s on the net task only; the LED loop is unaffected. Set `MQTT_HOST ""` to disable.
