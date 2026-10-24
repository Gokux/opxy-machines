#include "../display.cpp"
#include <cstring>

// Exercise the retained-frame contract with every combination of control
// extremes, animation, note starts, track changes and reduced motion.
int main() {
    using namespace solar;
    for (opxy::u32 mask = 0; mask < 256; ++mask) {
        DisplayState state{};
        solar::Frame frame{};
        opxy::draw::Input input{};
        display_reset(state);
        for (opxy::u32 slot = 0; slot < opxy::parameter_slots; ++slot)
            input.slots[slot] = mask & (1u << slot) ? 0x7fffffff : 0;
        for (opxy::u32 step = 0; step < 128; ++step) {
            input.elapsed = step % 3 == 0 ? 102 : 17;
            input.track = step / 32;
            input.note_serial = step / 7;
            input.reduced_motion = step >= 96;
            for (opxy::u32 position = 0; position < opxy::page_controls; ++position) {
                input.shown[position] = static_cast<opxy::u8>(position);
                input.parameters[position] = input.slots[position];
            }
            display(state, input, frame);
            if (!opxy::draw::valid(frame)) return 1;
        }
    }
    // The header names every Chord and Mute choice, from the middle of each
    // choice as presets store it, and keeps the stock value elsewhere.
    for (opxy::u32 choice = 0; choice < 8; ++choice) {
        DisplayState state{};
        solar::Frame frame{};
        opxy::draw::Input input{};
        display_reset(state);
        const opxy::i32 word = static_cast<opxy::i32>((choice * 2 + 1) * 0x08000000u);
        input.slots[0] = input.slots[3] = word;
        input.slots[4] = 0x7fffffff;
        for (opxy::u32 position = 0; position < opxy::page_controls; ++position) input.shown[position] = static_cast<opxy::u8>(position);
        display(state, input, frame);
        if (std::strcmp(frame.header[0], solar_chord_labels[choice]) || std::strcmp(frame.header[3], solar_mute_labels[choice]) ||
            frame.header[1][0] || frame.header[2][0]) return 2;
        input.shown[0] = 4;   // Drift, the first position's alternate
        display(state, input, frame);
        if (frame.header[0][0]) return 3;
    }
    return 0;
}
