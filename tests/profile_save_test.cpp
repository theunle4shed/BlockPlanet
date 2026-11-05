#include "tetrisphere/profile_save.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace tetrisphere;

namespace {

GameEvent event(const char* name, std::map<std::string, TrophyValue> fields = {}) { return {name, std::move(fields)}; }

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string error;

    // Events become progress and statistics; milestones ask for a save.
    ProfileProgress progress;
    assert(!record_game_event(progress, event("mode_started", {{"mode", std::string("rescue")}})));
    assert(!record_game_event(progress, event("piece_placed", {{"mode", std::string("rescue")}})));
    assert(!record_game_event(progress, event("life_lost", {{"mode", std::string("rescue")}})));
    assert(record_game_event(progress, event("level_completed", {{"mode", std::string("rescue")},
                                                                 {"chapter", std::int64_t{3}},
                                                                 {"level", std::int64_t{2}}})));
    assert(record_game_event(progress, event("chapter_completed", {{"mode", std::string("rescue")},
                                                                   {"chapter", std::int64_t{3}}})));
    assert(record_game_event(progress, event("puzzle_solved", {{"puzzle", std::int64_t{7}}})));
    record_game_event(progress, event("vs_round", {{"mode", std::string("vs_cpu")}, {"result", std::string("win")}}));
    record_game_event(progress, event("vs_round", {{"mode", std::string("vs_cpu")}, {"result", std::string("loss")}}));
    assert(!record_game_event(progress, event("unknown_event")));
    assert(!record_game_event(progress, event("life_lost", {{"mode", std::string("training")}})));
    assert(record_game_event(progress, event("training_completed", {{"lesson", std::string("basics")}})));
    assert(progress.chapters_completed["rescue"].count(0) == 1);  // Story's Chapter 0
    assert(progress.levels_completed["rescue"][3].count(2) == 1);
    assert(progress.chapters_completed["rescue"].count(3) == 1);
    assert(progress.puzzles_solved.count(7) == 1);
    const auto& s = progress.statistics;
    assert(s.games_started == 1 && s.pieces_placed == 1 && s.lives_lost == 1 && s.levels_completed == 1);
    assert(s.chapters_completed == 1 && s.puzzles_solved == 1 && s.vs_rounds_won == 1 && s.vs_rounds_lost == 1);

    // A missing file is an empty save; a save round-trips.
    ProfileSave store(root / "1" / "save.json");
    ProfileSaveData data;
    assert(store.load(data, error) && !data.preferences.controls && data.progress.statistics.games_started == 0);
    data.progress = progress;
    data.progress.statistics.seconds_played = 125;
    ControlPreferences controls;
    controls.family_override = "playstation";
    controls.bindings.confirm = PhysicalButton::East;
    controls.magic = MagicControl::RightShoulder;
    data.preferences.controls = controls;
    assert(store.save(data, error));
    ProfileSaveData read;
    assert(store.load(read, error));
    assert(read.preferences.controls && read.preferences.controls->family_override == "playstation");
    assert(read.preferences.controls->bindings.confirm == PhysicalButton::East);
    assert(read.preferences.controls->magic == MagicControl::RightShoulder);
    assert(read.progress.levels_completed["rescue"][3].count(2) == 1);
    assert(read.progress.statistics.seconds_played == 125 && read.progress.statistics.vs_rounds_won == 1);

    // A damaged file falls back to the previous one (.bak).
    data.progress.statistics.seconds_played = 200;
    assert(store.save(data, error));
    { std::ofstream(root / "1" / "save.json", std::ios::trunc) << "{broken"; }
    assert(store.load(read, error) && read.progress.statistics.seconds_played == 125);
    { std::ofstream(root / "1" / "save.json.bak", std::ios::trunc) << "{broken"; }
    assert(!store.load(read, error) && !error.empty());
    std::filesystem::remove_all(root);
    return 0;
}
