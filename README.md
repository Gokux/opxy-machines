# opxy-machines

Three drone synth machines for the OP-XY, written for the
[Open-XY SDK](https://github.com/etcetc000/open-xy).

| Machine | Sound | Screen |
| --- | --- | --- |
| [Drone](drone/README.md) | Detuned saw stack, sub, fifth and a drifting resonant filter | A quadcopter |
| [Solar](solar/README.md) | Five free-running negistor saws per voice, light modulation and cross-FM, after the drone voices of the Elta Music Solar 42N | A space station in the sun |
| [Papa Srapa](papasrapa/README.md) | Two Schmitt-trigger oscillators: sirens, birds, monsters, volleys and surf, after the Solar's Papa Srapa voices | A transistor radio |

All three target Open-XY SDK 1.3 on OP-XY OS 1.2.1, with fixed-point C++ and greyscale
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

The three machines are published in the Open-XY catalog, whose IDs
`sdk.py preview` keeps for the catalog: it refuses to preview `drone`, `solar`
or `papasrapa` from a local folder. To audition one, copy its folder, give the
copy another `id` in `definition.json` and the same prefix in
`panel.svg.fragment`, then preview the copy:

```sh
python sdk.py preview --source <copy> --out local/solar-preview-001
python tools/serve_harness.py --preview local/solar-preview-001
```

Open http://127.0.0.1:8917/, enable audio and play. Use a fresh `--out`
folder after each edit.

## Check the machines

```sh
python sdk.py test --machine local/opxy-machines/drone --machine local/opxy-machines/solar --machine local/opxy-machines/papasrapa
```

This validates metadata and artwork, builds previews, runs each machine's
sound and display tests and estimates CPU cost on the host. Add `--firmware` to compile
all three alongside the bundled machines with the pinned Blackfin tools.
The repository's CI runs both checks against a pinned SDK commit, checks
sound arithmetic with undefined-behavior sanitization, and requires each
machine's CPU estimate to stay below 20,000 instructions per voice block.
These are host estimates and vary between runs; they do not measure device
cycles or polyphony. The catalog entries carry hardware measurements.

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
python sdk.py build --machine local/opxy-machines/drone --machine local/opxy-machines/solar --machine local/opxy-machines/papasrapa --stock-os <private>/stock-1.2.1/os.ldr --out <private>/builds/drones-001
```

Replace `<private>` with a folder outside every checkout, for example
`%LOCALAPPDATA%/OPXY-SDK/private` on Windows or
`~/Library/Application Support/OPXY-SDK/private` on macOS. Use a fresh output
folder for every build. Firmware, stock content and firmware-containing
packages must stay outside checkouts, including ignored `local/` folders.

The manifests request Drone engine 36 / page 84, Solar 5 / 85 and Papa Srapa
8 / 86. Drone asked for engine 1 before OS 1.2.1, where Ring Mod's fixed
effect 1 now holds that number too. A build gives a new machine its request
when free, else numbers above 33; the package's `layout.json` records the
actual assignments. Preserve a
previous build's assignments with
`--layout <previous-package>/layout.json`, since projects store those numbers.
Each machine ships its presets in `presets/`, which `machine.json` names:
`default.json`, its device starting sound, and one file per named patch. The
Open-XY builder installs them as `content/presets/<id>/<name>.json`;
`sdk.py build` packages leave them out, so copy the folder there yourself.

Solar's Chord/Mute and Papa Srapa's Range/Mode/Octave show their choice names
in the device header: each display writes them with `opxy::draw::header`
wherever a position shows one of them, including after an encoder press
selects an alternate. Turning an encoder still moves through normalized
values, so a choice spans several detents; one-choice stepping is tracked in
[SDK issue #71](https://github.com/etcetc000/open-xy/issues/71). Drone has no
enum controls.

## License

MIT, © 2026 Gokux. See [LICENSE](LICENSE). The Open-XY SDK has its own license.
