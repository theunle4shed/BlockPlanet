#pragma once

// The music's spectrum for the jukebox (the owner's Winamp-inspired player,
// 2026-11-17): the game's mixed output (stereo 16-bit, as queued to the PC's
// audio) is kept in a short ring; any thread can ask for its bands, from bass
// to treble, each 0 to 1, smoothed so they rise fast and fall slowly.

#include <array>
#include <cstddef>
#include <cstdint>

namespace tetrisphere::audio_spectrum {

constexpr std::size_t band_count = 16;

// The samples as queued (interleaved stereo).
void push(const std::int16_t* samples, std::size_t count);
// The bands now (smoothed between calls) and the overall level (0 to 1).
std::array<float, band_count> bands();
float level();

// For tests: the bands of a mono block (no smoothing), 512 samples at 44.1 kHz.
std::array<float, band_count> analyse(const float* mono, std::size_t count, int frequency = 44100);

} // namespace tetrisphere::audio_spectrum
