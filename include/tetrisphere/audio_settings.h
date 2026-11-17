#pragma once

// Audio settings (docs/UI.md, Options and the pause menu's Audio): music
// and effects levels and the music choice, applied to the game on its own
// thread (src/graphics/audio_hooks.cpp). The overall level is the PC's.

namespace blockplanet {

// The game's music and effects levels, 0..100 percent each, kept applied
// (the game resets the music level when a song starts).
void set_audio_levels(int music_percent, int effects_percent);
// The jukebox's Stop: silences the music without changing the saved level.
void set_music_muted(bool muted);
bool music_muted();

// How the game picks music: random (true) or automatic (false). Ends a
// track override.
void set_music_random(bool random);
// Pause menu: play this track now and keep choosing it (the game's CHOOSE),
// until set_music_random ends the override.
void play_track(int track);
// Play one of the game's songs by its id now (0x100 and up: songs outside the
// 23 tracks), leaving the game's music choice alone.
void play_song(int song);
// Stop the song play_song started (a game is starting).
void stop_song();
// The extra song playing (its id), or -1.
int extra_song();
// The track the game is set to (0..22), and whether an override is on.
int current_track(const unsigned char* rdram);
bool track_override();

constexpr int track_count = 23;
// Track names, in the game's order (its Audio page; names from the ROM).
const char* track_name(int track);

} // namespace blockplanet
