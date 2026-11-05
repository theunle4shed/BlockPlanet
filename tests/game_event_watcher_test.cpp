#include "tetrisphere/game_event_watcher.h"

#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using namespace tetrisphere;

namespace {

std::vector<std::string> names(const std::vector<GameEvent>& events) {
    std::vector<std::string> out;
    for (const auto& e : events) out.push_back(e.name);
    return out;
}

GameStateSnapshot playing(std::int16_t hearts, std::uint8_t piece, std::int32_t score = 0) {
    GameStateSnapshot s;
    s.scene = -1;
    s.mode = 1;
    s.hearts = hearts;
    s.piece = piece;
    s.score = score;
    return s;
}

// Mirrors a Rescue recording: start, a scoring drop, three failed drops,
// game over, then a retry.
void rescue_sequence() {
    GameEventWatcher watcher;
    GameStateSnapshot menu;
    menu.scene = 0;
    assert(watcher.update(menu).empty());
    // The hearts refill in the same frame play begins.
    auto events = watcher.update(playing(3, 0));
    assert(names(events) == std::vector<std::string>{"mode_started"});
    assert(std::get<std::string>(events[0].fields.at("mode")) == "rescue");
    assert(names(watcher.update(playing(3, 8))) == std::vector<std::string>{"piece_placed"});
    assert(watcher.update(playing(3, 8, 1040)).empty());  // still dropping: no repeat
    assert(watcher.update(playing(3, 2, 1040)).empty());
    events = watcher.update(playing(2, 8, 1040));
    assert((names(events) == std::vector<std::string>{"piece_placed", "life_lost"}));
    watcher.update(playing(2, 0, 1040));
    assert(names(watcher.update(playing(1, 8, 1040))) ==
           (std::vector<std::string>{"piece_placed", "life_lost"}));
    watcher.update(playing(1, 2, 1040));
    events = watcher.update(playing(-1, 8, 1040));
    assert((names(events) == std::vector<std::string>{"piece_placed", "life_lost", "game_over"}));
    assert(std::get<std::int64_t>(events.back().fields.at("score")) == 10400);
    // The score resets for the retry; the new game starts when hearts refill.
    watcher.update(playing(-1, 2, 0));
    assert(names(watcher.update(playing(3, 2, 0))) == std::vector<std::string>{"mode_started"});
}

void ignores_menus_and_transitions() {
    GameEventWatcher watcher;
    GameStateSnapshot a = playing(3, 0);
    a.scene = 4;
    watcher.update(a);
    // Returning to play mid-game (score above zero) is not a new game.
    assert(watcher.update(playing(3, 8, 500)).empty());
    // Leaving play mid-drop reports nothing.
    GameStateSnapshot b = playing(2, 8);
    b.scene = 2;
    assert(watcher.update(b).empty());
}

void reads_guest_memory_layout() {
    std::vector<std::uint8_t> rdram(0x800000);
    auto put16 = [&](std::uint32_t address, std::uint16_t v) {
        std::memcpy(rdram.data() + ((address & 0xFFFFFF) ^ 2u), &v, 2);
    };
    auto put32 = [&](std::uint32_t address, std::int32_t v) {
        std::memcpy(rdram.data() + (address & 0xFFFFFF), &v, 4);
    };
    put16(game_state::scene, 0xFFFF);
    put16(game_state::mode, 1);
    put32(game_state::p1_context + game_state::score_offset, 1040);
    put16(game_state::p1_context + game_state::hearts_offset, 2);
    rdram[(game_state::piece & 0xFFFFFF) ^ 3u] = 8;
    put16(game_state::training_type, 1);
    put32(game_state::training_page, static_cast<std::int32_t>(game_state::basics_last_page));
    const auto s = read_game_state(rdram.data());
    assert(s.scene == -1 && s.mode == 1 && s.score == 1040 && s.hearts == 2 && s.piece == 8);
    assert(s.training_type == 1 && s.training_page == game_state::basics_last_page);
    assert(game_mode_name(1) == "rescue" && game_mode_name(4) == "puzzle" &&
           game_mode_name(5) == "hide_and_seek" && game_mode_name(99).empty());
}

void vs_rounds() {
    GameEventWatcher watcher;
    auto vs = [](std::int16_t p1, std::int16_t p2) {
        GameStateSnapshot s = playing(p1, 0);
        s.mode = 6;
        s.p2_hearts = p2;
        return s;
    };
    auto events = watcher.update(vs(3, 3));
    assert(names(events) == std::vector<std::string>{"mode_started"});
    assert(std::get<std::string>(events[0].fields.at("mode")) == "vs_cpu");
    watcher.update(vs(1, 3));
    events = watcher.update(vs(-1, 3));
    assert((names(events) == std::vector<std::string>{"life_lost", "vs_round"}));
    assert(std::get<std::string>(events[1].fields.at("result")) == "loss");
    // Hearts refill for the next round: no new mode_started and no game_over.
    assert(watcher.update(vs(3, 3)).empty());
    watcher.update(vs(3, 1));
    events = watcher.update(vs(3, -1));
    assert(names(events) == std::vector<std::string>{"vs_round"});
    assert(std::get<std::string>(events[0].fields.at("result")) == "win");
}

// Training Basics counts as finished only if its last page was shown before
// the game left it (Story's Chapter 0).
void training_basics() {
    auto training = [](std::uint32_t page) {
        GameStateSnapshot s;
        s.scene = -1;
        s.mode = game_state::mode_training;
        s.training_type = 1;
        s.training_page = page;
        s.hearts = 3;
        return s;
    };
    GameStateSnapshot menu;
    menu.scene = 0;
    auto has = [](const std::vector<GameEvent>& events, const char* name) {
        for (const auto& e : events) if (e.name == name) return true;
        return false;
    };
    GameEventWatcher quit;
    quit.update(menu);
    quit.update(training(0x800EE7A4));
    quit.update(training(0x800EF198));  // the magic lesson, not its last page
    assert(!has(quit.update(menu), "training_completed"));

    GameEventWatcher done;
    done.update(menu);
    done.update(training(0x800EE7A4));
    assert(!has(done.update(training(game_state::basics_last_page)), "training_completed"));
    const auto events = done.update(menu);
    assert(has(events, "training_completed"));
    assert(!has(done.update(menu), "training_completed"));  // once

    GameEventWatcher puzzle;  // Training Puzzle (type 3)
    auto last = training(game_state::puzzle_last_page);
    last.training_type = 3;
    puzzle.update(last);
    const auto puzzle_events = puzzle.update(menu);
    assert(has(puzzle_events, "training_completed"));
    for (const auto& e : puzzle_events)
        if (e.name == "training_completed") assert(std::get<std::string>(e.fields.at("lesson")) == "puzzle");

    GameEventWatcher advanced;  // Advanced's text is past Basics' range
    auto page = training(0x800EF44C);
    page.training_type = 2;
    advanced.update(page);
    assert(!has(advanced.update(menu), "training_completed"));
}

} // namespace

int main() {
    training_basics();
    rescue_sequence();
    ignores_menus_and_transitions();
    reads_guest_memory_layout();
    vs_rounds();
    return 0;
}
