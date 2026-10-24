# opxy-machines

Three drone synth machines for the OP-XY, written for the
[Open-XY SDK](https://github.com/etcetc000/open-xy).

| Machine | Sound | Screen |
| --- | --- | --- |
| [Drone](drone/README.md) | Detuned saw stack, sub, fifth and a drifting resonant filter | A quadcopter |
| [Solar](solar/README.md) | Five free-running negistor saws per voice, light modulation and cross-FM, after the drone voices of the Elta Music Solar 42N | A sunset |
| [Papa Srapa](papasrapa/README.md) | Two Schmitt-trigger oscillators: sirens, birds, monsters, volleys and surf, after the Solar's Papa Srapa voices | A megaphone |

All three are fixed-point C++ in the SDK's machine format, with greyscale
artwork in the stock screens' style. Each folder's README lists its controls,
presets and mappings.

> Experimental. Custom OP-XY firmware is unofficial; native code is not
> sandboxed and a build cannot guarantee recovery from a faulty image. Keep
> your stock `os.ldr` and `op1_factory.json`. Not affiliated with teenage
> engineering, Elta Music or Papa Srapa; the sounds are modelled from published
> descriptions.

## Setup

Clone this repository as the SDK checkout's ignored `local/` folder:

```sh
git clone https://github.com/etcetc000/open-xy
git clone https://github.com/Gokux/opxy-machines open-xy/local
cd open-xy
```

Then follow the SDK's [Check your tools](https://github.com/etcetc000/open-xy/blob/staging/docs/first-machine.md#check-your-tools).
Commands run from the SDK checkout; on macOS use `python3`, and add
`--cxx <clang++>` to `preview` if Clang is not on PATH.

## Play in the browser

```sh
python sdk.py preview --source local/solar --out local/solar-preview-001
python tools/serve_harness.py --preview local/solar-preview-001
```

Open http://127.0.0.1:8917/, enable audio and play. Use `local/drone` or
`local/papasrapa` for the others, and a fresh `--out` folder after each edit.

## Put them on the device

Follow the SDK's [build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine):
move a machine's folder to `machines/<id>/`, rename every `local_` to `<id>_`
in its `panel.svg.fragment` and add `<id>` to `profiles/collection.json`, then
build with your own stock files. The identities are free in the SDK as
published: Drone engine 1 / page 84, Solar 5 / 85, Papa Srapa 8 / 86. Named
presets live in each `definition.json`; command-line builds do not install
them as device presets.

## License

MIT, © 2026 Gokux. See [LICENSE](LICENSE). The Open-XY SDK has its own license.
