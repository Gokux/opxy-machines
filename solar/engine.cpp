#include "core.h"
#include <opxy/q31.hpp>
#include "chords.h"
#include "tables.h"

namespace solar {
namespace {

using opxy::q31::multiply;

// Per-oscillator level, Q31: five at once stay below full scale.
const i32 OSC_LEVEL = 0x170a3d71;          // 0.18
const i32 DRIFT_FOLLOW = 3221225;          // per-block approach to a new drift, about 0.0015
const i32 DRIFT_RANGE = 64424509;          // 0.03: full Drift wanders about +-26 cents
const i32 FM_MAX = 1503238554;             // 0.7: deepest cross-modulation at full Volt
const u32 HIGHEST = 0x60000000u;           // above about 16.5 kHz an oscillator falls silent
// How strongly each oscillator follows the light, as if the MOD buttons were
// half-pressed differently; Q31.
const i32 light_sensitivity[5] = {751619277, 2147483647, 1288490189, 1932735283, 966367642};

inline i32 nonnegative(i32 value) { return value < 0 ? 0 : value; }
inline u32 minimum(u32 a, u32 b) { return a < b ? a : b; }

inline u32 next(u32& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
// Uniform in [-0.5, 0.5) of Q31.
inline i32 random_half(u32& seed) { return static_cast<i32>(next(seed) >> 1) - 0x40000000; }

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

// The audio loop uses Q15 samples and Q23 smoothed gains. Products fit
// in 32 bits; phases and pitch increments retain all 32 bits.
inline i32 floor_bits(i32 value, unsigned bits) {
    return value >= 0 ? value >> bits : -1 - ((-1 - value) >> bits);
}
inline i32 mul15(i32 a, i32 b) { return floor_bits(a * b, 15); }

// Negistor curve with eight fractional interpolation bits (Q15 output).
inline i32 negistor(u32 phase, u32 increment) {
    const u32 index = phase >> 24;
    const i32 low = floor_bits(solar_charge[index], 16);
    const i32 high = floor_bits(solar_charge[index + 1], 16);
    i32 value = low + floor_bits((high - low) * static_cast<i32>((phase >> 16) & 255), 8);
    const u32 step = increment >> 16;
    if (step && increment < 0x80000000u) {
        if (phase < increment) {
            const u32 x = 65535u - minimum(phase / step, 65535u);
            value += mul15(static_cast<i32>((x * x) >> 17), SOLAR_JUMP >> 16);
        } else if (0u - phase < increment) {
            const u32 x = 65535u - minimum((0u - phase) / step, 65535u);
            value -= mul15(static_cast<i32>((x * x) >> 17), SOLAR_JUMP >> 16);
        }
    }
    return value;
}

inline i32 clip(i32 x) {
    x = x < -32767 ? -32767 : x > 32767 ? 32767 : x;
    const i32 cube = mul15(mul15(x, x), x);
    // (3x - x^3) / 8; quarter-scale peak in the Q31 output buffer.
    return (3 * x - cube) * 8192;
}

// A 64-sample linear ramp follows the same ~8 ms control time constant.
// Compute its slope once per block; the audio loop only adds it. The Q23
// endpoint quantization is below two Q15 sample/gain steps.
inline i32 ramp(i32& current, i32 target) {
    const i32 difference = target - current;
    if (difference > -384 && difference < 384) { current = target; return 0; }
    return floor_bits(difference * 43, 14);
}

// Specializing the five oscillators exposes fixed state offsets to the compiler.
// Calls stay ordered: each oscillator reads its neighbor's most recent sample.
template <unsigned n, bool moving>
inline i32 oscillator(Voice& voice, const u32* increments, const i32* steps) {
    if (moving) voice.gains[n] += steps[n];
    // Each oscillator is modulated by its neighbour, the first by the last.
    const i32 modulator = voice.outputs[n == 0 ? 4 : n - 1];
    const i32 depth = mul15(voice.fm >> 8, modulator);
    // Split the unsigned increment so a signed Q15 product cannot overflow.
    const i32 bend = static_cast<i32>(increments[n] >> 16) * depth
        + floor_bits(static_cast<i32>(increments[n] & 65535u) * depth, 16);
    const u32 increment = increments[n] + static_cast<u32>(bend * 2);
    const i32 sample = negistor(voice.phases[n], increment);
    voice.outputs[n] = sample;
    const i32 result = mul15(sample, voice.gains[n] >> 8);
    voice.phases[n] += increment;
    return result;
}

template <bool moving>
inline void render_block(Voice& voice, opxy::Output output, const u32* increments, const i32* steps, i32 fm_step) {
    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        if (moving) voice.fm += fm_step;
        i32 mix = 0;
        mix += oscillator<0, moving>(voice, increments, steps);
        mix += oscillator<1, moving>(voice, increments, steps);
        mix += oscillator<2, moving>(voice, increments, steps);
        mix += oscillator<3, moving>(voice, increments, steps);
        mix += oscillator<4, moving>(voice, increments, steps);
        output[i] = clip(mix);
    }
}

}

// The oscillators run free on the real instrument, so a note starts them at
// random phases. Each voice keeps its own small component deviations.
inline void start(Voice& voice, Shared&, u32 owner) {
    voice.seed = owner * 0x9e3779b9u | 1u;
    for (u32 n = 0; n < 5; ++n) {
        voice.phases[n] = next(voice.seed);
        voice.outputs[n] = voice.drift[n] = 0;
        voice.drift_targets[n] = random_half(voice.seed);
        voice.tolerance[n] = static_cast<i32>(next(voice.seed) >> 8) - 0x800000;  // about +-7 cents
        voice.gains[n] = 0;
    }
    voice.light = voice.light_target = 0;
    voice.light_clock = 0;
    voice.fm = 0;
    voice.initialized = 0;
}
inline void initialize(Voice& voice, Shared& shared, u32 owner) { start(voice, shared, owner); }
inline void destroy(Voice&, Shared&, u32) {}

// Parameters: 0 Chord, 1 Volt, 2 Mod, 3 Mute, then the alternates 4 Drift
// (Chord) and 5 Flicker (Mod). Pitch is read once per block.
inline void render(Voice& voice, Shared&, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const i32* chord = solar_chords[static_cast<u32>(nonnegative(parameters[0])) >> 28];
    const i32 volt = nonnegative(parameters[1]);
    const i32 mod = nonnegative(parameters[2]);
    const u32 mute = solar_mutes[static_cast<u32>(nonnegative(parameters[3])) >> 28];
    const i32 drift_amount = nonnegative(parameters[4]);
    // Volt transposes down over its first half; beyond it the oscillators
    // modulate each other.
    const i32 transpose = lookup(solar_volt, volt);
    const i32 fm = volt > 0x40000000 ? multiply((volt - 0x40000000) * 2, FM_MAX) : 0;

    // The light: a new level at Flicker's rate, followed at the same rate.
    const u32 flicker = static_cast<u32>(lookup(solar_flicker, parameters[5]));
    voice.light_clock += flicker;
    if (voice.light_clock < flicker) voice.light_target = random_half(voice.seed);
    voice.light += multiply(voice.light_target - voice.light, static_cast<i32>(flicker * 3));

    u32 increments[5];
    i32 targets[5];
    for (u32 n = 0; n < 5; ++n) {
        // Drift: each oscillator wanders to a new pitch about every second and a half.
        if ((next(voice.seed) & 1023u) == 0) voice.drift_targets[n] = random_half(voice.seed);
        voice.drift[n] += multiply(voice.drift_targets[n] - voice.drift[n], DRIFT_FOLLOW);
        const i32 deviation = voice.tolerance[n]
            + multiply(multiply(voice.drift[n], drift_amount), DRIFT_RANGE)
            + multiply(multiply(voice.light, mod), light_sensitivity[n]);
        const i32 ratio = multiply_q28(multiply_q28(solar_semitones[chord[n] + 24], transpose),
                                       (1 << 28) + deviation / 8);
        const u32 increment = scale(pitch[0], ratio, HIGHEST);
        const bool audible = increment < HIGHEST && (mute >> n & 1u);
        increments[n] = increment < HIGHEST ? increment : 0;
        targets[n] = audible ? OSC_LEVEL >> 8 : 0;
    }
    if (!voice.initialized) {
        for (u32 n = 0; n < 5; ++n) voice.gains[n] = targets[n];
        voice.fm = fm >> 8;
        voice.initialized = 1;
    }
    const i32 fm_step = ramp(voice.fm, fm >> 8);
    i32 steps[5];
    bool moving = fm_step != 0;
    for (unsigned n = 0; n < 5; ++n) {
        steps[n] = ramp(voice.gains[n], targets[n]);
        moving = moving || steps[n] != 0;
    }
    if (moving) render_block<true>(voice, output, increments, steps, fm_step);
    else render_block<false>(voice, output, increments, steps, fm_step);
}

} // namespace solar
