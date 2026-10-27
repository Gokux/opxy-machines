#pragma once
#include <opxy/draw.hpp>

namespace solar {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// A space station in the sun: the sun's corona and disc, the station's truss,
// modules and wing masts, forty solar cells (a wing pair of eight per
// oscillator) and thirty-two speckles of radiation noise over everything.
using Frame = opxy::draw::Frame<1, 64, 86, 0, 9>;
struct DisplayState {
    opxy::u32 phase, light_clock, seed, previous_event, track, initialized;
    opxy::i32 light, light_target, flash_age;
};
}
