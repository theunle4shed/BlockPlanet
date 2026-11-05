#pragma once

// Audio settings (docs/UI.md, Options). The volume is applied to the PC's
// audio output; the balance and music choice are the game's own Audio page
// settings, applied on the game's thread (src/graphics/audio_hooks.cpp).

namespace blockplanet {

// 0..100 percent.
void set_audio_volume(int percent);
int audio_volume();

// The game's settings: balance 0 (music only) .. 6 (both full) .. 12 (effects
// only); music -2 auto, -1 random, 0..22 a chosen track. They are applied,
// and kept applied, from the next frame.
void set_game_audio(int balance, int music);

// The game's current values (for the Options screen before the player has
// changed anything).
struct GameAudio {
    int balance = 6;
    int music = -2;
};
GameAudio read_game_audio(const unsigned char* rdram);

// Track names, in the game's order (its Audio page; names from the ROM).
constexpr int track_count = 23;
const char* track_name(int track);

} // namespace blockplanet
