#include "display.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace papasrapa {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// Node layout; artwork IDs papasrapa_node_N follow it.
enum { wave = 0, needle = 1, fm_button = 2, am_button = 3, pointer = 4, lfo_lamp = 5, power_led = 6, antenna = 7 };
const u32 segments = 4;
const i32 settled = 4096;      // flash age past its decay, Q10 seconds
const i32 blur_hz = 12 * 1024; // above this the LFO lamp stops visibly blinking
const i32 window_x = 240, window_y = 104;   // tuning window centre, pixels
const i32 pivot_x = 360, pivot_y = 62;      // antenna foot, pixels
const i32 segment_length = 13;              // pixels
// The radio is drawn at 85 % about (240, 125), the artwork's centre: sx and sy
// take an artwork position in pixels to the screen in Q10.
const i32 zoom = 870;   // Q10
inline i32 sx(i32 x) { return 240 * unit + (x - 240) * zoom; }
inline i32 sy(i32 y) { return 125 * unit + (y - 125) * zoom; }

inline i32 control(i32 word) { return clamp(word, 0, opxy::q31::maximum) >> 21; }  // Q10

struct Wave {
    u32 scroll, lfo_phase, held, frame;
    u32 divider;
    bool fm, am;
    i32 mod, noise, sample_hold;   // Q10
};

// Four cycles of the output across the 232-pixel window, authored at half
// size (paths reach 64 pixels) and placed at double scale, as the controls
// shape it: the divided copy steps the square, FM squeezes it, AM gates it
// with the LFO, S&H shifts it by the held values and noise roughens it. The
// frame count is the key, so it is rebuilt every frame.
inline void wave_path(Frame::path_type& path, const Wave& w) {
    path.key = w.frame;
    for (u32 i = 0; i < Frame::point_count; ++i) {
        i32 y = 13 * unit;   // the ends sit on the window's lower edge, where Z closes
        if (i != 0 && i + 1 != Frame::point_count) {
            u32 t = i * 4 * 65536u / 63 + w.scroll;
            if (w.fm) t += static_cast<u32>(sine(static_cast<i32>(i * 171 % 3600)) * w.mod / 64);
            // The divided copy rides on the tone as a smaller step: a staircase.
            i32 level = (t & 0xffffu) < 0x8000u ? 12 : -12;
            if (w.divider > 1) level = level * 2 / 3 + ((t / w.divider & 0xffffu) < 0x8000u ? 4 : -4);
            if (w.am && ((i * 2 * 65536u / 63 + w.lfo_phase) & 0xffffu) >= 0x8000u) level = level * (1024 - w.mod) / 1024;
            const i32 held = static_cast<i32>(w.held >> (8 * (i * 4 / 64)) & 255u) - 128;
            const i32 rough = static_cast<i32>((i * 2654435761u + w.frame * 40503u) >> 28) - 8;
            y = -level * unit + held * w.sample_hold * 8 / 128 + rough * w.noise / 3;
        }
        path.points[i] = Point{(-58 + static_cast<i32>(i) * 116 / 63) * unit, i != 0 && i + 1 != Frame::point_count ? y / 2 : y};
    }
}
} // namespace

inline void display_reset(DisplayState& state) {
    state.scroll = state.lfo_phase = state.held = state.frames = 0;
    state.previous_event = state.track = state.initialized = 0;
    state.seed = 1;
    state.flash_age = settled;
}

// Only screen work. The tuning window shows the output wave with Mod,
// Divider, Noise and S&H; the needle stands at the Divider. Mode lights the
// fm and am band buttons, Rate turns the knob and blinks the LFO lamp, Octave
// pulls the antenna out (Hi) or in (Low), and the power LED flashes on each
// note start.
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const i32 rate = control(input.slots[0]);
    const bool ten = input.slots[4] >= 0x40000000;
    const u32 mode = static_cast<u32>(clamp(input.slots[5], 0, opxy::q31::maximum)) >> 29;
    const bool low = input.slots[6] >= 0x40000000;
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

    // LFO: 65536 per cycle. Each new cycle shifts a held value in.
    const u32 before = state.lfo_phase >> 16;
    state.lfo_phase += static_cast<u32>(elapsed) * static_cast<u32>(hz) / 16;
    if (state.lfo_phase >> 16 != before) {
        state.seed = state.seed * 1664525u + 1013904223u;
        state.held = state.held << 8 | state.seed >> 24;
    }
    state.scroll += static_cast<u32>(elapsed) * 32;   // half a cycle per second
    if (elapsed && ++state.frames == 0xffffffffu) state.frames = 0;

    Wave w;
    w.scroll = state.scroll;
    w.lfo_phase = state.lfo_phase & 0xffffu;
    w.held = state.held;
    w.frame = state.frames + 1;
    w.divider = 1 + static_cast<u32>(control(input.slots[2]) * 16 / 1024);
    w.fm = mode & 1u;
    w.am = (mode & 2u) != 0;
    w.mod = control(input.slots[1]);
    w.noise = control(input.slots[3]);
    w.sample_hold = control(input.slots[7]);
    wave_path(frame.paths[0], w);
    place(frame, wave, sx(window_x), sy(window_y), 2 * zoom, 0, keep);
    place(frame, needle, sx(window_x - 112 + static_cast<i32>(w.divider - 1) * 224 / 16), sy(window_y), zoom, 0, keep);

    place(frame, fm_button, sx(238), sy(160), zoom, w.fm ? 8 : 2, fill_and_stroke);
    place(frame, am_button, sx(238), sy(184), zoom, w.am ? 8 : 2, fill_and_stroke);
    place(frame, pointer, sx(322), sy(172), zoom, 0, keep);
    turn(frame, pointer, sweep(input.slots[0], 1350));
    const bool gate = (state.lfo_phase & 0xffffu) < 0x8000u;
    place(frame, lfo_lamp, sx(356), sy(146), zoom, hz > blur_hz ? 5 : gate ? 8 : 2, fill_and_stroke);
    place(frame, power_led, sx(112), sy(72), zoom, state.flash_age < 384 ? 8 : 3, fill_and_stroke);

    // The antenna leans 45 degrees to the right; with Mod it sways a little
    // with the LFO. Low pulls the top two segments in.
    const Turn lean = rotation(450 + w.mod * 20 / 1024 * sine(static_cast<i32>(w.lfo_phase * 3600 >> 16)) / 1024);
    for (u32 s = 0; s < segments; ++s) {
        if (low && s >= 2) { hide(frame, antenna + s); continue; }
        const i32 along = static_cast<i32>(s) * segment_length;
        place(frame, antenna + s, sx(pivot_x) + along * lean.sin * zoom / unit, sy(pivot_y) - along * lean.cos * zoom / unit, zoom, 0, keep);
        turn(frame, antenna + s, lean);
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
