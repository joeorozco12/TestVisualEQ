# Power budget, voltage drop, enclosure

## Current per node (5 V rail)

| Load | Typical | Peak | Notes |
|---|---|---|---|
| ESP32, WiFi on, modem sleep off | 110–160 mA | 400 mA (TX bursts, ~10 ms) | `WiFi.setSleep(false)` on purpose: keeps the bank loaded and UDP latency steady. |
| 25× WS2812B, candle/ember palettes @ cap 160/255 | 150–350 mA | — | measured with `bench_leds` mode `p` vs. your USB meter |
| 25× WS2812B full white @ 160/255 | — | 940 mA → limiter clamps to `LED_PSU_MA` = 900 | 25 px × 60 mA × 160/255 |
| 25× WS2812B full white uncapped (never happens) | — | 1.5 A | the reason the cap exists |
| PAM8302 + 4 Ω speaker | 20–150 mA | 600 mA (2.5 W bursts) | muted via SD pin when idle |
| HC-SR04 | 15 mA | — | |
| µSD | 5 mA | 100 mA (write; we only read) | |
| **Sum** | **≈ 0.4–0.6 A** | **≈ 1.9 A if everything peaks at once** | a 2 A / 2.4 A port is required, 1 A is not |

Runtime on a 10 Ah (37 Wh) bank at 0.5 A average and ~85 % conversion efficiency: ≈ 12–13 h, i.e. one full
dusk-to-dawn night with margin. 20 Ah covers two nights.

Brightness cap vs. budget: `LED_BRIGHTNESS_MAX` is the hard ceiling; `FastLED.setMaxPowerInVoltsAndMilliamps`
additionally scales any single frame whose computed draw exceeds `LED_PSU_MA`. Together they make the LED rail
unconditionally ≤ 0.9 A regardless of effect or user slider.

## Power bank auto-off

Banks cut the port when load < ~60–150 mA for 10–30 s. Two defences, both in config:

1. ESP32 with modem sleep off alone draws >100 mA, above most thresholds.
2. During DAY (LEDs black) a `KEEPALIVE_PULSE_MS` warm-dim LED pulse every `KEEPALIVE_PERIOD_MS` adds ~225 mA.
   Invisible in daylight inside a pumpkin. `bench_power` runs 60 s of (1) alone then 120 s of (1)+(2) so you can
   see which your bank needs; some banks need the period shortened to ~5 s.

If a bank still drops out, use the optional dump load (`KEEPALIVE_LOAD_PIN`, 22 Ω/2 W via NPN = 230 mA) or a
bank marketed "always-on / trickle mode" (Anker, Voltaic).

## Voltage drop

- Resistance: 22 AWG ≈ 53 mΩ/m, 24 AWG ≈ 84 mΩ/m, 28 AWG (cheap USB) ≈ 213 mΩ/m. Round trip doubles it.
- A 1 m 28 AWG USB cable at 1.5 A: 2 × 0.213 × 1.5 = **0.64 V** → 4.36 V at the node. ESP32 LDO is fine (needs ≥3.6 V in),
  WS2812B gets dimmer/redder and its VIH drops to 3.05 V (why 3.3 V data "sometimes works").
- Use a ≤0.5 m cable with 24 AWG or better power pair, or solder to the bank's output directly if it's a cheap one you don't mind.
- 5 V from the ESP32 "VIN/5V" pin to the grid: ≤15 cm of 22 AWG, 1000 µF at the grid, 100 µF at the amp.
- Do not power the grid through the DevKit's PCB trace from USB: it is ~0.5 A rated. Bring 5 V to the grid and the DevKit in parallel (star from the cable).

## Level shifting and signal integrity

- 74AHCT125 at 5 V; input threshold 1.6 V, so 3.3 V in is clean. Output through 330 Ω into DIN damps the ring on the pigtail.
- Keep LED data < 15 cm before the shifter; after the shifter up to ~1 m is fine. Twist data with ground.
- ECHO divider 1 k / 2 k gives 3.33 V. HC-SR04 TRIG accepts 3.3 V.

## Enclosure: pumpkin interior

- Interior is wet, slightly acidic, 100 % RH and rots within ~5 days. Nothing bare touches flesh.
- Electronics + bank in a small IP54 box (or a lidded food container) with a silica gel pack. Cables exit at the bottom with a drip loop.
- LED grid: conformal-coat the back, mount on a plastic plate or inside a zip bag; it runs cool (≤0.3 W typical) so no thermal concern.
- Ultrasonic: transducers degrade when wet. Mount in the mouth/eye cut-out under a lip, facing slightly down, and accept it may die after a rainy night: firmware marks it unhealthy after 15 s without echoes and keeps running without proximity.
- Speaker: faces a cut-out; rear sealed from the flesh. A 2" full-range in a small sealed pod is enough.
- Cold: ESP32 and WS2812B are fine to −20 °C. Bank capacity drops ~20 % at 0 °C; keep it in the box.
- Heat: none (no candle), so the real risk is condensation on warm-up at dusk; the conformal coat covers that.
- Firmware safety nets: task watchdog (12 s) on loop and net tasks; brownout detector on; every sensor has a healthy flag and a fallback; `FALLBACK_LIGHTS_ON` means a dead clock + dead LDR still gives you a lit pumpkin.
