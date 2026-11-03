#include "display.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace drone {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// Node layout; artwork IDs drone_node_N follow it.
enum { blades = 0, rings = 4, hubs = 8, arms = 12, body = 14, plate = 15, led = 16, shadows = 17, bar = 24 };
// Text slots: the edited page's titles and values, then the other page's.
enum { titles = 0, values = 4, grey_titles = 8, grey_values = 12 };
// Palette: the stock screen's grey steps, black (0) to #CDCDCD (8).
const u32 lightest = 8;
const i32 settled = 4096;                 // LED age past its flash, Q10 seconds
const i32 rotor_x = 64, rotor_y = 44;     // rotor offsets from the body, pixels
const i32 sides_x[4] = {-1, 1, -1, 1};
const i32 sides_y[4] = {-1, -1, 1, 1};
// Spread pulls the rotors out of step, as it detunes the saws: Q10 speed offsets.
const i32 rotor_offsets[4] = {0, 300, -200, 450};

// A press switches between two pages (definition.json "press": "page"). Spread
// has no alternate, so it leads both pages; page 2 is Reso, Rate and Fifth.
// The artwork covers the stock header and draws both: page 1 on top, page 2
// under it. The page being edited is the stock header's size on its stepped
// bar; the other is smaller and grey. A switch grows and shrinks the rows in place.
const u32 pages[2][4] = {{0, 1, 2, 3}, {0, 4, 5, 6}};
// Each slot's title as the stock header writes it, in lower case.
const char* const names[7] = {"spread", "tone", "motion", "sub", "reso", "rate", "fifth"};
// Positions, Q10 pixels, on the stock header's columns: titles at each column's
// left, values 65 pixels in. Each row's top with its page edited and with the
// other page edited: page 1 stays at the top, and page 2 sits under page 1's
// bar or moves up 2 pixels into its own. An edited row has the stock header's
// baselines on the bar; a grey row has one baseline.
const i32 column = 120 * unit, title_x = 5 * unit, value_x = 65 * unit, bar_step = 60 * unit;
const i32 edited_top[2] = {0, 18 * unit}, grey_top[2] = {0, 20 * unit};
const i32 title_y = 10 * unit, value_y = 17818, grey_y = 13 * unit;
// The bright values are authored at the header's 20 px and the grey ones at
// 13 px, so each rests at unit scale: 13 px is 666/1024 of 20, and 20 px is
// 1575/1024 of 13.
const i32 grow_scale = 666, shrink_scale = 1575;
// A switch eases out exponentially: the distance left halves every 21 ms, so
// it is within 1% after 0.15 s, when it settles.
const i32 page_settle = 154;           // 0.15 s, Q10
const u32 octaves_per_q10 = 1496;      // Q15 halvings per Q10 second: one per 21 ms
// 2^(-k/8) in Q16 for k = 0..8: one octave, interpolated between eighths.
const u32 halving_q16[9] = {65536, 60097, 55109, 50535, 46341, 42495, 38968, 35734, 32768};

// 2^(-octaves) in Q16, for Q15 octaves (32768 = one octave).
inline u32 halving(u32 octaves_q15) {
    const u32 whole = octaves_q15 >> 15, eighth = (octaves_q15 >> 12) & 7, fraction = octaves_q15 & 4095;
    if (whole > 16) return 0;
    const u32 a = halving_q16[eighth], b = halving_q16[eighth + 1];
    return (a - (((a - b) * fraction) >> 12)) >> whole;
}
// Between where a switch settles and where it started: `left` (Q16) of the way back.
inline i32 between(i32 end, i32 start, u32 left) {
    return end + ((start - end) * static_cast<i32>(left >> 6) >> 10);
}
// A control as the header shows it: 0 to 100, without a percent sign.
inline void amount(char* out, u32 capacity, i32 value) {
    const u32 p = value < 0 ? 0 : ((static_cast<u32>(value) >> 10) * 100 + (1u << 20)) >> 21;
    char digits[4];
    u32 n = 0;
    if (p >= 100) digits[n++] = '1';
    if (p >= 10) digits[n++] = static_cast<char>('0' + p / 10 % 10);
    digits[n++] = static_cast<char>('0' + p % 10);
    digits[n] = 0;
    copy(out, capacity, digits);
}

// Both pages of the header, and the stock header's values too: covered on the
// device, shown by previews that draw the stock header over the artwork.
inline void draw_header(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    // The page being edited: page 2 once the second position shows its alternate.
    const u32 page = input.shown[1] >= 4 ? 1 : 0;
    if (state.page && state.track == input.track && state.page != page + 1 && !input.reduced_motion)
        state.since = 0;
    else if (state.since < page_settle)
        state.since += clamp(input.elapsed, 0, page_settle);
    if (input.reduced_motion || state.since > page_settle) state.since = page_settle;
    state.page = page + 1;
    const u32 left = state.since >= page_settle ? 0 : halving(static_cast<u32>(state.since) * octaves_per_q10);
    // The edited page's row grows from grey and the other's shrinks, each in
    // place, and the bar moves between them. The bright text draws the row the
    // bar is nearer, so the two sets trade rows halfway through.
    const u32 other = 1 - page, bright = left > (1u << 15) ? other : page;
    const i32 edited = edited_top[page], was = grey_top[page], grey = grey_top[other], from = edited_top[other];
    for (u32 i = 0; i < 8; ++i)
        place(frame, bar + i, static_cast<i32>(i) * bar_step, between(edited, from, left), unit, 0, keep);
    for (u32 i = 0; i < 4; ++i) {
        const u32 slot = input.shown[i] < 7 ? input.shown[i] : i;
        amount(frame.header[i], sizeof(frame.header[i]), input.slots[slot]);
    }
    for (u32 row = 0; row < 2; ++row) {
        const bool growing = row == page;
        const i32 ty = growing ? between(edited + title_y, was + grey_y, left) : between(grey + grey_y, from + title_y, left);
        const i32 vy = growing ? between(edited + value_y, was + grey_y, left) : between(grey + grey_y, from + value_y, left);
        // The values' size as a fraction of the header's 20 px.
        const i32 size = growing ? between(unit, grow_scale, left) : between(grow_scale, unit, left);
        const u32 t = row == bright ? titles : grey_titles, v = row == bright ? values : grey_values;
        const i32 scale = row == bright ? size : size * shrink_scale >> 10;
        for (u32 i = 0; i < 4; ++i) {
            const u32 slot = pages[row][i];
            char value[header_capacity];
            amount(value, sizeof(value), input.slots[slot]);
            text(frame, t + i, static_cast<i32>(i) * column + title_x, ty, unit, names[slot]);
            text(frame, v + i, static_cast<i32>(i) * column + value_x, vy, scale, value);
        }
    }
}

// Q10 sine, 256 steps per turn.
inline i32 wave(u32 index) {
    const i32 s = drone_sine[index & 255];
    return s / 2097152 - (s < 0 && s % 2097152 != 0);
}

// A two-blade propeller, narrow at the hub and the tips. The angle (256 steps
// per turn) and Q10 scale fix the geometry, so both form the key.
inline void blade(Frame::path_type& path, u32 angle, i32 scale) {
    const u32 key = 1 + angle + 256 * static_cast<u32>(scale);
    if (path.key == key) return;
    const i32 length = 27 * scale, width = 4 * scale;
    const i32 c = wave(angle + 64), s = wave(angle);
    for (u32 i = 0; i < Frame::point_count; ++i) {
        const i32 ct = wave(i * 4 + 64), st = wave(i * 4);
        const i32 x = floor_q10(length * ct);
        const i32 lobe = floor_q10(2 * st * (ct < 0 ? -ct : ct));
        const i32 y = floor_q10(width * lobe);
        path.points[i].x = floor_q10(x * c - y * s);
        path.points[i].y = floor_q10(x * s + y * c);
    }
    path.key = key;
}
} // namespace

inline void display_reset(DisplayState& state) {
    for (u32 k = 0; k < 4; ++k) state.spin[k] = k * 16384u;
    state.hover = state.previous_event = state.track = state.initialized = 0;
    state.led_age = settled;
    state.page = 0;
    state.since = page_settle;
}

// Only screen work. Tone spins the rotors faster, Spread pulls them out of
// step, Motion sets the hover, Sub weighs the drone down towards its shadow,
// and the LED flashes on each note start.
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const i32 spread = clamp(input.slots[0], 0, opxy::q31::maximum) >> 21;   // Q10
    const i32 tone = clamp(input.slots[1], 0, opxy::q31::maximum) >> 21;
    const i32 motion = clamp(input.slots[2], 0, opxy::q31::maximum) >> 21;
    const i32 sub = clamp(input.slots[3], 0, opxy::q31::maximum) >> 21;
    const i32 elapsed = input.reduced_motion ? 0 : clamp(input.elapsed, 0, 102);
    draw_header(state, input, frame);

    // A snapshot (first frame or new track) never fabricates a note.
    if (!state.initialized || state.track != input.track || input.reduced_motion) {
        state.led_age = settled;
    } else {
        state.led_age = clamp(state.led_age + elapsed, 0, settled);
        if (input.track < 8 && input.note_serial != state.previous_event) state.led_age = 0;
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // 0.5..4 turns per second; diagonal rotors share a direction, as on a quadcopter.
    const i32 turns = 512 + tone * 7 / 2;
    for (u32 k = 0; k < 4; ++k) {
        const i32 speed = turns * (1024 + spread * rotor_offsets[k] / 1024) / 1024;
        const i32 step = elapsed * speed / 16;
        state.spin[k] += static_cast<u32>(k == 0 || k == 3 ? step : -step);
    }
    // Hover: 0.3..1.2 Hz, 1..7 pixels; the sway runs at 5/8 of the bob.
    state.hover += static_cast<u32>(elapsed * (300 + motion * 900 / 1024) / 16);
    const i32 amplitude = 1024 + motion * 6;
    const i32 bob = floor_q10(amplitude * wave(state.hover >> 8));
    const i32 sway = floor_q10(amplitude * 3 / 2 * wave((((state.hover >> 3) * 5) >> 8) & 255));
    // Altitude 1 without Sub down to 0.3: lower is slightly smaller and nearer its shadow.
    const i32 altitude = 1024 - sub * 7 / 10;
    const i32 scale = (970 + altitude * 80 / 1024) * 92 / 100;   // drawn at 92 %, clear of the edges
    // Centred below the two header rows: the rotor guards stay under 38 px.
    const i32 cx = 240 * unit + sway, cy = 116 * unit + bob;
    const i32 sx = 240 * unit + sway + (6 + altitude * 18 / 1024) * unit;
    const i32 sy = 116 * unit + (6 + altitude * 18 / 1024) * unit;

    for (u32 k = 0; k < 4; ++k) {
        const i32 ox = sides_x[k] * rotor_x * scale, oy = sides_y[k] * rotor_y * scale;
        blade(frame.paths[k], (state.spin[k] >> 8) & 255, scale);
        place(frame, blades + k, cx + ox, cy + oy, unit, 7 + static_cast<u32>(tone >> 9), stroke);
        place(frame, rings + k, cx + ox, cy + oy, scale, 2 + static_cast<u32>(tone >> 9), stroke);
        place(frame, hubs + k, cx + ox, cy + oy, scale, 0, keep);
        place(frame, shadows + k, sx + ox, sy + oy, scale, 0, keep);
    }
    for (u32 n = 0; n < 2; ++n) {
        place(frame, arms + n, cx, cy, scale, 0, keep);
        place(frame, shadows + 4 + n, sx, sy, scale, 0, keep);
    }
    place(frame, shadows + 6, sx, sy, scale, 0, keep);
    place(frame, body, cx, cy, scale, 0, keep);
    place(frame, plate, cx, cy, scale, 0, keep);
    const u32 glow = state.led_age < 512 ? lightest - static_cast<u32>(state.led_age * 5 / 512) : 3;
    place(frame, led, cx, cy + 7 * scale, scale, glow, fill_and_stroke);
}
}
