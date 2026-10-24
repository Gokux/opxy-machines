#pragma once
#include <opxy/draw.hpp>

namespace drone {
// Matches resources.display in definition.json: paths, points, nodes, palette.
// A quadcopter seen from above: four spinning blades, then rotor guards,
// hubs, arms, body, status LED and the ground shadow.
using Frame = opxy::draw::Frame<4, 64, 24, 9>;
struct DisplayState {
    opxy::u32 spin[4];   // blade angles, 65536ths of a turn
    opxy::u32 hover;     // hover phase, 65536ths of a cycle
    opxy::u32 previous_event, track, initialized;
    opxy::i32 led_age;   // Q10 seconds since the last note start
};
}
