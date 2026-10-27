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
       cells = 14, speckles = 54, bar = 86 };
const u32 speckle_count = 32;
const u32 levels = 4;                       // cells per wing side
const i32 settled = 4096;                   // flash age past its decay, Q10 seconds
const i32 sun_x = 66, sun_y = 78;           // pixels, clear of the header's two rows
const i32 station_x = 300;                  // pixels
const i32 wing_x[5] = {-96, -48, 0, 48, 96};
// Flicker 0..1 in 16 steps: light changes per second, Q10 (0.05..40 Hz, as the engine).
const i32 flicker_hz[17] = {51, 78, 118, 179, 272, 414, 628, 954, 1448, 2199, 3340, 5072, 7702, 11696, 17761, 26972, 40960};

inline i32 control(i32 word) { return clamp(word, 0, opxy::q31::maximum) >> 21; }  // Q10

// The header. A press switches between two pages (definition.json "press":
// "page"): Chord, Volt, Mod and Mute, then Drift, Volt, Flicker and Mute;
// Volt and Mute have no alternate, so page 2 shows them again. The artwork
// covers the stock header and draws both pages in place: page 1 on top, page
// 2 under it. The page being edited is the stock header's size on its
// stepped bar; the other is smaller and grey. A switch grows and shrinks the
// rows in place.
enum { title_text = 0, value_text = 4, grey_title_text = 8, grey_value_text = 12 };
const u32 bar_steps = 8;
const u32 pages[2][4] = {{0, 1, 2, 3}, {4, 1, 5, 3}};
// Each slot's title as the stock header writes it, in lower case.
const char* const titles[6] = {"chord", "volt", "mod", "mute", "drift", "flicker"};
// Positions, Q10 pixels, on the stock header's columns: titles at each
// column's left, values 65 pixels in. Each row's top with its page edited and
// with the other page edited: page 2 sits under page 1's bar, or moves up 2
// pixels into its own.
const i32 column = 120 << 10, title_x = 5 << 10, value_x = 65 << 10, bar_step = 60 << 10;
const i32 edited_top[2] = {0, 18 << 10}, grey_top[2] = {0, 20 << 10};
const i32 title_y = 10 << 10, value_y = 17818, grey_y = 13 << 10;
// Bright values are authored at the header's 20 px and grey ones at 13 px, so
// each rests at unit scale: 13 px is 666/1024 of 20, 20 px 1575/1024 of 13.
const i32 grow_scale = 666, shrink_scale = 1575;
// An exponential ease-out: the distance left halves every 21 ms, within 1%
// after 0.15 s, when it settles.
const i32 page_settle = 154;           // 0.15 s, Q10
const u32 octaves_per_q10 = 1496;      // Q15 halvings per Q10 second
const u32 halving_q16[9] = {65536, 60097, 55109, 50535, 46341, 42495, 38968, 35734, 32768};

inline u32 halving(u32 octaves_q15) {
    const u32 whole = octaves_q15 >> 15, eighth = (octaves_q15 >> 12) & 7, fraction = octaves_q15 & 4095;
    if (whole >= 16) return 0;
    const u32 a = halving_q16[eighth], b = halving_q16[eighth + 1];
    return (a - (((a - b) * fraction) >> 12)) >> whole;
}
// Between where a switch settles and where it started: `left` (Q16) of the way back.
inline i32 between(i32 end, i32 start, u32 left) { return end + ((start - end) * static_cast<i32>(left >> 6) >> 10); }

// A slot's value as the header shows it: Chord and Mute by name, the
// continuous controls 0 to 100.
inline void caption(char* out, u32 capacity, u32 slot, const opxy::draw::Input& input) {
    const u32 word = static_cast<u32>(clamp(input.slots[slot], 0, opxy::q31::maximum));
    if (slot == 0 || slot == 3) {
        copy(out, capacity, slot == 0 ? solar_chord_labels[word >> 28] : solar_mute_labels[word >> 28]);
        return;
    }
    const u32 p = ((word >> 10) * 100 + (1u << 20)) >> 21;
    char text[4];
    u32 n = 0;
    if (p >= 100) text[n++] = '1';
    if (p >= 10) text[n++] = static_cast<char>('0' + p / 10 % 10);
    text[n++] = static_cast<char>('0' + p % 10);
    text[n] = 0;
    copy(out, capacity, text);
}

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
    state.page = state.page_track = 0;
    state.since = page_settle;
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
        const i32 y = 40 + static_cast<i32>((seed >> 24) * 178 >> 8);   // below the header's rows
        const u32 shade = (seed & 7u) == 0 && glow > 384 ? 8 : 3 + (seed >> 3 & 3u);
        place(frame, speckles + k, x * unit, y * unit, unit, shade, fill_and_stroke);
    }

    // The page being edited: page 2 once Chord's position shows Drift. A
    // switch on the same track starts the rows moving; a new track, the first
    // frame or reduced motion shows the end at once.
    const u32 page = input.shown[0] >= 4 ? 1 : 0;
    if (state.page && state.page_track == input.track && state.page != page + 1 && !input.reduced_motion)
        state.since = 0;
    else if (state.since < page_settle)
        state.since += clamp(input.elapsed, 0, page_settle);
    if (input.reduced_motion || state.since > page_settle) state.since = page_settle;
    state.page = page + 1;
    state.page_track = input.track;
    const u32 left = state.since >= page_settle ? 0 : halving(static_cast<u32>(state.since) * octaves_per_q10);
    // The edited page's row grows from grey and the other's shrinks, each in
    // place, and the bar moves between them. The bright text draws the row the
    // bar is nearer, so the two sets trade rows halfway through.
    const u32 other = 1 - page, bright = left > (1u << 15) ? other : page;
    const i32 edited = edited_top[page], was = grey_top[page], grey = grey_top[other], from = edited_top[other];
    for (u32 i = 0; i < bar_steps; ++i)
        place(frame, bar + i, static_cast<i32>(i) * bar_step, between(edited, from, left), unit, 0, keep);
    // The stock header's values too: covered on the device, shown by previews
    // that draw the stock header over the artwork.
    for (u32 i = 0; i < opxy::page_controls; ++i) {
        const u32 slot = input.shown[i] < 6 ? input.shown[i] : i;
        caption(frame.header[i], sizeof(frame.header[i]), slot, input);
    }
    for (u32 row = 0; row < 2; ++row) {
        const bool growing = row == page;
        const i32 ty = growing ? between(edited + title_y, was + grey_y, left) : between(grey + grey_y, from + title_y, left);
        const i32 vy = growing ? between(edited + value_y, was + grey_y, left) : between(grey + grey_y, from + value_y, left);
        // The values' size as a fraction of the header's 20 px.
        const i32 size = growing ? between(unit, grow_scale, left) : between(grow_scale, unit, left);
        const u32 title_slots = row == bright ? title_text : grey_title_text;
        const u32 value_slots = row == bright ? value_text : grey_value_text;
        const i32 scale = row == bright ? size : size * shrink_scale >> 10;
        for (u32 i = 0; i < 4; ++i) {
            const i32 x = static_cast<i32>(i) * column;
            const u32 slot = pages[row][i];
            char value[header_capacity];
            caption(value, sizeof(value), slot, input);
            text(frame, title_slots + i, x + title_x, ty, unit, titles[slot]);
            text(frame, value_slots + i, x + value_x, vy, scale, value);
        }
    }
}
}
