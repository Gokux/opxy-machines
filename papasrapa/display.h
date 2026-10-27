#pragma once
#include <opxy/draw.hpp>

namespace papasrapa {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// A transistor radio: the output wave in the tuning window, the tuning
// needle, the fm and am band buttons, the Rate knob's pointer, the LFO lamp,
// the power LED and four telescopic antenna segments.
using Frame = opxy::draw::Frame<1, 64, 11, 0, 9>;
struct DisplayState {
    opxy::u32 scroll, lfo_phase, seed, held, frames, previous_event, track, initialized;
    opxy::i32 flash_age;
};
}
