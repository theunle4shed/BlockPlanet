#pragma once

// A profile's own save file, profiles/<id>/save.json: progress and
// preferences (see docs/SAVES.md). The game's cartridge save and trophies
// are separate files in the same folder.

#include "tetrisphere/linux_platform.h"
#include "tetrisphere/trophies.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace tetrisphere {

struct ControlPreferences {
    std::string family_override = "auto";
    ControllerBindings bindings{};
    MagicControl magic = MagicControl::West;
};

struct ProfilePreferences {
    // Unset until the profile has controls of its own; the PC's defaults are
    // copied in on first use.
    std::optional<ControlPreferences> controls;
};

struct ProfileStatistics {
    std::int64_t games_started = 0;
    std::int64_t games_over = 0;
    std::int64_t pieces_placed = 0;
    std::int64_t lives_lost = 0;
    std::int64_t levels_completed = 0;
    std::int64_t chapters_completed = 0;
    std::int64_t puzzles_solved = 0;
    std::int64_t vs_rounds_won = 0;
    std::int64_t vs_rounds_lost = 0;
    std::int64_t seconds_played = 0;  // time in play, not in menus
};

struct ProfileProgress {
    // Per mode ("rescue", "hide_and_seek"): chapter -> levels completed.
    std::map<std::string, std::map<int, std::set<int>>> levels_completed;
    // Per mode: chapters completed.
    std::map<std::string, std::set<int>> chapters_completed;
    std::set<int> puzzles_solved;
    ProfileStatistics statistics;
};

struct ProfileSaveData {
    ProfilePreferences preferences;
    ProfileProgress progress;
};

// Updates progress and statistics from a game event (as reported by
// GameEventWatcher). Returns true for milestones worth saving at once
// (levels, chapters and puzzles completed).
bool record_game_event(ProfileProgress& progress, const GameEvent& event);

class ProfileSave {
public:
    explicit ProfileSave(std::filesystem::path path);

    // Reads save.json, or its .bak if the main file is damaged; a missing
    // file gives an empty save. Fails only when both copies are damaged.
    bool load(ProfileSaveData& data, std::string& error) const;
    // Writes atomically, keeping the previous file as .bak.
    bool save(const ProfileSaveData& data, std::string& error) const;

private:
    std::filesystem::path path_;
};

} // namespace tetrisphere
