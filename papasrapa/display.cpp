#include "display.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace papasrapa {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// Node layout; artwork IDs papasrapa_node_N follow it.
enum { wave = 0, needle = 1, fm_button = 2, am_button = 3, pointer = 4, lfo_lamp = 5, power_led = 6, antenna = 7,
       bar = 11 };
enum { title_text = 0, value_text = 4, grey_title_text = 8, grey_value_text = 12 };
const u32 segments = 4, bar_steps = 8;
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

// A press switches between two pages (definition.json "press": "page"): Rate,
// Mod, Divider and Noise, then Range, Mode, Octave and S&H. The artwork covers
// the stock header and draws both, page 1 on top and page 2 under it. The page
// being edited is the stock header's size on its stepped bar; the other is
// smaller and grey. A switch grows and shrinks the rows in place.
const u32 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
// Each slot's title as the stock header writes it, in lower case.
const char* const titles[8] = {"rate", "mod", "divider", "noise", "range", "mode", "octave", "s h"};
// Positions, Q10 pixels, on the stock header's columns: titles at each column's
// left, values 65 pixels in. Each row's top with its page edited and with the
// other page edited: page 2 sits under page 1's bar, or moves up 2 pixels into
// its own. An edited row has the stock header's baselines on the bar; a grey
// row has one baseline.
const i32 column = 120 * unit, title_x = 5 * unit, value_x = 65 * unit, bar_step = 60 * unit;
const i32 edited_top[2] = {0, 18 * unit}, grey_top[2] = {0, 20 * unit};
const i32 title_y = 10 * unit, value_y = 17818, grey_y = 13 * unit;
// The bright values are authored at the header's 20 px and the grey ones at
// 13 px, so each rests at unit scale: 13 px is 666/1024 of 20, and 20 px is
// 1575/1024 of 13.
const i32 grow_scale = 666, shrink_scale = 1575;
// A switch eases out exponentially: the distance left halves every 21 ms, so
// it is within 1 % after 0.15 s, when it settles.
const i32 settle = 154;               // 0.15 s, Q10
const u32 halvings_per_q10 = 1496;    // Q15 halvings per Q10 second

// 2^-halvings in Q16 for Q15 halvings, linear between whole ones.
inline u32 remaining(u32 halvings) {
    const u32 whole = halvings >> 15;
    if (whole >= 16) return 0;
    const u32 from = 65536u >> whole;
    return from - (from >> 1) * (halvings & 32767u) / 32768u;
}
// Between where a switch settles and where it started: `left` (Q16) of the way back.
inline i32 between(i32 end, i32 start, u32 left) {
    return end + ((start - end) * static_cast<i32>(left >> 6) >> 10);
}
// An amount, 0 to 100 without a percent sign; the parameter keeps its precision.
inline void amount(char* out, u32 capacity, i32 word) {
    const u32 p = ((static_cast<u32>(clamp(word, 0, opxy::q31::maximum)) >> 10) * 100 + (1u << 20)) >> 21;
    char digits[4];
    u32 n = 0;
    if (p >= 100) digits[n++] = '1';
    if (p >= 10) digits[n++] = static_cast<char>('0' + p / 10 % 10);
    digits[n++] = static_cast<char>('0' + p % 10);
    digits[n] = 0;
    copy(out, capacity, digits);
}
// A slot's value as the header rows show it: Range, Mode and Octave by name.
inline void caption(char* out, u32 capacity, u32 slot, const opxy::draw::Input& input) {
    const u32 word = static_cast<u32>(clamp(input.slots[slot], 0, opxy::q31::maximum));
    if (slot == 4) copy(out, capacity, papasrapa_range_labels[word >> 30]);
    else if (slot == 5) copy(out, capacity, papasrapa_mode_labels[word >> 29]);
    else if (slot == 6) copy(out, capacity, papasrapa_octave_labels[word >> 30]);
    else amount(out, capacity, input.slots[slot]);
}

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
    state.page = 0;
    state.since = settle;
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

    // The page being edited: page 2 once the first position shows an alternate.
    // A switch on the same track animates; a track change or reduced motion jumps.
    const u32 page = input.shown[0] >= 4 ? 1 : 0;
    if (state.page && state.initialized && state.track == input.track && state.page != page + 1 && !input.reduced_motion)
        state.since = 0;
    else if (state.since < settle)
        state.since += clamp(input.elapsed, 0, settle);
    if (input.reduced_motion || state.since > settle) state.since = settle;
    state.page = page + 1;

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

    // The stock header's values too: covered on the device, shown by previews
    // that draw the stock header over the artwork.
    for (u32 i = 0; i < opxy::page_controls; ++i) {
        const u32 slot = input.shown[i] < opxy::parameter_slots ? input.shown[i] : i;
        caption(frame.header[i], sizeof(frame.header[i]), slot, input);
    }

    // The edited page's row grows from grey and the other's shrinks, each in
    // place, and the bar moves between them. The bright text draws the row the
    // bar is nearer, so the two sets trade rows halfway through.
    const u32 left = state.since >= settle ? 0 : remaining(static_cast<u32>(state.since) * halvings_per_q10);
    const u32 other = 1 - page, bright = left > (1u << 15) ? other : page;
    const i32 edited = edited_top[page], was = grey_top[page], grey = grey_top[other], from = edited_top[other];
    for (u32 i = 0; i < bar_steps; ++i)
        place(frame, bar + i, static_cast<i32>(i) * bar_step, between(edited, from, left), unit, 0, keep);
    for (u32 row = 0; row < 2; ++row) {
        const bool growing = row == page;
        const i32 ty = growing ? between(edited + title_y, was + grey_y, left) : between(grey + grey_y, from + title_y, left);
        const i32 vy = growing ? between(edited + value_y, was + grey_y, left) : between(grey + grey_y, from + value_y, left);
        // The values' size as a fraction of the header's 20 px.
        const i32 size = growing ? between(unit, grow_scale, left) : between(grow_scale, unit, left);
        const u32 title_slot = row == bright ? title_text : grey_title_text;
        const u32 value_slot = row == bright ? value_text : grey_value_text;
        const i32 scale = row == bright ? size : size * shrink_scale >> 10;
        for (u32 i = 0; i < 4; ++i) {
            const i32 x = static_cast<i32>(i) * column;
            char value[header_capacity];
            caption(value, sizeof(value), pages[row][i], input);
            text(frame, title_slot + i, x + title_x, ty, unit, titles[pages[row][i]]);
            text(frame, value_slot + i, x + value_x, vy, scale, value);
        }
    }
}
}
