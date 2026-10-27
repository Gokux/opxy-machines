#include "core.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace drone {
namespace {

using opxy::i64;
using opxy::q31::multiply;

// Mix levels, Q31: four saws and the sub stay below full scale before the filter.
const i32 SAW_LEVEL = 0x147ae148;   // 0.16
const i32 SUB_LEVEL = 0x23d70a3d;   // 0.28
// Motion sweeps the Tone position by up to 0.3, about two octaves of cutoff.
const i32 SWEEP_DEPTH = 644245094;
// Smallest Motion drift, 2 cents, so a unison drone still moves.
const i32 DRIFT_FLOOR = 2482270;

// Q15 audio products and defined floor shifts stay within signed 32 bits.
inline i32 floor_bits(i32 value, unsigned bits) {
    return value >= 0 ? value >> bits : -1 - ((-1 - value) >> bits);
}
inline i32 mul15(i32 a, i32 b) { return floor_bits(a * b, 15); }
inline i32 clip(i32 x) {
    x = x < -32767 ? -32767 : x > 32767 ? 32767 : x;
    const i32 cube = mul15(mul15(x, x), x);
    return (3 * x - cube) * 8192;
}
inline i32 clamp17(i32 x) { return x < -131071 ? -131071 : x > 131071 ? 131071 : x; }

inline i32 nonnegative(i32 value) { return value < 0 ? 0 : value; }
inline u32 minimum(u32 a, u32 b) { return a < b ? a : b; }
inline i32 saturate(i64 value) {
    return value > opxy::q31::maximum ? opxy::q31::maximum
         : value < -opxy::q31::maximum ? -opxy::q31::maximum : static_cast<i32>(value);
}

inline i32 sine(u32 phase) {
    const u32 index = phase >> 24;
    const i32 low = floor_bits(drone_sine[index], 16);
    const i32 high = floor_bits(drone_sine[index + 1], 16);
    return low + floor_bits((high - low) * static_cast<i32>((phase >> 16) & 255), 8);
}

// 17-entry tables over a Q31 control position.
inline i32 lookup(const i32* table, i32 position) {
    const u32 p = static_cast<u32>(nonnegative(position));
    const u32 index = p >> 27;
    const i32 fraction = static_cast<i32>((p & 0x7ffffffu) << 4);
    return table[index] + multiply(table[index + 1] - table[index], fraction);
}

// Band-limited saw: the naive ramp plus a polynomial (PolyBLEP) correction on
// the samples either side of each wrap. Integer division as in Braids; below
// 2^16 (about 0.7 Hz) the step is too small to need it.
inline i32 saw(u32 phase, u32 increment) {
    i32 value = static_cast<i32>(phase >> 16) - 32768;
    const u32 step = increment >> 16;
    if (step && increment < 0x80000000u) {
        // (1 - x)^2 with x in Q16; shifting by 17 yields Q15.
        if (phase < increment) {
            const u32 x = 65535u - minimum(phase / step, 65535u);
            value += static_cast<i32>((x * x) >> 17);
        } else if (0u - phase < increment) {
            const u32 x = 65535u - minimum((0u - phase) / step, 65535u);
            value -= static_cast<i32>((x * x) >> 17);
        }
    }
    return value;
}

inline u32 detuned(u32 increment, i32 amount) {
    const i32 bend = static_cast<i32>(increment >> 16) * amount
        + floor_bits(static_cast<i32>(increment & 65535u) * amount, 16);
    return increment + static_cast<u32>(bend * 2);
}

// Q23 control state follows its target by a 64-sample linear ramp per block,
// the same ~8 ms time constant as a 93/32768 one-pole. Compute its slope once
// per block; the audio loop only adds it. Snapping below 384 stays under one
// Q15 step.
inline i32 ramp(i32& current, i32 target) {
    const i32 difference = target - current;
    if (difference > -384 && difference < 384) { current = target; return 0; }
    return floor_bits(difference * 43, 14);
}

// The block is rendered in passes over the output, which holds the Q15 mix
// until the filter replaces it: few values stay live in each loop, so they
// keep to registers. Loops work on local copies, since stores to the output
// could alias the voice.

// The centre and the two side saws at one level; one product for the three.
inline void saws(opxy::Output mix, u32* phases, u32 centre, u32 up, u32 down) {
    u32 a = phases[0], b = phases[1], c = phases[2];
    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        mix[i] = mul15(saw(a, centre) + saw(b, up) + saw(c, down), SAW_LEVEL >> 16);
        a += centre; b += up; c += down;
    }
    phases[0] = a; phases[1] = b; phases[2] = c;
}

// The fifth saw at its smoothed Q23 level.
inline void add_fifth(opxy::Output mix, u32& phase, u32 increment, i32& level, i32 step) {
    u32 p = phase;
    i32 g = level;
    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        g += step;
        mix[i] += mul15(saw(p, increment), g >> 8);
        p += increment;
    }
    phase = p; level = g;
}

// The sub sine an octave down at its smoothed Q23 level.
inline void add_sub(opxy::Output mix, u32& phase, u32 increment, i32& level, i32 step) {
    u32 p = phase;
    i32 g = level;
    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        g += step;
        mix[i] += mul15(sine(p), g >> 8);
        p += increment;
    }
    phase = p; level = g;
}

// Chamberlin state-variable lowpass over the mix, in place; stable for every
// table cutoff and damping (f^2 + 2fq < 4). Saturation bounds high resonance.
// Q17 filter state with Q14 coefficients keeps resonance headroom without a
// 64-bit saturating sum in the sample loop.
inline void filter(opxy::Output mix, Voice& voice, i32 cutoff_step, i32 damping_step) {
    i32 low = voice.low, band = voice.band, cutoff = voice.cutoff, damping = voice.damping;
    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        cutoff += cutoff_step;
        damping += damping_step;
        const i32 f = cutoff >> 9, q = damping >> 9;
        low = clamp17(low + floor_bits(f * band, 14));
        const i32 damped = floor_bits(q * band, 14);
        const i32 high = clamp17(mix[i] * 4 - low - damped - damped);
        band = clamp17(band + floor_bits(f * high, 14));
        mix[i] = clip(floor_bits(low, 2));
    }
    voice.low = low; voice.band = band; voice.cutoff = cutoff; voice.damping = damping;
}
}

// Each note starts its oscillators at spread phases and its filter from
// silence. LFO phases depend on the voice, so chords drift apart.
inline void start(Voice& voice, Shared&, u32 owner) {
    voice.phases[0] = 0;
    voice.phases[1] = 0x55555555u;
    voice.phases[2] = 0xaaaaaaaau;
    voice.phases[3] = 0x2aaaaaaau;
    voice.phases[4] = 0;
    for (u32 k = 0; k < 3; ++k) voice.lfos[k] = owner * 0x9e3779b9u * (k + 1);
    voice.low = voice.band = 0;
    voice.cutoff = voice.damping = voice.sub = voice.fifth = 0;
    voice.initialized = 0;
}
inline void initialize(Voice& voice, Shared& shared, u32 owner) { start(voice, shared, owner); }
inline void destroy(Voice&, Shared&, u32) {}

// Parameters: 0 Spread, 1 Tone, 2 Motion, 3 Sub, then the alternates
// 4 Reso (Tone), 5 Rate (Motion), 6 Fifth (Sub).
// One definition in the SDK's voice translation unit; avoid an out-of-line
// inline-function COMDAT section, which the browser linker does not accept.
void render(Voice& voice, Shared&, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const i32 spread = nonnegative(parameters[0]);
    const i32 motion = nonnegative(parameters[2]);
    // LFOs advance once per block: the sweep at Rate, the drifts at 3/4 and 11/8 of it.
    const i32 rate = lookup(drone_rate, parameters[5]);
    voice.lfos[0] += static_cast<u32>(rate);
    voice.lfos[1] += static_cast<u32>(rate - (rate >> 2));
    voice.lfos[2] += static_cast<u32>(rate + (rate >> 2) + (rate >> 3));

    const i32 sweep = multiply(multiply((sine(voice.lfos[0]) * 65536), motion), SWEEP_DEPTH);
    // The cutoff tracks the note: Tone is the cutoff for a 110 Hz note, scaled
    // by pitch / 110 Hz (2^32 / 401 per sample), up to the table's stable top.
    const i32 tone = lookup(drone_cutoff, saturate(static_cast<i64>(parameters[1]) + sweep));
    // tone * pitch / 2^32 with the Q31 multiply; the device links no 64-bit multiply.
    const i32 scaled = multiply(tone, static_cast<i32>(pitch[0] >> 1));
    const i32 cutoff = scaled > drone_cutoff[16] / 401 ? drone_cutoff[16] : scaled * 401;
    const i32 damping = DRONE_Q_HALF - multiply(nonnegative(parameters[4]), DRONE_Q_SPAN);
    const i32 sub = multiply(nonnegative(parameters[3]), SUB_LEVEL);
    const i32 fifth = multiply(nonnegative(parameters[6]), SAW_LEVEL);
    // Spread is quadratic: slow beating low on the control, chorus at the top.
    // Motion drifts each side saw by up to half its detune.
    const i32 detune = multiply(multiply(spread, spread), DRONE_SPREAD);
    const i32 drift = multiply(motion, (detune >> 1) + DRIFT_FLOOR);
    const i32 upper = detune + multiply((sine(voice.lfos[1]) * 65536), drift);
    const i32 lower = -(detune + multiply((sine(voice.lfos[2]) * 65536), drift));

    // Pitch is read once per block, like the controls.
    const u32 base = pitch[0];
    const u32 up = detuned(base, floor_bits(upper, 16));
    const u32 down = detuned(base, floor_bits(lower, 16));

    if (!voice.initialized) {
        voice.cutoff = cutoff >> 8;
        voice.damping = damping >> 8;
        voice.sub = sub >> 8;
        voice.fifth = fifth >> 8;
        voice.initialized = 1;
    }
    const i32 cutoff_step = ramp(voice.cutoff, cutoff >> 8);
    const i32 damping_step = ramp(voice.damping, damping >> 8);
    const i32 sub_step = ramp(voice.sub, sub >> 8);
    const i32 fifth_step = ramp(voice.fifth, fifth >> 8);

    saws(output, voice.phases, base, up, down);
    // A silent fifth or sub only keeps its phase moving.
    if (voice.fifth || fifth_step) add_fifth(output, voice.phases[3], base + (base >> 1), voice.fifth, fifth_step);
    else voice.phases[3] += (base + (base >> 1)) * opxy::block_frames;
    if (voice.sub || sub_step) add_sub(output, voice.phases[4], base >> 1, voice.sub, sub_step);
    else voice.phases[4] += (base >> 1) * opxy::block_frames;
    filter(output, voice, cutoff_step, damping_step);
}

} // namespace drone
