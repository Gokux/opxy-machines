#pragma once
#include <opxy/audio.hpp>

namespace solar {
using opxy::i32;
using opxy::u32;

// Only sound state. The SDK owns firmware headers, slot padding and allocation.
struct Voice {
    u32 phases[5];
    i32 outputs[5];       // Q15 last samples, read back for cross-modulation
    i32 drift[5], drift_targets[5];
    i32 tolerance[5];     // this voice's fixed component deviation
    i32 gains[5];         // Q23 smoothed mute levels
    i32 light, light_target;
    u32 light_clock, seed;
    i32 fm;              // Q23 smoothed cross-FM depth
    u32 initialized;
};
// Solar voices share nothing.
struct Shared {};
}
