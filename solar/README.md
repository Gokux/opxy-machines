# Solar

Five free-running oscillators per voice after the drone voices of the Elta
Music Solar 42N: two low, one middle and two high negistor sawtooths that drift
like the single-transistor originals, bend with a light sensor and modulate each
other past half Volt. The sound is modelled from the instrument's published
description; no code or tables come from it. Read [engine.cpp](engine.cpp),
[core.h](core.h) and [display.cpp](display.cpp) alongside the
[engine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md).

| Encoder | Control | Alternate (press) |
| --- | --- | --- |
| 1 | Chord | Drift |
| 2 | Volt | – |
| 3 | Mod | Flicker |
| 4 | Mute | – |

| Control | Mapping |
| --- | --- |
| Chord | Eight voicings in semitones from the played note: Oct, Fifth, Minor, Major, Sus, Trit, Clust, Stack ([chords.h](chords.h)) |
| Volt | First half: all five transpose down by `2^(-2 * value)`, an octave at 50. Second half: each oscillator is frequency-modulated by its neighbour (the first by the last), up to depth 0.7 |
| Mod | A random light level changes each oscillator's pitch ratio by up to ±0.5 times its own sensitivity (0.35, 1, 0.6, 0.9, 0.45), like half-pressed MOD buttons |
| Mute | Which of the five oscillators sound: 12345, 123, 345, 135, 24, 12, 45, 3 |
| Drift | Each oscillator wanders to a new pitch about every 1.5 s, up to ±26 cents at full |
| Flicker | The light changes `0.05 * 800^value` times per second (0.05..40 Hz): from clouds to a flickering tube |

The waveform is a negistor relaxation oscillator: an exponential charge
`1 - e^(-2.5t)` with an abrupt reset, DC removed and PolyBLEP-corrected at the
reset. Every voice has its own fixed component tolerance of about ±7 cents per
oscillator and starts its oscillators at random phases, as free-running
hardware would. Unlike the original, the voicing follows the played note.
Pitch is read once per 64-sample block; an oscillator above about 16.5 kHz
falls silent. Five oscillators at 0.18 each pass a cubic soft clip limiting the
output to a quarter of full scale (-12 dB). Mute changes and FM depth smooth per
sample through block-prepared linear ramps. Solar has no filter of its own: the device's track filter and effects
follow it.

Presets: **Classic**, **Dusk**, **Neon** and **Eclipse**.

The screen is a sunset. Volt lowers the sun behind the horizon and, past half,
roughens its corona and bends the waveform as the oscillators cross-modulate.
Mod and Flicker make the corona flicker. The five oscillators are status LEDs on
a pitch staff: Chord places them, Mute dims them, Drift sways them, and they
flash on each note start.

```sh
python sdk.py preview --source local/opxy-machines/solar --out local/solar-preview-001
python tools/serve_harness.py --preview local/solar-preview-001
python sdk.py test --machine local/opxy-machines/solar
```

| File | Owns |
| --- | --- |
| [definition.json](definition.json) | Controls, presets, resources and the display shape |
| [core.h](core.h), [engine.cpp](engine.cpp) | Voice state, lifecycle and sound |
| [chords.h](chords.h) | Voicings and mute patterns, shared by sound and screen |
| [tables.h](tables.h) | Negistor curve, semitone, Volt and Flicker tables |
| [display.h](display.h), [display.cpp](display.cpp), [panel.svg.fragment](panel.svg.fragment) | Sunset screen |
| [machine.json](machine.json) | SDK version, kind, preferred identity (engine 5, page 85) and a long default amp release |

A voice is 144 bytes; the definition allows 256 voices. Samples and
cross-modulation use Q15, gains and FM ramps use Q23, and oscillator phases
remain 32-bit. The five oscillator calls preserve their modulation order.

For device builds and enum control behavior, see the repository
[build instructions](../README.md#build-firmware).
