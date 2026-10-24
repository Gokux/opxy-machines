#pragma once
#include <opxy/draw.hpp>

namespace solar {
// Matches resources.display in definition.json: paths, points, nodes, palette.
// A sun setting behind the horizon (Volt) with a flickering corona (Mod), the
// five oscillators as status LEDs on a pitch staff, and a negistor waveform.
using Frame = opxy::draw::Frame<2, 64, 8, 9>;
struct DisplayState {
    opxy::u32 phase, light_clock, seed, previous_event, track, initialized;
    opxy::i32 light, light_target, flash_age;
};
}
