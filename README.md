# Amiga Digital Sampler

S/PDIF to parallel-port sampler interface for Amiga.

## Overview

This project provides fully **digital** audio sampling for the Amiga using:
- Raspberry Pi for S/PDIF capture and DSP
- Raspberry Pi Pico for cycle-accurate parallel output
- Amiga sampling software (e.g., ProTracker)

The result is a modern, clean, high-quality digital sampler that plugs directly into the Amiga’s parallel port.

## Status

**1.2 – New DSP engine with selectable shaping, filtering, compression, saturation, and dither**

The sampler supports multiple DSP modes suitable for different content types:

- **Raw / punchy**: filter off, shaping off  
- **Clean / pads**: shaping on (auto-filter on)
- **Bright transients / snares**: filter off, shaping off
- **Lo-fi / crunchy**: shaping on, filter off (intentional aliasing)  

## Motivation

Traditional analog Amiga parallel-port samplers are noisy, have DC offsets, and dynamics issues.
Rather than fight the hardware, this project implements a **fully digital** sampler:

S/PDIF → Raspberry Pi → DSP → Pico → Amiga DB25

No noise, no DC offsets, and precise sample rate control.

## Hardware

### Required Components

- Raspberry Pi 3A+ or 4  
- HiFiBerry Digi+ I/O  
- Raspberry Pi Pico  
- 74HCT245 octal buffer  
- 74LVX14 Schmitt trigger inverter  
- DB25 male connector  
- 0.1µF ceramic capacitors (3×)  
  - Rails
  - 74HCT245 decoupling  
  - 74LVX14 decoupling

### Connections

#### SPI (Pi → Pico)
```
Pi GPIO 10 (MOSI)  →  Pico GP16
Pi GPIO 11 (SCLK)  →  Pico GP18
Pi GPIO 8  (CE0)   →  Pico GP17
GND                →  GND
```

#### STROBE (from Amiga → Pico via 74LVX14)
```
74LVX14 Pin 1  IN   →  DB25 Pin 1 (STROBE)
74LVX14 Pin 2  OUT  →  Pico GP10
Pin 14 → 3.3V
Pin 7  → GND
0.1µF across VCC/GND
```

#### Data Buffer (Pico → Amiga via 74HCT245)
```
74HCT245 Pins 2–9   ←  Pico GP2–9
74HCT245 Pins 11–18 →  DB25 Pins 2–9
DIR → 5V
OE  → Pico GP11
VCC → 5V, GND → GND
0.1µF across VCC/GND
````

**IMPORTANT:** All devices including the Amiga must share ground.

## Software

### Raspberry Pi

```bash
cd pi
make
./sampler            # default raw mode
./sampler --shape    # shaped, filtered, clean mode
./sampler --filter   # enable LPF
./sampler --shape --no-filter # shape, but disable LPF
./sampler --compress --saturate
./sampler --test-tone --tone-freq 440
````

### DSP Options

```
--filter       Enable 14kHz anti-alias LPF (pre + post)
--shape        Enable 2nd/3rd-order noise shaping
                (auto-enables filter unless explicitly disabled with --no-filter)
--dither       Enable HP-TPDF dither in oversample quantizer
--compress     Gentle envelope-based compression
--saturate     Soft saturation
--gain X       Input gain (default 1.0)
--rate Hz      Target sample rate (default 28149.96)
--test-tone    Generate sine wave
--tone-freq    Frequency of sine (default 1000 Hz)
--test-ramp    Generate 0..255 ramp
```

### Pico

```bash
cd pico
mkdir build && cd build
cmake ..
make
# Copy pico_amiga_sampler.uf2 to the Pico in BOOTSEL mode
```

## ProTracker Setup

1. Set sampling note to **A-3**
2. After sampling, set **Finetune +1** for exact pitch

This matches the Amiga's 28149.96 kHz sampling mode for PT A-3.

## Technical Details

### DSP Signal Path (with options)

```
48kHz S/PDIF input
↓
DC-block (always)
↓
Pre-FIR LPF (optional: --filter)
↓
Compressor (optional: --compress)
↓
Saturator (optional: --saturate)
↓
Oversample quantizer at 48kHz (always)
  • shaping (optional: --shape)
  • HP-TPDF dither (optional: --dither)
↓
Post-FIR LPF (optional: --filter)
↓
Decimation to ~28.15kHz (always)
↓
Final 8-bit quantizer (always)
  • gentle shaping (optional: --shape)
↓
SPI burst output → Pico
↓
PIO-driven parallel bus → Amiga
```

### Notes on DSP Behavior

* **Oversampling is always active internally**, ensuring stable decimation
* **Shaping automatically enables filtering** unless you explicitly use `--no-filter`
* Disabling filters is ideal for snares/kicks
* Enabling shaping + filtering is ideal for pads/melodic sounds
* Shaping without filtering produces aliasing (lo-fi mode)

### Timing

* Amiga STROBE ≈ **28149.96 Hz**
* Pico latches the next sample exactly on each STROBE edge
* Pi → Pico SPI transfer uses small bursts to minimize latency
* 8KB ringbuffer smooths jitter

## Warning

Be careful, don't break your Amiga! Check wiring twice. Ensure common ground. Only drive DB25 **D0–D7** pins — nothing else.

## Acknowledgements

* Thanks to **echolevel** for Open Amiga Sampler
* Thanks to **8bitbubsy** for help debugging ProTracker timing

## License

MIT

