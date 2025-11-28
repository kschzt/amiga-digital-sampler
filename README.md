# Amiga Digital Sampler

S/PDIF to parallel port sampler interface for Amiga.

## Overview

Enables fully digital audio input to Amiga sampler software via Raspberry Pi and Pico.

## Status

1.1 - Sounds great with improved DSP (see below)

## Motivation

Traditional analog Amiga parallel-port samplers are noisy, have DC offsets and dynamics issues. I grew frustrated trying to make one sound clean, so I decided to build a modern digital one instead. This provides a fully digital path from S/PDIF to the parallel port and into eg. ProTracker. 

## Hardware

### Required Components
- Raspberry Pi 3A+ or 4
- HiFiBerry Digi+ I/O
- Raspberry Pi Pico
- 74HCT245 octal buffer
- 74LVX14 Schmitt trigger inverter
- DB25 male connector
- 0.1µF ceramic capacitors (3x)
  - 1x across breadboard power rails
  - 1x for 74HCT245 decoupling
  - 1x for 74LVX14 decoupling

### Connections

#### SPI (Pi → Pico)
```
Pi GPIO 10 (MOSI)  →  Pico GP16
Pi GPIO 11 (SCLK)  →  Pico GP18
Pi GPIO 8  (CE0)   →  Pico GP17
GND                →  GND
```

#### STROBE Signal (74LVX14)
```
74LVX14 Pin 14 (VCC)  →  3.3V
74LVX14 Pin 7  (GND)  →  GND
74LVX14 Pin 1  (IN)   →  DB25 Pin 1 (STROBE)
74LVX14 Pin 2  (OUT)  →  Pico GP10
0.1µF capacitor between pins 14 and 7
```

#### Data Buffer (74HCT245)
```
74HCT245 Pin 20 (VCC)  →  5V
74HCT245 Pin 10 (GND)  →  GND
74HCT245 Pin 1  (DIR)  →  5V
74HCT245 Pin 19 (OE)   →  Pico GP11
0.1µF capacitor between pins 20 and 10

Pico GP2-9 →  74HCT245 Pins 2-9 (A1-A8)

74HCT245 Pins 18-11 (B1-B8)  →  DB25 Pins 2-9

GND → DB25 Pins 18-25 (any or all)
```

**Make sure all devices including Amiga share ground**.

## Software

### Raspberry Pi
```bash
cd pi
make
./sampler  # or with --test-tone or --test-ramp
```

### Pico
```bash
cd pico
mkdir build && cd build
cmake ..
make
# Copy pico_amiga_sampler.uf2 to Pico in BOOTSEL mode
```

## ProTracker Setup

1. Set sample rate to A-3
2. After sampling, set finetune to +1 for correct playback

## Technical Details

### Signal Path
- S/PDIF input at 48kHz
- DC blocking filter
- Minimum-phase FIR lowpass (14kHz cutoff)
- Optional soft compression and saturation
- 3rd-order noise-shaped quantization at 48kHz (pushes noise to 16-24kHz)
- Post-quantization FIR lowpass (14kHz cutoff, removes shaped HF noise)
- Decimation to 28.15kHz
- 2nd-order noise-shaped final quantization to 8-bit
- SPI transfer to Pico
- STROBE-synchronized parallel output

### Timing
- On Amiga STROBE at approx. 28150Hz, we post the next sample onto the DB25 pins (like other samplers)
- Pico PIO handles cycle-accurate output
- 8KB ring buffer for jitter absorption

## Warning

Be careful, please don't break your Amiga!
Please make sure all the components (including Amiga) share ground, and **make sure** your wiring doesn't send current into the wrong place.
The only pins we should send current to are D0-D7 on the DB25. 

## Acknowledgements

- Thanks to echolevel for the awesome Open Amiga Sampler, which made this project possible.
- Thanks to 8bitbubsy for helping to debug ProTracker sample rates.

## License

MIT
