#pragma once
#include <opxy/audio.hpp>

namespace papasrapa {
using opxy::i32;
using opxy::u32;

// Only sound state. The SDK owns firmware headers, slot padding and allocation.
struct Voice {
    u32 phase, sub_phase;   // tone oscillator and its divided copy
    u32 lfo_phase, seed;
    i32 glide;              // LFO square through the timing capacitor, 0..1
    i32 amplitude;          // smoothed AM gain
    i32 held;               // sample-and-hold noise, -0.5..0.5
};
// Voices share nothing.
struct Shared {};
}
