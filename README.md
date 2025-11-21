# Amiga Digital Sampler

This project implements a pro-audio sampler that sends real-time audio from S/PDIF → Raspberry Pi → SPI → Raspberry Pi Pico PIO → 74HCT245 → Amiga Parallel Port, with a 100% digital path. 

It provides:

* Real-time DSP (FIR LPF, DC-block, noise shaping, optional dither)
* Hard-synchronized output at 27.928 kHz (ProTracker A-3)
* Noise-shaped 8-bit unsigned Amiga-compatible samples
* Test-tone mode for hardware bring-up
* PIO-driven parallel output via 74HCT245 level shifting

## Motivation

Traditional analog Amiga parallel-port samplers are often noisy, have DC offsets and dynamics issues. This provides a fully digital path from S/PDIF to the parallel port. 

By combining modern digital filtering, proper anti-aliasing, stable clocking, and high-quality 8-bit noise-shaped conversion, it produces results that far exceed what vintage hardware could achieve—all while remaining fully compatible with the Amiga’s unsigned 8-bit parallel sample format.

---

# Bill of Materials (BOM)

### **Core Hardware**

* **Raspberry Pi 4B** — performs DSP + SPI output
* **Raspberry Pi Pico** — PIO playback engine
* **HiFiBerry Digi+** — S/PDIF → I²S input frontend
* **74HCT245** — 3.3V→5V level shifter for 8-bit parallel bus
* **DB25 male connector** — connection to Amiga parallel port
* **Resistors for STROBE level shifter:**

  * 100Ω (series)
  * 10kΩ (pulldown)
  * 20kΩ (divider)
* **Misc wiring:**

  * Dupont wires or PCB
  * Breadboard or perfboard
  * USB power cables

### **Power**

* Pi 4: 5V/3A USB-C PSU
* Pico: USB power via Pi

Alternatively, you can power the Pico from the Pi's GPIO.

---

# Signal Flow Overview

```
S/PDIF → HiFiBerry Digi+ → I²S → Raspberry Pi → DSP → SPI → Pico → PIO → 74HCT245 → Amiga Parallel Port
```

---

# Wiring Guide

## Raspberry Pi 4B → Raspberry Pi Pico (SPI0)

| Pi Pin  | GPIO   | Function  | Pico Pin | Notes              |
| ------- | ------ | --------- | -------- | ------------------ |
| 19      | GPIO10 | SPI0 MOSI | GP16     | Pico receives data |
| 23      | GPIO11 | SPI0 SCLK | GP18     | Clock              |
| 24      | GPIO8  | SPI0 CE0  | GP17     | Chip select        |
| Any GND | —      | Ground    | GND      | Must share ground  |

---

## Pico → 74HCT245 (8-bit data bus)

| Pico Pin | Signal | 74HCT245 Pin | Description                |
| -------- | ------ | ------------ | -------------------------- |
| GP2      | D0     | A0           | LSB                        |
| GP3      | D1     | A1           |                            |
| GP4      | D2     | A2           |                            |
| GP5      | D3     | A3           |                            |
| GP6      | D4     | A4           |                            |
| GP7      | D5     | A5           |                            |
| GP8      | D6     | A6           |                            |
| GP9      | D7     | A7           | MSB                        |
| GP11     | /OE    | OE           | Output enable (active LOW) |
| 5V       | DIR    | DIR          | Set for A→B direction      |
| 5V       | VCC    | —            |                            |
| GND      | GND    | —            | Common ground              |

### Output Enable

Pico drives **GP11 = 0** to enable the 74HCT245 outputs.

---

## 74HCT245 → Amiga DB25 Parallel Port

| 74HCT245 B-side     | Amiga DB25 Pin | Name   |
| ------------------- | -------------- | ------ |
| B0                  | 2              | D0     |
| B1                  | 3              | D1     |
| B2                  | 4              | D2     |
| B3                  | 5              | D3     |
| B4                  | 6              | D4     |
| B5                  | 7              | D5     |
| B6                  | 8              | D6     |
| B7                  | 9              | D7     |
| (STROBE from Amiga) | 1              | STROBE |
| GND                 | 18–25          | Ground |

---

# STROBE Level Shifter (Amiga → Pico)

**Amiga STROBE (5V) → Pico GP10 (3.3V safe)** via:

```
DB25 pin1 → 100Ω → node → GP10
                         ↓
                        10kΩ to 3.3V
                        20kΩ to GND
```

This divides 5V → ~3.3V.

---

# Pico PIO

* Outputs 8-bit sample on GP2–GP9
* Triggered by **external STROBE rising edge** (Amiga clock)
* PIO program: pull → out pins, 8 → loop

---

# Raspberry Pi DSP + SPI

The Pi applies:

* DC-block
* 14kHz FIR LPF
* Noise-shaped 8-bit quantization
* Optional dither (`--dither`)
* Downsample to **27,928 Hz** (ProTracker A-3)
* SPI burst of 32 bytes using `SPI_IOC_MESSAGE`
* Test tone mode:

```
./sampler --test-tone --tone-freq 1000
```

---

# Testing Steps

1. Power Pico + Pi
2. Run Pi in test-tone mode
3. Scope:
   * CH1 = SCLK
   * CH2 = MOSI
   * CH3 = CE0
   * CH4 = Pico D0 or STROBE
4. Confirm stable SPI bursts and clean 5V D0–D7 outputs
5. Connect to Amiga parallel port
6. Sample into ProTracker at A-3 (27.928kHz)
7. Enjoy fully digital audio

---

# Note

* Pi 4B or Pi 5B required when using HiFiBerry Digi+ (Pi 3A+ cannot run SPI and Digi together)
