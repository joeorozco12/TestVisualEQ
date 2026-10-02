# Hookup guide (one node)

Point-to-point for an ESP32 DevKit (30/38-pin WROOM). Build in the order listed; each stage
matches a bench env in [bringup.md](bringup.md). Pin numbers are ESP32 GPIO numbers, not
header positions. Use 22 AWG for every 5 V/GND run, 26–28 AWG for signals.

## 0. Power distribution (do this first, test with nothing else attached)

```
Power bank 2 A port ──USB cable (≤0.5 m, 24 AWG power pair)──┐
                                                             │ cut/breakout: VBUS + GND
                 ┌───────────────────────────────────────────┤  5 V star point
                 │                 │                 │       │
            ESP32 "VIN"/5V    74AHCT125 VCC     PAM8302 VIN   HC-SR04 VCC
            ESP32 GND         + 100 nF          + 100 µF      GND
                 │
            LED grid 5V ── 1000 µF (6.3 V+) ── LED grid GND      (cap leads ≤ 2 cm from the grid pads)
```

- Star the 5 V and GND from one point at the cable breakout (a small screw terminal block or a scrap of perfboard). Do not daisy-chain the grid off the DevKit's 5 V pin: that pin is behind a ~0.5 A trace.
- One GND reference for everything. The level shifter and the ultrasonic divider only work if their GND is the ESP32's GND.
- The bank's USB-A → cut cable is the simplest breakout. A USB-A breakout board (VBUS/GND screw terminals) is cleaner.
- Serial monitor while running on the bank: plug the DevKit's micro-USB into the PC **only with the bank unplugged**, or cut the DevKit's 5 V input diode path (don't). Two 5 V sources into the DevKit back-feed each other.

Check: 5.0 V ± 0.2 at the star point under 0.5 A; ESP32 boots (blue status LED GPIO2 blinks fast = no WiFi, expected).

## 1. LED grid (bench_leds)

```
ESP32 GPIO27 ──────────► 74AHCT125 pin 2 (1A)
GND ───────────────────► 74AHCT125 pin 1 (1OE)       OE is active-low: tie to GND
74AHCT125 pin 3 (1Y) ──► 330 Ω ──► grid DIN
74AHCT125 pin 14 (VCC) ◄── 5 V star, 100 nF to pin 7 (GND)
Unused gates: tie 2OE,3OE,4OE (pins 4,10,13) to GND, inputs 2A,3A,4A (pins 5,9,12) to GND
grid 5V / GND ◄── 5 V star, 1000 µF across them at the grid
```

- Keep GPIO27 → shifter ≤ 15 cm. Shifter → grid may be up to ~1 m (twist DIN with GND).
- If you use a 74HCT245 instead: DIR (pin 1) to 5 V, OE (pin 19) to GND, A1 in, B1 out. SN74AHCT1G125 single-gate: A=GPIO27, OE=GND, Y=330 Ω→DIN.
- 5×5 grid orientation: DIN corner is (x=0,y=0). If `bench_leds` mode `m` walks the wrong way, flip `LED_GRID_SERPENTINE` or rotate the grid; don't rewire.
- Colour order: if `r` lights green, set `LED_COLOR_ORDER` to RGB.

## 2. Ultrasonic (bench_ultrasonic)

```
HC-SR04 VCC  ◄── 5 V star        (3.3 V supply gives short range and erratic echoes)
HC-SR04 GND  ◄── GND
HC-SR04 TRIG ◄── ESP32 GPIO32    (direct; 3.3 V is a valid high)
HC-SR04 ECHO ──► 1 kΩ ──┬──► ESP32 GPIO33
                        └── 2 kΩ ──► GND          (5 V × 2/3 = 3.33 V)
```

- Mount: transducers facing the approach path, 40–100 cm above ground, under a lip. Pointing slightly down helps reject rain and distant cars.
- Signal leads up to ~1 m are fine; longer, add a 100 nF across VCC/GND at the sensor.

## 3. Ambient light (bench_light)

```
3V3 ──► LDR ──┬──► ESP32 GPIO34
              ├── 10 kΩ ──► GND
              └── 100 nF ──► GND
```

- GPIO34 is input-only with no internal pull: the 10 kΩ is mandatory.
- The LDR must see the sky, not the pumpkin's own LEDs: outside the shell, on top, under a drop of hot glue, or looking out a dedicated 5 mm hole on the side away from the LED grid. Self-illumination makes it think it's day and turns itself off (only matters when NTP has failed, but that's the night you'll care).
- BH1750 instead: VCC 3V3, GND, SDA GPIO21, SCL GPIO22, ADDR open; set `LIGHT_SENSOR_TYPE LIGHT_SENSOR_BH1750`.

## 4. Audio (bench_audio)

```
ESP32 GPIO25 (DAC1) ──► 1 kΩ ──► PAM8302 A+
GND ────────────────────────────► PAM8302 A−
ESP32 GPIO26 ──────────────────► PAM8302 SD        (low = shutdown; firmware raises it only while playing)
PAM8302 VIN ◄── 5 V star, 100 µF to GND at the module
PAM8302 GND ◄── GND
PAM8302 + / − ──► speaker 4–8 Ω  (both leads from the amp; never ground a speaker lead, it's a bridged output)
```

- The DAC output sits at 1.65 V DC; the PAM8302 inputs are AC-coupled internally, so no extra cap. The 1 kΩ just protects the DAC from the module's input cap inrush.
- Start with the module's gain pot at ~⅓. Software `AUDIO_VOLUME` 0.7 then tune in the web UI.
- Hiss at idle = SD not wired or `AUDIO_AMP_SD_PIN` wrong. Pop at start = raise `AUDIO_AMP_WARMUP_MS`.

```
µSD module (3.3 V type, no onboard shifter):
  VCC  ◄── 3V3        GND ◄── GND
  CS   ◄── GPIO5      SCK ◄── GPIO18      MISO ──► GPIO19      MOSI ◄── GPIO23
```

- GPIO5 is a strapping pin: the card's CS pull-up keeps it high at boot, which is the correct state. If the board fails to boot with the card inserted, add a 10 kΩ pull-up on CS.
- Keep SD leads short (< 10 cm); 20 MHz SPI on jumper wires is the limit. Drop `SD_SPI_HZ` to 10000000 if you get mount failures.
- Card: FAT32, `/sounds/scare1.wav` (22050 Hz, 16-bit, mono).

## 5. Button and status

- BOOT button (GPIO0) is already on the DevKit. External button: GPIO0 to GND, internal pull-up is enabled.
- Status LED is the on-board GPIO2 LED. Slow 1 Hz blink = WiFi up; 2.5 Hz = standalone.

## 6. Optional keep-alive dump load

Only if `bench_power` shows the bank drops out even with LED pulses:

```
ESP32 GPIO4 ──► 1 kΩ ──► base of 2N2222 / gate of 2N7000 (logic-level MOSFET preferred)
collector/drain ──► 22 Ω 2 W ──► 5 V star      emitter/source ──► GND
```

Set `KEEPALIVE_LOAD_PIN 4`. ~230 mA pulse, 0.6 s every 8 s: ~0.1 W average, resistor stays warm.

## 7. Assembly order inside the pumpkin

1. Box (IP54 or lidded food container): ESP32 on standoffs, level shifter + resistors on a 3×4 cm perfboard, PAM8302, SD module, screw-terminal star point, silica gel. Cables exit a single grommet at the bottom.
2. Grid on a plastic plate or in a zip bag, hung from a skewer across the top of the pumpkin so it illuminates the cut-outs from behind. Conformal coat the grid's back first.
3. Speaker in a sealed pod (film canister / small jar) behind the mouth cut-out.
4. Ultrasonic in an eye cut-out or a dedicated slot, under the lid lip, pointing at the approach.
5. LDR on top of the pumpkin, under a hot-glue dome, lead through the stem hole.
6. Power bank in the box or in its own bag; USB cable ≤ 0.5 m to the star point.
7. Lid on, drip loop on every cable, run `node` and watch the web UI for an hour before the first night.

## Quick continuity checklist before first power

| Check | Expect |
|---|---|
| 5 V star to GND | > 1 kΩ (no short); 1000 µF charging kick on a DMM is normal |
| GPIO25/26 to GND | > 10 kΩ (DAC/amp SD not shorted) |
| GPIO33 to GND | ≈ 2 kΩ (divider present) |
| GPIO34 to GND | ≈ 10 kΩ in the dark, lower in light |
| 74AHCT125 pin 1 and 14 | GND and 5 V respectively (OE low, VCC right) |
| Grid 5V to GND | > 100 Ω |
| Speaker leads | neither to GND |
