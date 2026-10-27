#include "display.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace drone {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

// Node layout; artwork IDs drone_node_N follow it.
enum { blades = 0, rings = 4, hubs = 8, arms = 12, body = 14, plate = 15, led = 16, shadows = 17 };
// Palette: the stock screen's grey steps, black (0) to #CDCDCD (8).
const u32 lightest = 8;
const i32 settled = 4096;                 // LED age past its flash, Q10 seconds
const i32 rotor_x = 64, rotor_y = 44;     // rotor offsets from the body, pixels
const i32 sides_x[4] = {-1, 1, -1, 1};
const i32 sides_y[4] = {-1, -1, 1, 1};
// Spread pulls the rotors out of step, as it detunes the saws: Q10 speed offsets.
const i32 rotor_offsets[4] = {0, 300, -200, 450};

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
    const i32 cx = 240 * unit + sway, cy = 108 * unit + bob;
    const i32 sx = 240 * unit + sway + (6 + altitude * 18 / 1024) * unit;
    const i32 sy = 108 * unit + (6 + altitude * 18 / 1024) * unit;

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
