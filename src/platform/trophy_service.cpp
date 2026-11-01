#include "tetrisphere/trophy_service.h"

#include "tetrisphere/diagnostic_log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

namespace tetrisphere {
namespace {

TrophyEngine engine;
std::unique_ptr<TrophyStore> store;
std::mutex save_mutex;
std::atomic<bool> ready{false};
std::atomic<bool> dirty{false};

// Reports unlocks on stdout and in the diagnostics file. The future on-screen
// notification and launcher integrations are further sinks.
class LogSink final : public TrophySink {
public:
    void trophy_unlocked(const TrophyDefinition& trophy) override {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "{\"event\":\"trophy_unlocked\",\"id\":\"%s\",\"tier\":\"%s\"}",
                      trophy.id.c_str(), trophy_tier_name(trophy.tier));
        std::fprintf(stdout, "%s\n", line);
        std::fflush(stdout);
        diagnostic_log().perf(line);
    }
};
LogSink log_sink;

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

void initialize_trophies(const std::filesystem::path& definitions,
                         const std::filesystem::path& progress_path) {
    std::ifstream input(definitions, std::ios::binary);
    std::stringstream text;
    text << input.rdbuf();
    std::string error;
    if (!input || !engine.load_definitions(text.str(), error)) {
        std::fprintf(stderr, "{\"event\":\"trophies_unavailable\",\"detail\":\"%s\"}\n",
                     input ? "invalid definitions" : "definitions not found");
        if (!error.empty()) std::fprintf(stderr, "%s\n", error.c_str());
        return;
    }
    store = std::make_unique<TrophyStore>(progress_path);
    TrophyProgress progress;
    if (!store->load(progress, error)) {
        // Keep the unreadable files untouched and run without saving.
        std::fprintf(stderr, "{\"event\":\"trophies_unavailable\",\"detail\":\"progress unreadable\"}\n");
        store.reset();
        return;
    }
    engine.set_progress(std::move(progress));
    engine.add_sink(&log_sink);
    ready = true;
    std::fprintf(stdout, "{\"event\":\"trophies_ready\",\"trophies\":%zu,\"unlocked\":%zu}\n",
                 engine.definitions().size(), engine.progress().unlocked.size());
    std::fflush(stdout);
}

TrophyEngine& trophy_engine() { return engine; }

void post_game_event(const GameEvent& event) {
    if (!ready) return;
    const auto unlocked = engine.post(event, unix_now());
    dirty = true;
    if (!unlocked.empty()) save_trophies();
}

void save_trophies() {
    if (!ready || !dirty.exchange(false)) return;
    const std::lock_guard lock(save_mutex);
    std::string error;
    if (store && !store->save(engine.progress(), error)) {
        dirty = true;
        std::fprintf(stderr, "{\"event\":\"trophy_save_failed\",\"detail\":\"%s\"}\n", error.c_str());
    }
}

void add_trophy_sink(TrophySink* sink) { engine.add_sink(sink); }

} // namespace tetrisphere
