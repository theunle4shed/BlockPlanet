#pragma once

// Starts the game's modes directly, without the original menus
// (docs/research/blockplanet-game-state.md, "Starting modes directly").
//
// The original menus end a mode's start in the same way the game's own
// Training jump does (func_80070118 at 0x80070288): set the mode and the
// player's context, then call the new-game routine func_800CC6E0 and set the
// menu rows to -1. A hook at that point, on the game's thread, does the same
// for the mode BlockPlanet asked for, with the values the original mode page
// sets (func_8004127C at 0x800462D0) and its save calls in the same order, so
// the menus' pages, fly-out, sounds and timing never run.
//
// BlockPlanet asks from its own thread (request), and learns the outcome on
// a later pass (take_result). A request that cannot be honoured (the title
// still showing, an original page open or closing, a save check taking the
// menu's other branch) is refused, and the caller falls back to ModeLauncher.

#include <cstdint>
#include <filesystem>
#include <optional>

namespace tetrisphere {

enum class DirectMode { Rescue, Training, Puzzle, Lines, HideAndSeek, Versus };

struct DirectStartRequest {
    DirectMode mode = DirectMode::Rescue;
    int chapter = 0;  // 0-based (the game's 0x800E44A0)
    int level = 0;    // 0-based level within the chapter (Puzzle: the puzzle)
    int training_type = 0;  // Training: 1 Basics, 2 Advanced, 3 Puzzle
};

namespace direct_start {

enum class Result { Started, Refused };

// Queues a start; the game's thread carries it out at its next frame in the
// menus. Returns false if one is already waiting.
bool request(const DirectStartRequest& request);
bool pending();
// The outcome of the last request, once (empty while it waits).
std::optional<Result> take_result();
// Gives up a request the game has not reached (refused), for example when the
// game stays on the title.
void cancel();

// BLOCKPLANET_LAUNCH=menus forces the original menus (ModeLauncher).
bool enabled();

// Plays the game's intro again from its start (then its title), as at boot,
// without restarting the game: for Back from "Who is playing?". Done by the
// game's thread at its next frame outside play; ignored during play.
void request_intro_restart();

// Back to the box art (Back from "Who is playing?", docs/UI.md): the title
// from its start, without the intro. Done at the game's next frame in its menus.
void request_title_restart();

// Guest play: player 1's name in the game is "Guest" (no name is asked for).
void set_guest(bool guest);

// Changes the cartridge save in use without restarting the game (a profile
// change): once the game has nothing waiting to be written, the save behind
// it becomes `path` (empty: a guest's, in memory only) and the game reads it
// in again, as at start-up. The outcome arrives through take_save_switch_result.
void request_save_switch(const std::filesystem::path& path);
std::optional<bool> take_save_switch_result();

} // namespace direct_start
} // namespace tetrisphere
