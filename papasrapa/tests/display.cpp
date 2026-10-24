#include "../display.cpp"

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
    return 0;
}
