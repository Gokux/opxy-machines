#include "core.h"
#include <opxy/q31.hpp>
#include "tables.h"

namespace papasrapa {
namespace {

using opxy::q31::multiply;

const i32 FULL = opxy::q31::maximum;
const i32 NOISE_LEVEL = 0x3851eb85; // 0.44
const u32 HIGHEST = 0x40000000u;   // the tone stops rising at 11 kHz

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

// (1 - x)^2 at half scale for x = distance / increment, the Q15 PolyBLEP residual
// of a unit step; zero once the edge is a sample away.
inline i32 residual(u32 distance, u32 increment) {
    const u32 step = increment >> 16;
    if (!step || distance >= increment) return 0;
    const u32 x = 65535u - minimum(distance / step, 65535u);
    return static_cast<i32>((x * x) >> 18);
}

// The Schmitt-trigger oscillator's output: a band-limited square at half scale.
inline i32 square(u32 phase, u32 increment) {
    i32 value = phase < 0x80000000u ? 16384 : -16384;
    // Rising edge at 0, falling edge at one half.
    value -= residual(phase, increment);
    value += residual(0u - phase - 1u, increment);
    value += residual(phase - 0x80000000u, increment);
    value -= residual(0x7fffffffu - phase, increment);
    return value;
}
}

inline void start(Voice& voice, Shared&, u32 owner) {
    voice.seed = owner * 0x9e3779b9u | 1u;
    voice.phase = voice.sub_phase = 0;
    voice.lfo_phase = 0;
    voice.glide = voice.held = 0;
    voice.amplitude = 1 << 19;  // Q19 AM smoothing state
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
    const i32 mod = nonnegative(parameters[1]) >> 16;
    // Divider 1..16: the tone's copy counted down by that many cycles.
    const u32 divider = 1 + ((static_cast<u32>(nonnegative(parameters[2])) >> 16) * 16 >> 15);
    // Sixteen possible divisors: avoid software division in block setup.
    static const i32 reciprocals[16] = {
        2147483647, 1073741823, 715827882, 536870911,
        429496729, 357913941, 306783378, 268435455,
        238609294, 214748364, 195225786, 178956970,
        165191049, 153391689, 143165576, 134217727
    };
    const i32 reciprocal = reciprocals[divider - 1];
    const i32 noise = mul15(nonnegative(parameters[3]) >> 16, NOISE_LEVEL >> 16);
    const u32 mode = static_cast<u32>(nonnegative(parameters[5])) >> 29;
    const bool fm = mode & 1u, am = (mode & 2u) != 0;
    const bool low = parameters[6] >= 0x40000000;
    const i32 sample_hold = nonnegative(parameters[7]) >> 16;
    // The LFO square charges the tone's timing capacitor: the jump becomes a
    // swoop with a time constant of 0.15 / rate, so slow rates sound like sirens.
    const i32 follow = static_cast<i32>(rate * 3 + rate / 3);

    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        voice.lfo_phase += rate;
        // A rising LFO edge clocks the sample-and-hold.
        if (voice.lfo_phase < rate) voice.held = static_cast<i32>(next(voice.seed) >> 17) - 16384;
        const i32 gate = voice.lfo_phase < 0x80000000u ? FULL : 0;
        voice.glide += multiply(gate - voice.glide, follow);

        // Q12 pitch ratio: enough headroom for FM and sample-and-hold together.
        i32 ratio = 4096;
        if (fm) ratio += mul15(mul15(voice.glide >> 16, mod), 12288); // +3 in Q12
        ratio = mul15(ratio, 32768 + mul15(mul15(voice.held, sample_hold), 49152)); // 1.5 in Q15
        const u32 base = low ? pitch[i] >> 2 : pitch[i];
        const u32 product = (base >> 16) * static_cast<u32>(ratio)
            + ((base & 65535u) * static_cast<u32>(ratio) >> 16);
        const u32 increment = product >= HIGHEST >> 4 ? HIGHEST : product << 4;

        i32 tone = square(voice.phase, increment);
        if (divider > 1) {
            const u32 divided = static_cast<u32>(multiply(static_cast<i32>(increment >> 1), reciprocal)) << 1;
            tone = floor_bits(tone, 1) + floor_bits(square(voice.sub_phase, divided), 1);
            voice.sub_phase += divided;
        }
        voice.phase += increment;

        const i32 target = am && !gate ? (32768 - mod) * 16 : 1 << 19;
        voice.amplitude += floor_bits((target - voice.amplitude) * 2289, 15); // ~0.3 ms AM edges
        const i32 hiss = mul15(floor_bits(static_cast<i32>(next(voice.seed)), 16), noise);
        output[i] = clip(mul15(floor_bits(tone, 1) + hiss, voice.amplitude >> 4));
    }
}

} // namespace papasrapa
