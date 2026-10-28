#pragma once
#include <opxy/draw.hpp>

namespace papasrapa {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// A transistor radio: the output wave in the tuning window, the tuning
// needle, the fm and am band buttons, the Rate knob's pointer, the LFO lamp,
// the power LED and four telescopic antenna segments; then the eight steps of
// the header bar. Sixteen text slots draw both pages over the stock header:
// four titles and four values in the bar's colours for the page being edited,
// then four of each in grey for the other.
using Frame = opxy::draw::Frame<1, 64, 19, 16, 9>;
struct DisplayState {
    opxy::u32 scroll, lfo_phase, seed, held, frames, previous_event, track, initialized;
    opxy::i32 flash_age;
    opxy::u32 page;    // the page being edited, plus one; 0 before the first frame
    opxy::i32 since;   // Q10 seconds since the page last switched
};
}
