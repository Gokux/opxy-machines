// Sound lookup tables, shared by engine.cpp and display.cpp.
#pragma once
#include <opxy/types.hpp>
// Rate 0..1 in 16 steps: LFO phase increment per sample, 0.1 Hz * 200^rate
// (0.1..20 Hz; the x10 range multiplies it).
static const opxy::i32 papasrapa_rate[17] = {
  9739, 13562, 18886, 26301, 36625, 51003, 71024, 98906,
  137732, 191801, 267095, 371946, 517957, 721287, 1004437, 1398740,
  1947831,
};
// The header's names for the enum controls, as definition.json labels them.
static const char* const papasrapa_range_labels[2] = {"x1", "x10"};
static const char* const papasrapa_mode_labels[4] = {"Drone", "FM", "AM", "Both"};
static const char* const papasrapa_octave_labels[2] = {"Hi", "Low"};
// The same rates in Hz, Q10, for the display.
static const opxy::i32 papasrapa_rate_hz[17] = {
  102, 143, 199, 277, 385, 536, 747, 1040,
  1448, 2017, 2808, 3911, 5446, 7584, 10561, 14707,
  20480,
};
