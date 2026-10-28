#pragma once
#include <opxy/draw.hpp>

namespace drone {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// A quadcopter seen from above: four spinning blades, then rotor guards,
// hubs, arms, body, status LED and the ground shadow; then the eight steps of
// the header bar. Sixteen text slots draw both pages over the stock header:
// four titles and four values in the bar's colours for the page being edited,
// then four of each in grey for the other.
using Frame = opxy::draw::Frame<4, 64, 32, 16, 9>;
struct DisplayState {
    opxy::u32 spin[4];   // blade angles, 65536ths of a turn
    opxy::u32 hover;     // hover phase, 65536ths of a cycle
    opxy::u32 previous_event, track, initialized;
    opxy::i32 led_age;   // Q10 seconds since the last note start
    opxy::u32 page;      // the page being edited, plus one; 0 before the first frame
    opxy::i32 since;     // Q10 seconds since the page last switched
};
}
