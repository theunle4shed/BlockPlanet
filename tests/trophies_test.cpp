#include "tetrisphere/trophies.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace tetrisphere;

namespace {

const char* definitions = R"({
  "schema_version": 1,
  "stats": [
    { "id": "pieces", "event": "piece_placed" },
    { "id": "biggest_combo", "event": "combo", "add_field": "size", "combine": "max" },
    { "id": "rescue_points", "event": "combo", "where": { "mode": "rescue" }, "add_field": "score" }
  ],
  "trophies": [
    { "id": "first_combo", "name": "First", "trigger": { "event": "combo" } },
    { "id": "big_combo", "name": "Big", "tier": "silver",
      "trigger": { "event": "combo", "where": { "size": { "at_least": 10 }, "mode": "rescue" } } },
    { "id": "three_pieces", "name": "Three", "trigger": { "stat": "pieces", "at_least": 3 } },
    { "id": "rescue_1000", "name": "Points", "trigger": { "stat": "rescue_points", "at_least": 1000 } },
    { "id": "pair", "name": "Pair", "tier": "gold", "trigger": { "all_of": ["first_combo", "three_pieces"] } },
    { "id": "everything", "name": "All", "tier": "platinum", "hidden": true, "trigger": { "all_of": [] } }
  ]
})";

GameEvent combo(std::int64_t size, std::int64_t score, const char* mode) {
    return {"combo", {{"size", size}, {"score", score}, {"mode", std::string(mode)}}};
}

struct RecordingSink : TrophySink {
    std::vector<std::string> ids;
    void trophy_unlocked(const TrophyDefinition& trophy) override { ids.push_back(trophy.id); }
};

void engine_rules() {
    TrophyEngine engine;
    std::string error;
    assert(engine.load_definitions(definitions, error));
    RecordingSink sink;
    engine.add_sink(&sink);

    // Event trigger with conditions; the stat-with-condition ignores other modes.
    auto unlocked = engine.post(combo(4, 500, "vs"), 100);
    assert(unlocked == std::vector<std::string>{"first_combo"});
    assert(engine.progress().stats.at("biggest_combo") == 4);
    assert(engine.progress().stats.count("rescue_points") == 0);

    // Conditions must all match: size 12 in VS is not enough for big_combo.
    assert(engine.post(combo(12, 500, "vs"), 101).empty());
    assert(engine.progress().stats.at("biggest_combo") == 12);
    unlocked = engine.post(combo(10, 1000, "rescue"), 102);
    assert((unlocked == std::vector<std::string>{"big_combo", "rescue_1000"}));
    assert(engine.progress().stats.at("biggest_combo") == 12);  // max, not sum

    // Stat trigger, then the all_of chain completes in the same post.
    engine.post({"piece_placed", {}}, 103);
    engine.post({"piece_placed", {}}, 104);
    unlocked = engine.post({"piece_placed", {}}, 105);
    assert((unlocked == std::vector<std::string>{"three_pieces", "pair", "everything"}));
    assert(engine.progress().unlocked.at("pair") == 105);

    // Nothing unlocks twice, and every unlock reached the sink exactly once.
    assert(engine.post(combo(30, 5000, "rescue"), 106).empty());
    assert((sink.ids == std::vector<std::string>{
        "first_combo", "big_combo", "rescue_1000", "three_pieces", "pair", "everything"}));
}

void guests_earn_nothing() {
    TrophyEngine engine;
    std::string error;
    assert(engine.load_definitions(definitions, error));
    engine.set_awards_enabled(false);
    assert(engine.post(combo(50, 9000, "rescue"), 1).empty());
    assert(engine.progress().stats.empty() && engine.progress().unlocked.empty());
    engine.set_awards_enabled(true);
    assert(!engine.post(combo(1, 1, "rescue"), 2).empty());
}

void rejects_bad_definitions() {
    TrophyEngine engine;
    std::string error;
    assert(!engine.load_definitions("not json", error) && !error.empty());
    assert(!engine.load_definitions(R"({"schema_version":2})", error));
    assert(!engine.load_definitions(R"({"schema_version":1,"trophies":[
        {"id":"a","name":"A","trigger":{"stat":"missing","at_least":1}}]})", error));
    assert(!engine.load_definitions(R"({"schema_version":1,"trophies":[
        {"id":"a","name":"A","trigger":{"all_of":["b"]}}]})", error));
    assert(!engine.load_definitions(R"({"schema_version":1,"trophies":[
        {"id":"a","name":"A","trigger":{"event":"x"}},{"id":"a","name":"B","trigger":{"event":"y"}}]})", error));
    assert(!engine.load_definitions(R"({"schema_version":1,"trophies":[
        {"id":"Bad Id","name":"A","trigger":{"event":"x"}}]})", error));
    assert(!engine.load_definitions(R"({"schema_version":1,"trophies":[
        {"id":"a","name":"A","trigger":{"event":"x","where":{"n":{"at_least":"ten"}}}}]})", error));
}

void store_roundtrip(const std::filesystem::path& directory) {
    std::filesystem::remove_all(directory);
    const auto path = directory / "trophies.json";
    TrophyStore store(path);
    TrophyProgress progress;
    std::string error;
    assert(store.load(progress, error) && progress.unlocked.empty());  // missing file is fine
    progress.unlocked["first_combo"] = 1234;
    progress.stats["pieces"] = 42;
    assert(store.save(progress, error));
    progress.stats["pieces"] = 43;
    assert(store.save(progress, error));  // second save keeps a backup
    TrophyProgress loaded;
    assert(store.load(loaded, error));
    assert(loaded.unlocked.at("first_combo") == 1234 && loaded.stats.at("pieces") == 43);
    // A corrupt primary falls back to the backup.
    { std::ofstream(path, std::ios::trunc) << "{broken"; }
    assert(store.load(loaded, error) && loaded.stats.at("pieces") == 42);
}

void shipped_definitions_load(const std::filesystem::path& source_dir) {
    std::ifstream input(source_dir / "assets/trophies/trophies.json");
    assert(input);
    std::stringstream text;
    text << input.rdbuf();
    TrophyEngine engine;
    std::string error;
    const bool ok = engine.load_definitions(text.str(), error);
    if (!ok) { std::fprintf(stderr, "%s\n", error.c_str()); }
    assert(ok && engine.definitions().size() >= 10);
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    engine_rules();
    guests_earn_nothing();
    rejects_bad_definitions();
    store_roundtrip(argv[1]);
    shipped_definitions_load(argv[2]);
    return 0;
}
