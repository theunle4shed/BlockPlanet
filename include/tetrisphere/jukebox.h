#pragma once

// The jukebox (docs/UI.md, Media Player): a Winamp-like player over the menu
// grid, for the game's 23 tracks. Its screens are jukebox.rml (the player)
// and jukebox_list.rml (the playlist).

#include <string>

namespace blockplanet::jukebox {

// Handles a jukebox menu command ("jukebox", "jukebox_play:N", "jukebox_next"
// ...); false when the command is not the jukebox's. `rdram` may be null.
bool command(const std::string& command, const unsigned char* rdram);
// Once a frame from the main loop: the clock text, and moving on to the next
// track when Repeat is off (at the end of a loop, read from `rdram`; may be null).
void tick(const unsigned char* rdram);
// A game is starting: the music comes back if it was stopped.
void on_launch();

} // namespace blockplanet::jukebox
