#include "../display.cpp"
#include <cstring>

// Exercise the retained-frame contract with every combination of control
// extremes, animation, page switches, note starts, track changes and reduced
// motion.
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
            // Page 2 (Drift, Volt, Flicker, Mute) on every fifth step.
            const opxy::u32 page = step / 5 % 2;
            for (opxy::u32 position = 0; position < opxy::page_controls; ++position) {
                input.shown[position] = static_cast<opxy::u8>(page && position % 2 == 0 ? 4 + position / 2 : position);
                input.parameters[position] = input.slots[input.shown[position]];
            }
            display(state, input, frame);
            if (!opxy::draw::valid(frame)) return 1;
        }
    }
    // The header names every Chord and Mute choice, from the middle of each
    // choice as presets store it, and shows the continuous controls 0 to 100.
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
            std::strcmp(frame.header[1], "0") || std::strcmp(frame.header[2], "0")) return 2;
        // Page 1 is bright on top, page 2 grey under it.
        if (std::strcmp(frame.texts[0].chars, "chord") || std::strcmp(frame.texts[4].chars, solar_chord_labels[choice]) ||
            std::strcmp(frame.texts[8].chars, "drift") || std::strcmp(frame.texts[12].chars, "100") ||
            std::strcmp(frame.texts[13].chars, "0") || std::strcmp(frame.texts[15].chars, solar_mute_labels[choice])) return 3;
        input.shown[0] = 4;   // Drift, the first position's alternate: page 2
        input.shown[2] = 5;
        input.reduced_motion = 1;
        display(state, input, frame);
        if (std::strcmp(frame.header[0], "100") || std::strcmp(frame.header[3], solar_mute_labels[choice])) return 4;
        // Page 2 is now bright, moved into the bar; page 1 grey on top.
        if (std::strcmp(frame.texts[0].chars, "drift") || std::strcmp(frame.texts[2].chars, "flicker") ||
            std::strcmp(frame.texts[8].chars, "chord") || frame.texts[0].y != 28 << 10 || frame.texts[8].y != 13 << 10 ||
            frame.nodes[86].y != 18 << 10) return 5;
    }
    return 0;
}
