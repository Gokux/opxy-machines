#include "display.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace papasrapa {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// Node layout; artwork IDs papasrapa_node_N follow it.
enum { arcs = 0, bars = 3, horn = 4, fm_led = 5, am_led = 6, specks = 7, lamp = 19 };
const u32 speck_count = 12;
const i32 settled = 4096;     // flash age past its decay, Q10 seconds
const i32 blur_hz = 12 * 1024; // above this the lamp and arcs stop visibly blinking
// sin(2 pi i / 16), Q10: the FM ripple along an arc.
const i32 ripple[16] = {0, 392, 724, 946, 1024, 946, 724, 392, 0, -392, -724, -946, -1024, -946, -724, -392};

inline i32 control(i32 word) { return clamp(word, 0, opxy::q31::maximum) >> 21; }  // Q10

// A crescent of radius pixels over +-40 degrees: 32 points out along the outer
// edge, 32 back along the inner edge, thick pixels apart. wiggle ripples it
// with the FM, more as it spreads; every input is quantized and forms the key.
inline void arc(Frame::path_type& path, u32 radius, u32 wiggle, u32 thick, u32 time) {
    const u32 key = 1 + radius + 64 * (wiggle + 16 * (thick + 16 * time));
    if (path.key == key) return;
    for (u32 j = 0; j < 32; ++j) {
        // Small-angle sine and cosine, Q10, for -0.7..0.7 radians.
        const i32 angle = -717 + static_cast<i32>(j) * 1434 / 31;
        const i32 square = angle * angle / 1024;
        const i32 s = angle - angle * square / 6144;
        const i32 c = 1024 - square / 2 + square * square / 24576;
        const i32 outer = static_cast<i32>(radius) * unit
            + static_cast<i32>(wiggle * radius) * ripple[(j + time) & 15] / 52;
        const i32 inner = outer - static_cast<i32>(thick) * unit;
        path.points[j].x = floor_q10(outer * c);
        path.points[j].y = floor_q10(outer * s);
        path.points[63 - j].x = floor_q10(inner * c);
        path.points[63 - j].y = floor_q10(inner * s);
    }
    path.key = key;
}

inline u32 height(const DisplayState& state, u32 k) { return state.bars[k >> 2] >> (8 * (k & 3)) & 255u; }

// Sixteen sample-and-hold bars on a 112-pixel baseline, at most 30 pixels tall.
inline void bar_path(Frame::path_type& path, const DisplayState& state, u32 cycle, u32 scale) {
    const u32 key = 1 + (cycle & 0xffffu) + 65536 * scale;
    if (path.key == key) return;
    for (u32 k = 0; k < 16; ++k) {
        const i32 left = (-56 + static_cast<i32>(k) * 7) * unit;
        const i32 top = -static_cast<i32>(height(state, k) * scale * 4);   // Q10: scale/255 of 30 px
        path.points[k * 4] = Point{left, 0};
        path.points[k * 4 + 1] = Point{left, top};
        path.points[k * 4 + 2] = Point{left + 7 * unit, top};
        path.points[k * 4 + 3] = Point{left + 7 * unit, 0};
    }
    path.key = key;
}
} // namespace

inline void display_reset(DisplayState& state) {
    state.phase = state.lfo_phase = state.previous_event = state.track = state.initialized = 0;
    state.seed = 1;
    state.flash_age = settled;
    for (u32 k = 0; k < 4; ++k) state.bars[k] = 0;
}

// Only screen work. Rate (and Range) runs the LFO lamp and the arcs, Mode
// lights the fm and am switches, Mod ripples (FM) or blinks (AM) the arcs,
// Divider thickens them, Noise sprays specks and S&H scales the held bars.
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const i32 rate = control(input.slots[0]);
    const i32 mod = control(input.slots[1]);
    const u32 divider = 1 + static_cast<u32>(control(input.slots[2]) * 16 / 1024);
    const i32 noise = control(input.slots[3]);
    const bool ten = input.slots[4] >= 0x40000000;
    const u32 mode = static_cast<u32>(clamp(input.slots[5], 0, opxy::q31::maximum)) >> 29;
    const bool fm = mode & 1u, am = (mode & 2u) != 0;
    const i32 sample_hold = control(input.slots[7]);
    const i32 hz = (papasrapa_rate_hz[rate >> 6] + (papasrapa_rate_hz[(rate >> 6) + 1]
        - papasrapa_rate_hz[rate >> 6]) * (rate & 63) / 64) * (ten ? 10 : 1);
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

    // LFO: 65536 per cycle. Each new cycle shifts in a held bar.
    const u32 before = state.lfo_phase >> 16;
    state.lfo_phase += static_cast<u32>(elapsed) * static_cast<u32>(hz) / 16;
    if (state.lfo_phase >> 16 != before) {
        state.seed = state.seed * 1664525u + 1013904223u;
        // The newest bar enters on the right (byte 15) and the row moves left.
        for (u32 k = 0; k < 3; ++k) state.bars[k] = state.bars[k] >> 8 | state.bars[k + 1] << 24;
        state.bars[3] = state.bars[3] >> 8 | (state.seed >> 24) << 24;
    }
    const bool blur = hz > blur_hz;
    const bool gate = (state.lfo_phase & 0xffffu) < 0x8000u;

    // Arcs leave the horn at 20 px/s plus 2 px/s per LFO hertz, up to 60.
    state.phase += static_cast<u32>(elapsed * clamp(20 + hz / 512, 20, 60));
    const u32 travel = (state.phase >> 10) % 42;
    const u32 time = input.reduced_motion ? 0 : (state.phase >> 12) & 15;
    const u32 wiggle = fm ? static_cast<u32>(mod * 6 / 1024) : 0;
    const u32 thick = 1 + divider / 3;
    for (u32 k = 0; k < 3; ++k) {
        const u32 radius = 10 + (travel + k * 14) % 42;
        arc(frame.paths[k], radius, wiggle, thick, time);
        u32 shade = 7;
        if (am && mod > 256) shade = blur ? 5 : gate ? 8 : 2;
        if (radius > 40 && shade > 3) shade -= 2;   // fading as it spreads
        place(frame, arcs + k, 156 * unit, 118 * unit, 1638, shade, stroke);
    }

    bar_path(frame.paths[3], state, state.lfo_phase >> 16, static_cast<u32>(sample_hold * 30 / 1024));
    place(frame, bars, 372 * unit, 176 * unit, unit, 7, stroke);

    const u32 flash = state.flash_age < 512 ? 8 - static_cast<u32>(state.flash_age * 5 / 512) : 3;
    place(frame, horn, 110 * unit, 118 * unit, unit, flash, fill_and_stroke);
    place(frame, fm_led, 88 * unit, 172 * unit, unit, fm ? 8 : 2, fill_and_stroke);
    place(frame, am_led, 118 * unit, 172 * unit, unit, am ? 8 : 2, fill_and_stroke);
    place(frame, lamp, 372 * unit, 84 * unit, unit, blur ? 5 : gate ? 8 : 2, fill_and_stroke);

    // Noise: up to twelve specks scattered in front of the horn, new every frame.
    const u32 shown = static_cast<u32>(noise * static_cast<i32>(speck_count) / 1024);
    u32 seed = input.reduced_motion ? 7u : state.seed ^ state.phase;
    for (u32 n = 0; n < speck_count; ++n) {
        if (n >= shown) { hide(frame, specks + n); continue; }
        seed = seed * 1664525u + 1013904223u;
        const i32 x = 180 + static_cast<i32>((seed >> 8) % 120);
        const i32 spread = (x - 160) * 11 / 20;
        const i32 y = 118 + static_cast<i32>((seed >> 20) % static_cast<u32>(2 * spread + 1)) - spread;
        place(frame, specks + n, x * unit, y * unit, unit, 0, keep);
    }

    // Range, Mode and Octave by name wherever a position shows them; an empty
    // header keeps the stock value of the continuous controls.
    for (u32 i = 0; i < opxy::page_controls; ++i) {
        const u32 slot = input.shown[i] < opxy::parameter_slots ? input.shown[i] : i;
        const u32 word = static_cast<u32>(clamp(input.slots[slot], 0, opxy::q31::maximum));
        opxy::draw::header(frame, i, slot == 4 ? papasrapa_range_labels[word >> 30]
                                   : slot == 5 ? papasrapa_mode_labels[word >> 29]
                                   : slot == 6 ? papasrapa_octave_labels[word >> 30] : "");
    }
}
}
