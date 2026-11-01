#pragma once

// BlockPlanet trophies.
//
// Game code reports what happened as GameEvents ("combo", "level_completed",
// ...). It never decides about trophies. The engine updates stats and unlocks
// trophies from data-driven definitions (assets/trophies/trophies.json), and
// tells every registered TrophySink, so a launcher integration such as Quiver
// or Steam is one more sink and needs no changes to the trophies themselves.
// See docs/TROPHIES.md.

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tetrisphere {

using TrophyValue = std::variant<std::int64_t, std::string>;

struct GameEvent {
    std::string name;
    std::map<std::string, TrophyValue> fields;
};

enum class TrophyTier { Bronze, Silver, Gold, Platinum };

struct TrophyCondition {
    enum class Op { Equal, NotEqual, AtLeast, AtMost };
    std::string field;
    Op op = Op::Equal;
    TrophyValue value;
};

struct TrophyStatDefinition {
    std::string id;
    std::string event;
    std::vector<TrophyCondition> where;
    // Each matching event adds `add`, or the value of `add_field` when set.
    std::int64_t add = 1;
    std::string add_field;
    // "sum" accumulates; "max" keeps the largest value seen.
    bool keep_max = false;
};

struct TrophyTrigger {
    enum class Kind { Event, StatAtLeast, AllOf };
    Kind kind = Kind::Event;
    std::string event;                   // Event
    std::vector<TrophyCondition> where;  // Event
    std::string stat;                    // StatAtLeast
    std::int64_t target = 0;             // StatAtLeast
    std::vector<std::string> trophies;   // AllOf; empty means every other trophy
};

struct TrophyDefinition {
    std::string id;
    std::string name;
    std::string description;
    TrophyTier tier = TrophyTier::Bronze;
    bool hidden = false;
    TrophyTrigger trigger;
};

struct TrophyProgress {
    std::map<std::string, std::int64_t> unlocked;  // trophy id -> Unix time
    std::map<std::string, std::int64_t> stats;     // stat id -> value
};

// Receives unlocks. Called on the thread that posted the event, after the
// engine has released its lock.
class TrophySink {
public:
    virtual ~TrophySink() = default;
    virtual void trophy_unlocked(const TrophyDefinition& trophy) = 0;
};

class TrophyEngine {
public:
    // Parse definitions JSON. Returns false with a message on any error.
    bool load_definitions(const std::string& json, std::string& error);
    const std::vector<TrophyDefinition>& definitions() const { return trophies_; }
    std::optional<TrophyDefinition> find(const std::string& id) const;

    void set_progress(TrophyProgress progress);
    TrophyProgress progress() const;

    // When false (for example a guest playing on a host's unlocked stage),
    // events change neither stats nor trophies.
    void set_awards_enabled(bool enabled);
    bool awards_enabled() const;

    void add_sink(TrophySink* sink);

    // Apply one event at the given Unix time. Returns the newly unlocked ids.
    std::vector<std::string> post(const GameEvent& event, std::int64_t now_unix);

private:
    bool unlock_locked(const TrophyDefinition& trophy, std::int64_t now,
                       std::vector<std::string>& unlocked);
    mutable std::mutex mutex_;
    std::vector<TrophyStatDefinition> stats_;
    std::vector<TrophyDefinition> trophies_;
    TrophyProgress progress_;
    bool awards_enabled_ = true;
    std::vector<TrophySink*> sinks_;
};

// Trophy progress file with a backup, written through a temporary file.
class TrophyStore {
public:
    explicit TrophyStore(std::filesystem::path path);
    bool load(TrophyProgress& progress, std::string& error) const;
    bool save(const TrophyProgress& progress, std::string& error) const;

private:
    std::filesystem::path path_;
};

std::string trophy_progress_to_json(const TrophyProgress& progress);
bool trophy_progress_from_json(const std::string& json, TrophyProgress& progress,
                               std::string& error);
const char* trophy_tier_name(TrophyTier tier);

} // namespace tetrisphere
