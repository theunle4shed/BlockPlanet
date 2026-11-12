#pragma once

// The game's trophy service: one TrophyEngine, its definitions shipped beside
// the executable (trophies.json), and the player's progress in the data
// directory. Game hooks report events with post_game_event().

#include "tetrisphere/trophies.h"

#include <filesystem>

namespace tetrisphere {

// Load definitions from `definitions` and progress from `progress_path`.
// Problems are reported and leave trophies off; they never stop the game.
void initialize_trophies(const std::filesystem::path& definitions,
                         const std::filesystem::path& progress_path);
TrophyEngine& trophy_engine();
void post_game_event(const GameEvent& event);
// Save progress if it changed since the last save.
void save_trophies();
// Changes profile without restarting: saves the current progress, then loads
// `progress_path` (none: a guest, who earns no trophies).
void switch_trophies(const std::filesystem::path& definitions, const std::filesystem::path* progress_path);
// Register an extra sink, for example a launcher integration.
void add_trophy_sink(TrophySink* sink);

} // namespace tetrisphere
