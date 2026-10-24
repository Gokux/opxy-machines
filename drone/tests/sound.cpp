#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

// Preset levels measured before the optimization, at repository commit
// 487e233, over 1024 blocks (first 128 discarded), owner 1, MIDI 36/60/84.
// Ten percent allows the deliberate fixed-point quantization, while catching
// lost oscillators, a broken filter, wrong gains or changes to preset levels.
static const opxy::i32 presets[][8] = {
    {536870912, 751619276, 644245094, 1288490188, 429496729, 644245094, 0},
    {1288490188, 1181116006, 1073741824, 644245094, 751619276, 858993459, 858993459},
    {858993459, 322122547, 1288490188, 2147483647, 1073741824, 322122547, 0},
    {1932735282, 1503238553, 1825361100, 1073741824, 1503238553, 1395864371, 1288490188},
    {429496729, 1181116006, 150323855, 1073741824, 2147483647, 751619276, 2147483647}
};
static const double original_rms[][3] = {
    {0.04955675, 0.05424661, 0.06527312},
    {0.05522513, 0.06459412, 0.06234834},
    {0.05952815, 0.05967940, 0.06063944},
    {0.07303379, 0.07566791, 0.07571737},
    {0.10356719, 0.10046111, 0.09874502}
};

static opxy::u32 increment(unsigned note) {
    return static_cast<opxy::u32>(std::pow(2.0, (note - 69.0) / 12) * 440 * 4294967296.0 / 44100);
}

int main() {
    using namespace drone;
    // Every combination of control endpoints, including live jumps and pitch
    // modulation within a block, from MIDI 0 through the highest MIDI note.
    const unsigned notes[] = {0, 36, 60, 96, 127};
    for (unsigned mask = 0; mask < (1u << 7); ++mask) {
        for (unsigned note : notes) {
            Voice voice{}; Shared shared{};
            initialize(voice, shared, 1);
            opxy::i32 parameters[8]{};
            opxy::u32 pitch[64];
            struct Guarded { opxy::i32 before, samples[64], after; } output{};
            output.before = 12345; output.after = 67890;
            for (unsigned block = 0; block < 64; ++block) {
                const unsigned active = block < 32 ? mask : ~mask;
                for (unsigned slot = 0; slot < 7; ++slot)
                    parameters[slot] = active & (1u << slot) ? 0x7fffffff : 0;
                for (unsigned i = 0; i < 64; ++i)
                    pitch[i] = increment(note) + (i < 32 ? 0 : increment(note) / 100);
                opxy::i32 saved[8]; opxy::u32 saved_pitch[64];
                std::memcpy(saved, parameters, sizeof(saved));
                std::memcpy(saved_pitch, pitch, sizeof(saved_pitch));
                render(voice, shared, 1, output.samples, parameters, pitch);
                if (output.before != 12345 || output.after != 67890 ||
                    std::memcmp(saved, parameters, sizeof(saved)) ||
                    std::memcmp(saved_pitch, pitch, sizeof(saved_pitch))) return 1;
                for (opxy::i32 sample : output.samples)
                    if (sample < -536870912 || sample > 536870912) return 2;
            }
            destroy(voice, shared, 1);
        }
    }
    for (unsigned preset = 0; preset < sizeof(presets) / sizeof(presets[0]); ++preset) {
        for (unsigned octave = 0; octave < 3; ++octave) {
            Voice voice{}; Shared shared{};
            initialize(voice, shared, 1);
            opxy::u32 pitch[64]; opxy::i32 output[64], first[64];
            for (opxy::u32& word : pitch) word = increment(36 + octave * 24);
            render(voice, shared, 1, first, presets[preset], pitch);
            start(voice, shared, 1);
            render(voice, shared, 1, output, presets[preset], pitch);
            if (std::memcmp(first, output, sizeof(first))) return 3;
            start(voice, shared, 1);
            double energy = 0;
            for (unsigned block = 0; block < 1024; ++block) {
                render(voice, shared, 1, output, presets[preset], pitch);
                if (block < 128) continue;
                for (opxy::i32 sample : output) {
                    const double value = sample / 2147483648.0;
                    energy += value * value;
                }
            }
            const double rms = std::sqrt(energy / 57344);
            const double reference = original_rms[preset][octave];
            if (!(rms > reference * 0.9 && rms < reference * 1.1)) {
                std::fprintf(stderr, "preset %u octave %u RMS %.8f, original %.8f\n", preset, octave, rms, reference);
                return 4;
            }
        }
    }
    
    return 0;
}
