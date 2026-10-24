# Drone

A detuned saw stack for slow, evolving drones: three band-limited saws, a sine
sub-octave and an optional fifth through a resonant state-variable lowpass,
moved by three slow LFOs. Read [engine.cpp](engine.cpp), [core.h](core.h) and
[display.cpp](display.cpp) alongside the [engine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md).

| Encoder | Control | Alternate (press) |
| --- | --- | --- |
| 1 | Spread | – |
| 2 | Tone | Reso |
| 3 | Motion | Rate |
| 4 | Sub | Fifth |

| Control | Mapping |
| --- | --- |
| Spread | The two side saws detune by `25 * value^2` cents either side of the centre saw |
| Tone | Lowpass cutoff `40 * 150^value` Hz for a 110 Hz note (40 Hz..6 kHz), scaled with the note's pitch |
| Motion | An LFO sweeps the Tone position by up to ±0.3 (about two octaves); two more drift the side saws by up to half their detune plus 2 cents |
| Sub | Sine one octave below, up to 0.28 of full scale |
| Reso | Filter damping from 1.4 down to 0.1 (Q 0.7..10) |
| Rate | Sweep LFO `0.02 * 100^value` Hz (0.02..2 Hz); the drifts run at 3/4 and 11/8 of it |
| Fifth | A fourth saw a fifth above, up to the level of the others |

The saws use PolyBLEP band-limiting. The filter is a Chamberlin state-variable
lowpass, stable for every cutoff and damping it receives (f² + 2fq < 4), with
saturating state so full resonance stays bounded. Each voice starts its LFOs at
its own phase, so held chords drift apart. A cubic soft clip limits the output
to a quarter of full scale (-12 dB), the declared output peak. Cutoff,
resonance, Sub and Fifth smooth per sample.

Presets: **Hum**, **Choir**, **Abyss**, **Storm** and **Pipe** (full resonance
and fifth, the loudest of them).

The screen is a quadcopter seen from above. Tone spins the rotors (0.5 to 4
turns per second, neighbours in opposite directions), Spread pulls them out of
step as it detunes the saws, Motion sets the hover, Sub weighs the drone down
towards its shadow, and the status LED flashes on each note start.

```sh
python sdk.py preview --engine drone --out local/drone-preview-001
python tools/serve_harness.py --preview local/drone-preview-001
python sdk.py check
```

| File | Owns |
| --- | --- |
| [definition.json](definition.json) | Controls, presets, resources and the display shape |
| [core.h](core.h), [engine.cpp](engine.cpp) | Voice state, lifecycle and sound |
| [tables.h](tables.h) | Sine, cutoff and LFO-rate tables |
| [display.h](display.h), [display.cpp](display.cpp), [panel.svg.fragment](panel.svg.fragment) | Quadcopter screen |
| [machine.json](machine.json) | Kind, identity (engine 1, page 84) and a long default amp release |

A voice is 60 bytes; the definition allows 256 voices. Arithmetic is 32-bit
fixed point apart from 64-bit additions in the filter's saturation; the device
links no 64-bit multiply. See [native builds](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md) to put it on
the device.
