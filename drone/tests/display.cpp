#include "../display.cpp"
#include <string.h>

// Exercise the retained-frame contract with every combination of control
// extremes, animation, note starts, page switches, track changes and reduced motion.
int main() {
    using namespace drone;
    const opxy::u8 shown[2][4] = {{0, 1, 2, 3}, {0, 4, 5, 6}};
    for (opxy::u32 mask = 0; mask < 256; ++mask) {
        DisplayState state{};
        drone::Frame frame{};
        opxy::draw::Input input{};
        display_reset(state);
        for (opxy::u32 slot = 0; slot < opxy::parameter_slots; ++slot)
            input.slots[slot] = mask & (1u << slot) ? 0x7fffffff : 0;
        for (opxy::u32 step = 0; step < 128; ++step) {
            input.elapsed = step % 3 == 0 ? 102 : 17;
            input.track = step / 32;
            input.note_serial = step / 7;
            input.reduced_motion = step >= 96;
            const opxy::u32 page = step / 5 % 2;
            for (opxy::u32 position = 0; position < opxy::page_controls; ++position) {
                input.shown[position] = shown[page][position];
                input.parameters[position] = input.slots[shown[page][position]];
            }
            display(state, input, frame);
            if (!opxy::draw::valid(frame)) return 1;
        }
    }
    // Settled on page 2: the bar sits under page 1, page 2's bright titles
    // lead with Spread again, and page 1 is the grey row.
    DisplayState state{};
    drone::Frame frame{};
    opxy::draw::Input input{};
    display_reset(state);
    for (opxy::u32 position = 0; position < opxy::page_controls; ++position) input.shown[position] = shown[1][position];
    for (opxy::u32 step = 0; step < 4; ++step) {
        input.elapsed = 102;
        display(state, input, frame);
    }
    if (frame.nodes[bar].y != 18 * opxy::draw::unit) return 2;
    if (strcmp(frame.texts[titles].chars, "spread") || strcmp(frame.texts[titles + 1].chars, "reso")) return 3;
    if (strcmp(frame.texts[grey_titles + 1].chars, "tone")) return 4;
    return 0;
}
