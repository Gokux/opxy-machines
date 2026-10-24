#include "display.h"
#include <opxy/q31.hpp>
#include "chords.h"
#include "tables.h"

namespace solar {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// Node layout; artwork IDs solar_node_N follow it.
enum { corona = 0, wave = 1, sun = 2, leds = 3 };
const i32 settled = 4096;   // flash age past its decay, Q10 seconds
// sin(2 pi i / 64), Q10.
const i32 ring[64] = {
    0, 100, 200, 297, 392, 483, 569, 650, 724, 792, 851, 903, 946, 980, 1004, 1019,
    1024, 1019, 1004, 980, 946, 903, 851, 792, 724, 650, 569, 483, 392, 297, 200, 100,
    0, -100, -200, -297, -392, -483, -569, -650, -724, -792, -851, -903, -946, -980, -1004, -1019,
    -1024, -1019, -1004, -980, -946, -903, -851, -792, -724, -650, -569, -483, -392, -297, -200, -100,
};
// Flicker 0..1 in 16 steps: light changes per second, Q10 (0.05..40 Hz, as the engine).
const i32 flicker_hz[17] = {51, 78, 118, 179, 272, 414, 628, 954, 1448, 2199, 3340, 5072, 7702, 11696, 17761, 26972, 40960};

inline i32 control(i32 word) { return clamp(word, 0, opxy::q31::maximum) >> 21; }  // Q10

inline i32 curve(u32 index) {   // the engine's negistor curve, Q10
    const i32 s = solar_charge[index & 255];
    return s / 2097152 - (s < 0 && s % 2097152 != 0);
}

// A star of 32 rays: spikes sets their length, jitter roughens them with the
// seed. turn, spikes, jitter and seed are quantized and form the key.
inline void corona_path(Frame::path_type& path, u32 turn, u32 spikes, u32 jitter, u32 seed) {
    const u32 key = 1 + turn + 64 * (spikes + 32 * (jitter + 16 * seed));
    if (path.key == key) return;
    for (u32 i = 0; i < Frame::point_count; ++i) {
        const i32 rough = static_cast<i32>(((i * 2654435761u + seed * 40503u) >> 24) & 15) - 8;
        const i32 r = (34 + (i & 1 ? 0 : static_cast<i32>(spikes))) * unit + rough * static_cast<i32>(jitter) * 128;
        const u32 a = (i + turn) & 63;
        path.points[i].x = floor_q10(r * ring[(a + 16) & 63]);
        path.points[i].y = floor_q10(r * ring[a]);
    }
    path.key = key;
}

// Negistor periods (in sixteenths) along a 120-pixel baseline, 65536 per
// cycle; Volt slows them, FM bends them.
inline void wave_path(Frame::path_type& path, u32 periods, u32 bend, u32 scroll, u32 time) {
    const u32 key = 1 + scroll + 64 * (periods + 64 * (bend + 8 * time));
    if (path.key == key) return;
    for (u32 i = 0; i < Frame::point_count; ++i) {
        const u32 cycles = i * periods * 4096 / 63 + scroll * 1024
            + static_cast<u32>(static_cast<i32>(bend) * ring[(i * 5 + time) & 63] * 2);
        const i32 y = (i == 0 || i + 1 == Frame::point_count) ? 0 : -12 * curve(cycles >> 8);
        path.points[i].x = (static_cast<i32>(i) * 120 / 63 - 60) * unit;
        path.points[i].y = y;
    }
    path.key = key;
}
} // namespace

inline void display_reset(DisplayState& state) {
    state.phase = state.light_clock = state.previous_event = state.track = state.initialized = 0;
    state.seed = 1;
    state.light = state.light_target = 0;
    state.flash_age = settled;
}

// Only screen work. Volt sets the sun and, past half, roughens the corona and
// bends the wave as the oscillators cross-modulate. Mod and Flicker drive the
// light; Chord places the LEDs on the staff, Mute dims them, Drift sways them.
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

    const i32 fm = volt > 512 ? (volt - 512) * 2 : 0;   // Q10
    const i32 sun_y = volt < 512 ? 84 + volt * 66 / 512 : 150 + (volt - 512) * 36 / 512;
    const u32 spikes = static_cast<u32>(3 + ((mod * 12 * (state.light + 512)) >> 20));
    const u32 jitter = static_cast<u32>(fm >> 7);
    const u32 time = (state.phase >> 14) & 63;
    corona_path(frame.paths[0], (state.phase >> 16) & 63, spikes, jitter, jitter ? (state.phase >> 12) & 255 : 0);
    place(frame, corona, 340 * unit, sun_y * unit, unit, 6, stroke);
    place(frame, sun, 340 * unit, sun_y * unit, unit, state.light > 0 && mod > 0 ? 8 : 7, fill_and_stroke);

    const u32 periods = static_cast<u32>(volt < 512 ? 48 - volt * 24 / 512 : 24);   // sixteenths
    wave_path(frame.paths[1], periods, static_cast<u32>(fm >> 7),
              (state.phase >> 13) & 63, fm ? time : 0);
    place(frame, wave, 124 * unit, 196 * unit, unit, 7, stroke);

    const i32 flash = state.flash_age < 384 ? (384 - state.flash_age) : 0;
    for (u32 n = 0; n < 5; ++n) {
        // Drift sways each LED on its own slow cycle, up to 3 pixels.
        const i32 sway = drift * 3 * ring[((state.phase >> 12) + n * 13) & 63] / 1024;
        const i32 y = (118 - chord[n] * 2) * unit + sway;
        const bool lit = mute >> n & 1u;
        place(frame, leds + n, (52 + static_cast<i32>(n) * 36) * unit, y,
              unit + (lit ? flash : 0), lit ? 8 : 2, fill_and_stroke);
    }
}
}
