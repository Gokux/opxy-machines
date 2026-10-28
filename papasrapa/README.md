# Papa Srapa

Two Schmitt-trigger oscillators after the drone voices 3 and 6 of the Elta
Music Solar 42N, which follow the DIY noise boxes of the artist Papa Srapa: a
square LFO modulates an audio square by frequency and amplitude, with noise, a
frequency divider and a sample-and-hold. Sirens, bird calls, space monsters,
volleys and surf. The sound is modelled from the instrument's published
description; no code or tables come from it. Read [engine.cpp](engine.cpp),
[core.h](core.h) and [display.cpp](display.cpp) alongside the
[engine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md).

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | Rate | Range |
| 2 | Mod | Mode |
| 3 | Divider | Octave |
| 4 | Noise | S H |

A press of any encoder switches all four to the other page
(`"press": "page"`). The screen draws its own header with both pages in
place: page 1 on top, page 2 under it. The page the encoders edit is the
stock header's size on its stepped bar; the other is smaller and grey, and a
switch grows and shrinks the two rows in about 0.15 s.

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

The screen is a transistor radio. Its tuning window shows the output wave
with Mod, Divider, Noise and S&H, and the tuning needle stands at the Divider.
The fm and am band buttons light with Mode, Rate turns the knob and blinks the
LFO lamp, Octave pulls the telescopic antenna out (Hi) or in (Low), and the
power LED flashes on each note start.

The machine is published in the Open-XY catalog; to preview it from a local
folder, see [Play in the browser](../README.md#play-in-the-browser).

```sh
python sdk.py test --machine local/opxy-machines/papasrapa
```

| File | Owns |
| --- | --- |
| [definition.json](definition.json) | Controls, presets, resources and the display shape |
| [core.h](core.h), [engine.cpp](engine.cpp) | Voice state, lifecycle and sound |
| [tables.h](tables.h) | LFO rates, shared by sound and screen |
| [display.h](display.h), [display.cpp](display.cpp), [panel.svg.fragment](panel.svg.fragment) | Transistor radio screen and the two-page header |
| [machine.json](machine.json) | SDK version, kind, preferred identity (engine 8, page 86) and a long default amp release |

A voice is 28 bytes; the definition allows 256 voices. Samples use Q15,
pitch ratios Q12 and smoothed AM gain Q19. The FM glide retains Q31 precision
so the slowest LFO setting continues to move. Oscillator phases remain 32-bit.

For device builds and enum control behavior, see the repository
[build instructions](../README.md#build-firmware).
