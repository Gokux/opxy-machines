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
// Output level, Q31: the soft clipper's ceiling, a quarter of full scale
// (-12 dB). The declared output peak in definition.json is this level.
const i32 OUTPUT_PEAK = 0x20000000;

inline i32 nonnegative(i32 value) { return value < 0 ? 0 : value; }
inline u32 minimum(u32 a, u32 b) { return a < b ? a : b; }
inline i32 saturate(i64 value) {
    return value > opxy::q31::maximum ? opxy::q31::maximum
         : value < -opxy::q31::maximum ? -opxy::q31::maximum : static_cast<i32>(value);
}

inline i32 sine(u32 phase) {
    const u32 index = phase >> 24;
    const i32 fraction = static_cast<i32>((phase & 0xffffffu) << 7);
    return drone_sine[index] + multiply(drone_sine[index + 1] - drone_sine[index], fraction);
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
    i32 value = static_cast<i32>(phase - 0x80000000u);
    const u32 step = increment >> 16;
    if (step && increment < 0x80000000u) {
        // (1 - x)^2 with x in Q16; x * x >> 1 is that square in Q31.
        if (phase < increment) {
            const u32 x = 65535u - minimum(phase / step, 65535u);
            value += static_cast<i32>((x * x) >> 1);
        } else if (0u - phase < increment) {
            const u32 x = 65535u - minimum((0u - phase) / step, 65535u);
            value -= static_cast<i32>((x * x) >> 1);
        }
    }
    return value;
}

// Cubic soft clip 1.5x - 0.5x^3, formed at half scale so it cannot overflow.
inline i32 clip(i32 x) {
    const i32 cube = multiply(multiply(x, x), x);
    const i32 shaped = multiply(x, 0x60000000) - multiply(cube, 0x20000000);
    return multiply(shaped, OUTPUT_PEAK << 1);
}

inline u32 detuned(u32 increment, i32 amount) {
    return increment + (static_cast<u32>(multiply(static_cast<i32>(increment >> 1), amount)) << 1);
}

inline void smooth(i32& current, i32 target) {
    current += multiply(target - current, DRONE_SMOOTH);
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
inline void render(Voice& voice, Shared&, u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const i32 spread = nonnegative(parameters[0]);
    const i32 motion = nonnegative(parameters[2]);
    // LFOs advance once per block: the sweep at Rate, the drifts at 3/4 and 11/8 of it.
    const i32 rate = lookup(drone_rate, parameters[5]);
    voice.lfos[0] += static_cast<u32>(rate);
    voice.lfos[1] += static_cast<u32>(rate - (rate >> 2));
    voice.lfos[2] += static_cast<u32>(rate + (rate >> 2) + (rate >> 3));

    const i32 sweep = multiply(multiply(sine(voice.lfos[0]), motion), SWEEP_DEPTH);
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
    const i32 upper = detune + multiply(sine(voice.lfos[1]), drift);
    const i32 lower = -(detune + multiply(sine(voice.lfos[2]), drift));

    if (!voice.initialized) {
        voice.cutoff = cutoff;
        voice.damping = damping;
        voice.sub = sub;
        voice.fifth = fifth;
        voice.initialized = 1;
    }
    for (unsigned i = 0; i != opxy::block_frames; ++i) {
        smooth(voice.cutoff, cutoff);
        smooth(voice.damping, damping);
        smooth(voice.sub, sub);
        smooth(voice.fifth, fifth);
        const u32 base = pitch[i];
        const u32 up = detuned(base, upper);
        const u32 down = detuned(base, lower);
        const u32 interval = base + (base >> 1);
        const i32 mix = multiply(saw(voice.phases[0], base), SAW_LEVEL)
                      + multiply(saw(voice.phases[1], up), SAW_LEVEL)
                      + multiply(saw(voice.phases[2], down), SAW_LEVEL)
                      + multiply(saw(voice.phases[3], interval), voice.fifth)
                      + multiply(sine(voice.phases[4]), voice.sub);
        // Chamberlin state-variable lowpass; stable for every table cutoff and
        // damping (f^2 + 2fq < 4). Saturation bounds high resonance.
        voice.low = saturate(static_cast<i64>(voice.low) + multiply(voice.cutoff, voice.band));
        const i32 damped = multiply(voice.damping, voice.band);
        const i32 high = saturate(static_cast<i64>(mix) - voice.low - damped - damped);
        voice.band = saturate(static_cast<i64>(voice.band) + multiply(voice.cutoff, high));
        output[i] = clip(voice.low);
        voice.phases[0] += base;
        voice.phases[1] += up;
        voice.phases[2] += down;
        voice.phases[3] += interval;
        voice.phases[4] += base >> 1;
    }
}

} // namespace drone
