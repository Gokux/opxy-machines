#pragma once
#include <opxy/draw.hpp>

namespace papasrapa {
// Matches resources.display in definition.json: paths, points, nodes, palette.
// A megaphone: three sound arcs and the sample-and-hold bars as outlines, then
// the horn, the fm and am switch LEDs, twelve noise specks and the LFO lamp.
using Frame = opxy::draw::Frame<4, 64, 20, 9>;
struct DisplayState {
    opxy::u32 phase, lfo_phase, seed, previous_event, track, initialized;
    opxy::i32 flash_age;
    opxy::u32 bars[4];   // sixteen held heights, one byte each
};
}
