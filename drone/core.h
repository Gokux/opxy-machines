#pragma once
#include <opxy/audio.hpp>

namespace drone {
using opxy::i32;
using opxy::u32;

// Only sound state. The SDK owns firmware headers, slot padding and allocation.
struct Voice {
    u32 phases[5];      // centre, upper and lower saw, fifth saw, sub sine
    u32 lfos[3];        // cutoff sweep and the two detune drifts
    i32 low, band;      // state-variable filter
    i32 cutoff, damping, sub, fifth;  // smoothed controls
    u32 initialized;
};
// Drone voices share nothing.
struct Shared {};
}
