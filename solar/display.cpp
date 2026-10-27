#include "display.h"
#include <opxy/q31.hpp>
#include "chords.h"

namespace solar {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// Node layout; artwork IDs solar_node_N follow it.
enum { corona = 0, sun = 1, truss = 2, lattice = 3, core = 4, modules = 5, radiators = 7, masts = 9,
       cells = 14, speckles = 54 };
const u32 speckle_count = 32;
const u32 levels = 4;                       // cells per wing side
const i32 settled = 4096;                   // flash age past its decay, Q10 seconds
const i32 sun_x = 66, sun_y = 66;           // pixels
const i32 station_x = 300;                  // pixels
const i32 wing_x[5] = {-96, -48, 0, 48, 96};
// Flicker 0..1 in 16 steps: light changes per second, Q10 (0.05..40 Hz, as the engine).
const i32 flicker_hz[17] = {51, 78, 118, 179, 272, 414, 628, 954, 1448, 2199, 3340, 5072, 7702, 11696, 17761, 26972, 40960};

inline i32 control(i32 word) { return clamp(word, 0, opxy::q31::maximum) >> 21; }  // Q10

// A thin ring round the sun, rippled six times; the ripple turns by `turn`
// degrees and deepens to `depth` (0..7) quarter pixels in brighter light.
// The quantized inputs form the key.
inline void corona_path(Frame::path_type& path, u32 turn, u32 depth) {
    const u32 key = 1 + turn + 360 * depth;
    if (path.key == key) return;
    for (u32 i = 0; i < Frame::point_count; ++i) {
        const i32 angle = static_cast<i32>(i * 3600 / Frame::point_count);
        const i32 r = 19 * unit + static_cast<i32>(2 + depth) * sine(static_cast<i32>((i * 6 * 3600 / Frame::point_count + turn * 10) % 3600)) / 4;
        path.points[i] = Point{floor_q10(r * sine(angle)), floor_q10(-r * sine(angle + 900))};
    }
    path.key = key;
}

// A part of the station at (dx, dy) pixels from its centre, turned with it.
struct Station {
    i32 x, y;        // centre, Q10 pixels
    Turn turn;
    void put(Frame& frame, u32 node, i32 dx, i32 dy, u32 shade, Paint paint) const {
        place(frame, node, x + dx * turn.cos - dy * turn.sin, y + dx * turn.sin + dy * turn.cos, unit, shade, paint);
        opxy::draw::turn(frame, node, turn);
    }
};
} // namespace

inline void display_reset(DisplayState& state) {
    state.phase = state.light_clock = state.previous_event = state.track = state.initialized = 0;
    state.seed = 1;
    state.light = state.light_target = 0;
    state.flash_age = settled;
}

// Only screen work. Each wing pair is an oscillator: Chord lights one to four
// cells each side, outward with pitch; Mute darkens the pair, and the wings
// flash on each note start. Volt lowers the orbit towards the Earth; past
// half the station shivers with the cross-FM. Drift rocks it slowly. Mod
// thickens the radiation noise over the picture and ripples the corona;
// Flicker makes the sun, the corona and the noise pulse.
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const i32* chord = solar_chords[static_cast<u32>(clamp(input.slots[0], 0, opxy::q31::maximum)) >> 28];
    const i32 volt = control(input.slots[1]);
    const i32 mod = control(input.slots[2]);
    const u32 mute = solar_mutes[static_cast<u32>(clamp(input.slots[3], 0, opxy::q31::maximum)) >> 28];
    const i32 drift = control(input.slots[4]);
    const i32 f = control(input.slots[5]);
    const i32 hz = flicker_hz[f >> 6] + (flicker_hz[(f >> 6) + 1] - flicker_hz[f >> 6]) * (f & 63) / 64;
    const i32 elapsed = input.reduced_motion ? 0 : clamp(input.elapsed, 0, 102);

    // A snapshot (first frame or new track) never fabricates a note.
    if (!state.initialized || state.track != input.track || input.reduced_motion) {
        state.flash_age = settled;
    } else {
        state.flash_age = clamp(state.flash_age + elapsed, 0, settled);
        if (input.track < 8 && input.note_serial != state.previous_event) state.flash_age = 0;
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // The light: a new random level each Flicker cycle, followed at that rate.
    state.phase += static_cast<u32>(elapsed) * 64;
    const u32 before = state.light_clock >> 16;
    state.light_clock += static_cast<u32>(elapsed * hz / 16);
    if (state.light_clock >> 16 != before) {
        state.seed = state.seed * 1664525u + 1013904223u;
        state.light_target = static_cast<i32>(state.seed >> 22) - 512;
    }
    state.light += (state.light_target - state.light) * clamp(elapsed * hz / 160, 0, 1024) / 1024;
    if (input.reduced_motion) state.light = 0;
    const i32 glow = mod ? (state.light + 512) * mod / 1024 : 0;   // 0..1024

    // The sun and its corona, rippling deeper in the brighter light.
    corona_path(frame.paths[0], (state.phase >> 14) % 360, static_cast<u32>(glow >> 7));
    place(frame, corona, sun_x * unit, sun_y * unit, unit, glow > 512 ? 6 : 5, stroke);
    place(frame, sun, sun_x * unit, sun_y * unit, unit, 8, fill_and_stroke);

    // Volt lowers the orbit 30 pixels over its first half; past half the
    // station shivers up to a pixel and a half. Drift rocks it up to 4
    // degrees either way.
    const i32 fm = volt > 512 ? (volt - 512) * 2 : 0;   // Q10
    const i32 noise = static_cast<i32>(((state.phase >> 11) * 2654435761u) >> 26) - 32;   // -32..31, every other frame
    const i32 shiver = noise * fm / 1024;   // -32..31
    const i32 rock = drift * 40 / 1024 * sine(static_cast<i32>((state.phase >> 9) % 3600)) / 1024;   // tenths
    Station station;
    station.x = station_x * unit + shiver * 48;
    station.y = (100 + (volt < 512 ? volt : 512) * 30 / 512) * unit - shiver * 32;
    station.turn = rotation(rock + shiver / 16);

    station.put(frame, truss, 0, 0, 0, keep);
    station.put(frame, lattice, 0, 0, 0, keep);
    for (u32 w = 0; w < 5; ++w) station.put(frame, masts + w, wing_x[w], 0, 0, keep);
    station.put(frame, core, 0, 0, 0, keep);
    station.put(frame, modules, -21, 0, 0, keep);
    station.put(frame, modules + 1, 21, 0, 0, keep);
    station.put(frame, radiators, -116, 0, 0, keep);
    station.put(frame, radiators + 1, 116, 0, 0, keep);
    const bool flash = state.flash_age < 384;
    for (u32 w = 0; w < 5; ++w) {
        const u32 lit = 1 + static_cast<u32>((chord[w] + 24) * 3 / 48);   // 1..4 cells each side
        const bool on = mute >> w & 1u;
        for (u32 side = 0; side < 2; ++side) {
            for (u32 l = 0; l < levels; ++l) {
                const u32 shade = !on ? 1 : l < lit ? (flash ? 8 : 7) : 3;
                const i32 dy = (11 + static_cast<i32>(l) * 8) * (side ? 1 : -1);
                station.put(frame, cells + w * 8 + side * levels + l, wing_x[w], dy, shade, fill_and_stroke);
            }
        }
    }

    // Radiation noise: speckles over the whole picture, like a camera sensor
    // in a radiation field. Mod sets how many, the light how many of those
    // and how bright; new ones every frame.
    const u32 hits = input.reduced_motion ? 0 : static_cast<u32>(mod * (768 + state.light / 2) / 1024 * 32 / 1024);
    u32 seed = state.phase * 2654435761u + 1;
    for (u32 k = 0; k < speckle_count; ++k) {
        seed = seed * 1664525u + 1013904223u;
        if (k >= hits) { hide(frame, speckles + k); continue; }
        const i32 x = static_cast<i32>((seed >> 8 & 0xffffu) * 480 >> 16);
        const i32 y = 22 + static_cast<i32>((seed >> 24) * 196 >> 8);
        const u32 shade = (seed & 7u) == 0 && glow > 384 ? 8 : 3 + (seed >> 3 & 3u);
        place(frame, speckles + k, x * unit, y * unit, unit, shade, fill_and_stroke);
    }

    // Chord and Mute by name wherever a position shows them; an empty header
    // keeps the stock value of the continuous controls.
    for (u32 i = 0; i < opxy::page_controls; ++i) {
        const u32 slot = input.shown[i] < opxy::parameter_slots ? input.shown[i] : i;
        const u32 choice = static_cast<u32>(clamp(input.slots[slot], 0, opxy::q31::maximum)) >> 28;
        opxy::draw::header(frame, i, slot == 0 ? solar_chord_labels[choice] : slot == 3 ? solar_mute_labels[choice] : "");
    }
}
}
