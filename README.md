# opxy-machines

Three drone synth machines for the OP-XY, written for the
[Open-XY SDK](https://github.com/etcetc000/open-xy).

| Machine | Sound | Screen |
| --- | --- | --- |
| [Drone](drone/README.md) | Detuned saw stack, sub, fifth and a drifting resonant filter | A quadcopter |
| [Solar](solar/README.md) | Five free-running negistor saws per voice, light modulation and cross-FM, after the drone voices of the Elta Music Solar 42N | A sunset |
| [Papa Srapa](papasrapa/README.md) | Two Schmitt-trigger oscillators: sirens, birds, monsters, volleys and surf, after the Solar's Papa Srapa voices | A megaphone |

All three target Open-XY SDK 1.0, with fixed-point C++ and greyscale
artwork in the stock screens' style. Each folder's README lists its controls,
presets and mappings.

> Experimental. Custom OP-XY firmware is unofficial; native code is not
> sandboxed and a build cannot guarantee recovery from a faulty image. Keep
> your stock `os.ldr` and `op1_factory.json`. Not affiliated with teenage
> engineering, Elta Music or Papa Srapa; the sounds are modelled from published
> descriptions.

## Setup

Clone this repository inside the SDK checkout's ignored `local/` folder:

```sh
git clone https://github.com/etcetc000/open-xy
git clone https://github.com/Gokux/opxy-machines open-xy/local/opxy-machines
cd open-xy
```

Then follow the SDK's [Check your tools](https://github.com/etcetc000/open-xy/blob/staging/docs/first-machine.md#check-your-tools).
Commands run from the SDK checkout; on macOS use `python3`, and add
`--cxx <clang++>` to `preview` if Clang is not on PATH.

## Play in the browser

```sh
python sdk.py preview --source local/opxy-machines/solar --out local/solar-preview-001
python tools/serve_harness.py --preview local/solar-preview-001
```

Open http://127.0.0.1:8917/, enable audio and play. Use
`local/opxy-machines/drone` or `local/opxy-machines/papasrapa` for the others,
and a fresh `--out` folder after each edit.

## Check the machines

```sh
python sdk.py test --machine local/opxy-machines/drone --machine local/opxy-machines/solar --machine local/opxy-machines/papasrapa
```

This validates metadata and artwork, builds previews, runs each machine's
sound and display tests and estimates CPU cost on the host. Add `--firmware` to compile
all three alongside the bundled machines with the pinned Blackfin tools.
The repository's CI runs both checks against a pinned SDK 1.0 commit, checks
sound arithmetic with undefined-behavior sanitization, and requires each
machine's CPU estimate to stay below 20,000 instructions per voice block.
These are host estimates; they do not measure device cycles or polyphony.

Audio uses Q15 samples internally and returns Q31 buffers. Oscillator phases
remain 32-bit. Drone keeps extra filter precision; Solar prepares gain and FM
ramps per block; Papa Srapa retains full-precision slow FM glide. These choices
trade some arithmetic precision for lower cost. The sound tests cover control
extremes and transitions, output bounds, retriggering, and preset levels
against the original implementation.

## Build firmware

Follow the SDK's [build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine)
to install the pinned toolchain and supply your own stock OS. From the SDK
checkout, pass the machine folders directly:

```sh
python sdk.py build --machine local/opxy-machines/drone --machine local/opxy-machines/solar --machine local/opxy-machines/papasrapa --stock-os <private>/stock-1.2.0/os.ldr --out <private>/builds/drones-001
```

Replace `<private>` with a folder outside every checkout, for example
`%LOCALAPPDATA%/OPXY-SDK/private` on Windows or
`~/Library/Application Support/OPXY-SDK/private` on macOS. Use a fresh output
folder for every build. Firmware, stock content and firmware-containing
packages must stay outside checkouts, including ignored `local/` folders.

The manifests retain the original identity requests: Drone engine 1 / page 84,
Solar 5 / 85, Papa Srapa 8 / 86. SDK 1.0 assigns new engines numbers above 33;
the package's `layout.json` records the actual assignments. Preserve a
previous build's assignments with
`--layout <previous-package>/layout.json`, since projects store those numbers.
Each machine includes `presets/default.json`, its device starting sound. The
SDK's repository-preset support installs this file under
`content/presets/<id>/default.json` in browser and command-line packages.
The other named patches in `definition.json` remain browser presets.

Solar's Chord/Mute and Papa Srapa's Range/Mode/Octave work with SDK 1.0 as
published. On the device their captions are stock numeric values and turning
an encoder moves through normalized values, so a choice spans several
detents; the browser shows enum labels. Automatic native labels and one-choice
stepping are tracked in [SDK issue #71](https://github.com/etcetc000/open-xy/issues/71).
That improvement is not required to build or play these machines. Drone has
no enum controls.

## License

MIT, © 2026 Gokux. See [LICENSE](LICENSE). The Open-XY SDK has its own license.
