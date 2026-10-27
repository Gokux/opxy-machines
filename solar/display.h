#pragma once
#include <opxy/draw.hpp>

namespace solar {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// A space station in the sun: the sun's corona and disc, the station's truss,
// modules and wing masts, forty solar cells (a wing pair of eight per
// oscillator), thirty-two speckles of radiation noise over everything and
// the eight steps of the header bar. Sixteen text slots draw both pages over
// the stock header: four titles and four values in the bar's colours for the
// page being edited, then four of each in grey for the other.
using Frame = opxy::draw::Frame<1, 64, 94, 16, 9>;
struct DisplayState {
    opxy::u32 phase, light_clock, seed, previous_event, track, initialized;
    opxy::i32 light, light_target, flash_age;
    opxy::u32 page;    // the page being edited, plus one; 0 before the first frame
    opxy::u32 page_track;
    opxy::i32 since;   // Q10 seconds since the page last switched
};
}
