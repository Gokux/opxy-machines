#include "core.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace papasrapa {
namespace {

using opxy::q31::multiply;
using opxy::q31::half;

const i32 FULL = opxy::q31::maximum;
const i32 FM_SPAN = 805306368;     // 3.0 in Q4.28: full Mod raises the tone up to 4x
const i32 SH_SPAN = 402653184;     // 1.5 in Q4.28: full S&H moves the tone 0.25x..1.75x
const i32 NOISE_LEVEL = 0x3851eb85; // 0.44
const i32 TONE_LEVEL = 0x40000000;  // 0.5: the half-scale squares at a quarter, as loud as the other drones
const i32 AM_SMOOTH = 150000000;   // about 0.07 per sample: AM edges take ~0.3 ms
const u32 HIGHEST = 0x40000000u;   // the tone stops rising at 11 kHz

inline i32 nonnegative(i32 value) { return value < 0 ? 0 : value; }
inline u32 minimum(u32 a, u32 b) { return a < b ? a : b; }

inline u32 next(u32& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

inline i32 lookup(const i32* table, i32 position) {
    const u32 p = static_cast<u32>(nonnegative(position));
    const u32 index = p >> 27;
    const i32 fraction = static_cast<i32>((p & 0x7ffffffu) << 4);
    return table[index] + multiply(table[index + 1] - table[index], fraction);
}

// The device links no 64-bit multiply, so products use the Q31 multiply.
// a * b in Q4.28, for positive operands whose product stays below 8.0.
inline i32 multiply_q28(i32 a, i32 b) { return multiply(a, b) << 3; }
// increment * ratio (Q4.28), at most ceiling: (increment / 2 * ratio) >> 31 is a sixteenth of it.
inline u32 scale(u32 increment, i32 ratio, u32 ceiling) {
    const u32 sixteenth = static_cast<u32>(multiply(static_cast<i32>(increment >> 1), ratio));
    return sixteenth >= ceiling >> 4 ? ceiling : sixteenth << 4;
}

// (1 - x)^2 at half scale for x = distance / increment, the PolyBLEP residual
// of a unit step; zero once the edge is a sample away.
inline i32 residual(u32 distance, u32 increment) {
    const u32 step = increment >> 16;
    if (!step || distance >= increment) return 0;
    const u32 x = 65535u - minimum(distance / step, 65535u);
    return static_cast<i32>((x * x) >> 2);
}

// The Schmitt-trigger oscillator's output: a band-limited square at half scale.
inline i32 square(u32 phase, u32 increment) {
    i32 value = phase < 0x80000000u ? 0x40000000 : -0x40000000;
    // Rising edge at 0, falling edge at one half.
    value -= residual(phase, increment);
    value += residual(0u - phase - 1u, increment);
    value += residual(phase - 0x80000000u, increment);
    value -= residual(0x7fffffffu - phase, increment);
    return value;
}

// Cubic soft clip to a quarter of full scale (-12 dB), the declared output peak.
inline i32 clip(i32 x) {
    const i32 cube = multiply(multiply(x, x), x);
    const i32 shaped = multiply(x, 0x60000000) - multiply(cube, 0x20000000);
    return multiply(shaped, 0x40000000);
}
}

inline void start(Voice& voice, Shared&, u32 owner) {
    voice.seed = owner * 0x9e3779b9u | 1u;
    voice.phase = voice.sub_phase = 0;
    voice.lfo_phase = 0;
    voice.glide = voice.held = 0;
    voice.amplitude = FULL;
}
inline void initialize(Voice& voice, Shared& shared, u32 owner) { start(voice, shared, owner); }
inline void destroy(Voice&, Shared&, u32) {}

// Parameters: 0 Rate, 1 Mod, 2 Divider, 3 Noise, then the alternates 4 Range
// (Rate), 5 Mode (Mod), 6 Octave (Divider) and 7 S&H (Noise). Mode bit 0 is
// the FM switch and bit 1 the AM switch, as in the Drone, FM, AM, Both labels.
inline void render(Voice& voice, Shared&, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const bool ten = parameters[4] >= 0x40000000;
    const u32 rate = static_cast<u32>(lookup(papasrapa_rate, parameters[0])) * (ten ? 10u : 1u);
    const i32 mod = nonnegative(parameters[1]);
    // Divider 1..16: the tone's copy counted down by that many cycles.
    const u32 divider = 1 + ((static_cast<u32>(nonnegative(parameters[2])) >> 16) * 16 >> 15);
    const i32 reciprocal = FULL / static_cast<i32>(divider);
    const i32 noise = multiply(nonnegative(parameters[3]), NOISE_LEVEL);
    const u32 mode = static_cast<u32>(nonnegative(parameters[5])) >> 29;
    const bool fm = mode & 1u, am = (mode & 2u) != 0;
    const bool low = parameters[6] >= 0x40000000;
    const i32 sample_hold = nonnegative(parameters[7]);
    // The LFO square charges the tone's timing capacitor: the jump becomes a
    // swoop with a time constant of 0.15 / rate, so slow rates sound like sirens.
    const i32 follow = static_cast<i32>(rate * 3 + rate / 3);

    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        voice.lfo_phase += rate;
        // A rising LFO edge clocks the sample-and-hold.
        if (voice.lfo_phase < rate) voice.held = static_cast<i32>(next(voice.seed) >> 1) - 0x40000000;
        const i32 gate = voice.lfo_phase < 0x80000000u ? FULL : 0;
        voice.glide += multiply(gate - voice.glide, follow);

        i32 ratio = 1 << 28;
        if (fm) ratio += multiply(multiply(voice.glide, mod), FM_SPAN);
        ratio = multiply_q28(ratio, (1 << 28) + multiply(multiply(voice.held, sample_hold), SH_SPAN));
        const u32 base = low ? pitch[i] >> 2 : pitch[i];
        const u32 increment = scale(base, ratio, HIGHEST);

        i32 tone = square(voice.phase, increment);
        if (divider > 1) {
            const u32 divided = static_cast<u32>(multiply(static_cast<i32>(increment >> 1), reciprocal)) << 1;
            tone = half(tone) + half(square(voice.sub_phase, divided));
            voice.sub_phase += divided;
        }
        voice.phase += increment;

        const i32 target = am ? FULL - multiply(mod, FULL - gate) : FULL;
        voice.amplitude += multiply(target - voice.amplitude, AM_SMOOTH);
        const i32 hiss = multiply(static_cast<i32>(next(voice.seed)), noise);
        output[i] = clip(multiply(multiply(tone, TONE_LEVEL) + hiss, voice.amplitude));
    }
}

} // namespace papasrapa
