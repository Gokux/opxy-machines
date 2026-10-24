#include "../display.cpp"
#include <cstring>

// Exercise the retained-frame contract with every combination of control
// extremes, animation, note starts, track changes and reduced motion.
int main() {
    using namespace papasrapa;
    for (opxy::u32 mask = 0; mask < 256; ++mask) {
        DisplayState state{};
        papasrapa::Frame frame{};
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
    // With the alternates shown, the header names every Range, Mode and Octave
    // choice from the middle of each choice; the own controls keep the stock value.
    for (opxy::u32 choice = 0; choice < 4; ++choice) {
        DisplayState state{};
        papasrapa::Frame frame{};
        opxy::draw::Input input{};
        display_reset(state);
        input.slots[4] = input.slots[6] = static_cast<opxy::i32>(((choice & 1) * 2 + 1) * 0x20000000u);
        input.slots[5] = static_cast<opxy::i32>((choice * 2 + 1) * 0x10000000u);
        for (opxy::u32 position = 0; position < opxy::page_controls; ++position) input.shown[position] = static_cast<opxy::u8>(position);
        display(state, input, frame);
        for (opxy::u32 position = 0; position < opxy::page_controls; ++position) if (frame.header[position][0]) return 2;
        input.shown[0] = 4; input.shown[1] = 5; input.shown[2] = 6;
        display(state, input, frame);
        if (std::strcmp(frame.header[0], papasrapa_range_labels[choice & 1]) || std::strcmp(frame.header[1], papasrapa_mode_labels[choice]) ||
            std::strcmp(frame.header[2], papasrapa_octave_labels[choice & 1]) || frame.header[3][0]) return 3;
    }
    return 0;
}
