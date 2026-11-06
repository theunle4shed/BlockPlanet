#pragma once

// The developer's records for Time Attack and Score Attack (docs/HUD_DESIGN.md):
// a time or a score for each stage of Story's 15 episodes, set by the owner
// playing with BLOCKPLANET_RECORD_DEVELOPER, shipped as
// assets/trophies/developer_records.json, and beaten for trophies.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace tetrisphere {

struct DeveloperRecords {
    static constexpr int chapters = 15;
    static constexpr int stages = 10;
    // "time_attack" (seconds, lower is better) or "score_attack" (points,
    // higher is better) -> chapter (1-based) -> a value per stage (0: none).
    std::map<std::string, std::map<int, std::vector<double>>> modes;

    double value(const std::string& mode, int chapter, int stage) const;
    // Whether `mine` beats or matches `record` for the mode.
    static bool beats(const std::string& mode, double mine, double record);
    // Keeps `value` if it is the first or better. Returns true when it changed.
    bool record(const std::string& mode, int chapter, int stage, double value);
    // Every stage of every episode has a developer record, and the player's
    // bests (chapter -> per stage) beat or match each one.
    bool all_beaten(const std::string& mode, const std::map<int, std::vector<double>>& bests) const;
};

bool load_developer_records(const std::filesystem::path& path, DeveloperRecords& out, std::string& error);
bool save_developer_records(const std::filesystem::path& path, const DeveloperRecords& records, std::string& error);
// Reads and writes the JSON text (for tests and the files above).
bool parse_developer_records(const std::string& text, DeveloperRecords& out, std::string& error);
std::string developer_records_json(const DeveloperRecords& records);

} // namespace tetrisphere
