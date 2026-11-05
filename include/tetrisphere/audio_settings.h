#pragma once

// Audio settings (docs/UI.md, Options and the pause menu's Audio). The
// volume is applied to the PC's audio output; music and effects levels and
// the music choice go to the game, on its own thread
// (src/graphics/audio_hooks.cpp).

namespace blockplanet {

// 0..100 percent, the PC's output.
void set_audio_volume(int percent);
int audio_volume();

// The game's music and effects levels, 0..100 percent each, kept applied
// (the game resets the music level when a song starts).
void set_audio_levels(int music_percent, int effects_percent);

// How the game picks music: random (true) or automatic (false). Ends a
// track override.
void set_music_random(bool random);
// Pause menu: play this track now and keep choosing it (the game's CHOOSE),
// until set_music_random ends the override.
void play_track(int track);
// The track the game is set to (0..22), and whether an override is on.
int current_track(const unsigned char* rdram);
bool track_override();

constexpr int track_count = 23;
// Track names, in the game's order (its Audio page; names from the ROM).
const char* track_name(int track);

} // namespace blockplanet
