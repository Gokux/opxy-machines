# Papa Srapa

Two Schmitt-trigger oscillators after the drone voices 3 and 6 of the Elta
Music Solar 42N, which follow the DIY noise boxes of the artist Papa Srapa: a
square LFO modulates an audio square by frequency and amplitude, with noise, a
frequency divider and a sample-and-hold. Sirens, bird calls, space monsters,
volleys and surf. The sound is modelled from the instrument's published
description; no code or tables come from it. Read [engine.cpp](engine.cpp),
[core.h](core.h) and [display.cpp](display.cpp) alongside the
[engine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md).

| Encoder | Control | Alternate (press) |
| --- | --- | --- |
| 1 | Rate | Range |
| 2 | Mod | Mode |
| 3 | Divider | Octave |
| 4 | Noise | S H |

| Control | Mapping |
| --- | --- |
| Rate | LFO `0.1 * 200^value` Hz (0.1..20 Hz) |
| Mod | Depth of whichever modulation Mode enables: FM raises the tone up to 4×, AM chops the output down to silence at full |
| Divider | 1..16: a copy of the tone divided by that many cycles, mixed half and half with it; 1 is the tone alone |
| Noise | White noise, up to 0.44 of full scale before the AM |
| Range | x1, or x10 for an LFO up to 200 Hz |
| Mode | The fm and am switches: Drone, FM, AM, Both |
| Octave | Hi, or Low for two octaves down |
| S H | Each rising LFO edge samples noise; the held value moves the tone 0.25×..1.75× at full |

The LFO square charges the tone's timing capacitor rather than switching its
pitch: FM glides with a time constant of `0.15 / rate`, so slow rates swoop like
sirens and fast ones warble. The tone is a PolyBLEP band-limited square that
stops rising at 11 kHz. AM edges take about 0.3 ms. A cubic soft clip limits the
output to a quarter of full scale (-12 dB). The source's divider control is not
described in detail; this port treats it as the frequency divider common in
such boxes.

Presets: **Siren**, **Bird**, **Monster**, **Volley** and **Surf** (the
manual's clean-noise recipe with slow AM).

The screen is a megaphone. Sound arcs leave the horn, rippling with FM,
blinking with AM and thickening with the Divider. Noise sprays specks, the lamp
blinks with the LFO, bars show the held S&H steps, two LEDs show the fm and am
switches, and the horn flashes on each note start.

```sh
python sdk.py preview --source local/opxy-machines/papasrapa --out local/papasrapa-preview-001
python tools/serve_harness.py --preview local/papasrapa-preview-001
python sdk.py test --machine local/opxy-machines/papasrapa
```

| File | Owns |
| --- | --- |
| [definition.json](definition.json) | Controls, presets, resources and the display shape |
| [core.h](core.h), [engine.cpp](engine.cpp) | Voice state, lifecycle and sound |
| [tables.h](tables.h) | LFO rates, shared by sound and screen |
| [display.h](display.h), [display.cpp](display.cpp), [panel.svg.fragment](panel.svg.fragment) | Megaphone screen |
| [machine.json](machine.json) | SDK version, kind, preferred identity (engine 8, page 86) and a long default amp release |

A voice is 28 bytes; the definition allows 256 voices. Arithmetic is 32-bit
fixed point; the device links no 64-bit multiply.

For device builds and enum control behavior, see the repository
[build instructions](../README.md#build-firmware).
