// Voicings and mute patterns, shared by engine.cpp and display.cpp. Row order
// follows the Chord and Mute labels in definition.json.
#pragma once
#include <opxy/types.hpp>
// Five oscillators as on the Solar's drone voices: two low, one middle and two
// high. Semitones from the played note.
static const opxy::i32 solar_chords[8][5] = {
  {-12, -12, 0, 12, 12},   // Oct
  {-12, -5, 0, 7, 12},     // Fifth
  {-12, -5, 0, 15, 19},    // Minor
  {-12, -5, 0, 16, 19},    // Major
  {-12, -7, 0, 14, 17},    // Sus
  {-12, -6, 0, 6, 18},     // Trit
  {-12, -11, 0, 13, 14},   // Clust
  {-24, -12, 0, 12, 24},   // Stack
};
// Bit n sounds oscillator n + 1, like the five mute buttons.
static const opxy::u32 solar_mutes[8] = {0x1f, 0x07, 0x1c, 0x15, 0x0a, 0x03, 0x18, 0x04};
