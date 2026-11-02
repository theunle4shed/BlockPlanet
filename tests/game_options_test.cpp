#include "tetrisphere/game_event_watcher.h"
#include "tetrisphere/game_options.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace tetrisphere;

namespace {

struct Ram {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x800000);
    std::uint8_t* data() { return bytes.data(); }
    void put16(std::uint32_t a, std::int16_t v) { std::memcpy(&bytes[(a & 0xFFFFFF) ^ 2u], &v, 2); }
    std::int16_t get16(std::uint32_t a) { std::int16_t v; std::memcpy(&v, &bytes[(a & 0xFFFFFF) ^ 2u], 2); return v; }
    void put32(std::uint32_t a, std::int32_t v) { std::memcpy(&bytes[a & 0xFFFFFF], &v, 4); }
    std::int32_t get32(std::uint32_t a) { std::int32_t v; std::memcpy(&v, &bytes[a & 0xFFFFFF], 4); return v; }
    std::uint8_t& u8(std::uint32_t a) { return bytes[(a & 0xFFFFFF) ^ 3u]; }
};

constexpr auto ctx = game_state::p1_context;

void casual_refills_hearts() {
    Ram ram;
    ram.put16(game_state::scene, -1);
    ram.put16(game_state::mode, 1);
    ram.put16(ctx + game_state::hearts_offset, 2);
    ram.put16(ctx + game_memory::hearts_shown_offset, 3);
    ram.u8(ctx + game_memory::heart_state_offset + 0x40) = 1;
    GameOptionApplier casual({true, std::nullopt});
    assert(casual.affects_awards());
    casual.apply(ram.data());
    assert(ram.get16(ctx + game_state::hearts_offset) == 3);
    assert(ram.get16(ctx + game_memory::hearts_shown_offset) == 3);
    assert(ram.u8(ctx + game_memory::heart_state_offset + 0x40) == 0);
    // Not in play, VS, or game already over: leave hearts alone.
    ram.put16(ctx + game_state::hearts_offset, 1);
    ram.put16(game_state::scene, 2);
    casual.apply(ram.data());
    assert(ram.get16(ctx + game_state::hearts_offset) == 1);
    ram.put16(game_state::scene, -1);
    ram.put16(game_state::mode, game_state::mode_vs_cpu);
    casual.apply(ram.data());
    assert(ram.get16(ctx + game_state::hearts_offset) == 1);
    // Off by default.
    GameOptionApplier normal({});
    assert(!normal.affects_awards());
    ram.put16(game_state::mode, 1);
    normal.apply(ram.data());
    assert(ram.get16(ctx + game_state::hearts_offset) == 1);
}

void casual_holds_time_trial_clock() {
    Ram ram;
    ram.put16(game_state::scene, -1);
    ram.put16(game_state::mode, 2);
    ram.put16(ctx + game_state::hearts_offset, 3);
    ram.put32(ctx + game_memory::clock_offset, 100);
    GameOptionApplier casual({true, std::nullopt});
    casual.apply(ram.data());
    assert(ram.get32(ctx + game_memory::clock_offset) == game_memory::casual_clock_floor);
    ram.put32(ctx + game_memory::clock_offset, 9000);
    casual.apply(ram.data());
    assert(ram.get32(ctx + game_memory::clock_offset) == 9000);
}

void start_chapter_until_play() {
    Ram ram;
    ram.put16(game_state::scene, 0);
    GameOptionApplier start({false, 12});
    assert(start.affects_awards());
    start.apply(ram.data());
    assert(ram.get16(game_state::chapter) == 11);
    ram.put16(game_state::chapter, 0);  // the game resets it while loading
    start.apply(ram.data());
    assert(ram.get16(game_state::chapter) == 11);
    ram.put16(game_state::scene, -1);
    start.apply(ram.data());
    // After play begins the game advances chapters itself.
    ram.put16(game_state::chapter, 12);
    ram.put16(game_state::scene, 3);
    start.apply(ram.data());
    assert(ram.get16(game_state::chapter) == 12);
}

void level_events() {
    GameEventWatcher watcher;
    auto play = [](std::int16_t chapter, std::int16_t level, std::int16_t hearts = 3, std::int16_t mode = 1) {
        GameStateSnapshot s;
        s.scene = -1; s.mode = mode; s.hearts = hearts; s.chapter = chapter; s.level = level; s.score = 100;
        return s;
    };
    watcher.update(play(0, 0));
    watcher.update(play(0, 0, 2));  // one heart lost on level 1:1
    GameStateSnapshot between = play(0, 0, 2);
    between.scene = 5;              // the game leaves play between levels
    watcher.update(between);
    auto events = watcher.update(play(0, 1, 2));
    assert(events.size() == 1 && events[0].name == "level_completed");
    assert(std::get<std::int64_t>(events[0].fields.at("chapter")) == 1);
    assert(std::get<std::int64_t>(events[0].fields.at("level")) == 1);
    assert(std::get<std::int64_t>(events[0].fields.at("hearts_lost")) == 1);
    assert(std::get<std::int64_t>(events[0].fields.at("score")) == 1000);
    events = watcher.update(play(1, 0, 2));
    assert(events.size() == 2 && events[0].name == "level_completed" && events[1].name == "chapter_completed");
    assert(std::get<std::int64_t>(events[0].fields.at("hearts_lost")) == 0);
    assert(std::get<std::int64_t>(events[1].fields.at("chapter")) == 1);
    // Puzzle: the same field counts puzzles.
    GameEventWatcher puzzles;
    puzzles.update(play(0, 0, 3, 4));
    events = puzzles.update(play(0, 1, 3, 4));
    assert(events.size() == 1 && events[0].name == "puzzle_solved");
    assert(std::get<std::int64_t>(events[0].fields.at("puzzle")) == 1);
}

void casual_refill_is_not_a_new_game() {
    GameEventWatcher watcher;
    GameStateSnapshot s;
    s.scene = -1; s.mode = 1; s.hearts = 3; s.score = 50;
    watcher.update(s);
    s.hearts = 2;
    auto events = watcher.update(s);
    assert(events.size() == 1 && events[0].name == "life_lost");
    s.hearts = 3;  // refilled by casual play
    assert(watcher.update(s).empty());
}

} // namespace

int main() {
    casual_refills_hearts();
    casual_holds_time_trial_clock();
    start_chapter_until_play();
    level_events();
    casual_refill_is_not_a_new_game();
    return 0;
}
