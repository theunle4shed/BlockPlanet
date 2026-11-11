#include "tetrisphere/pause_bridge.h"

#include "tetrisphere/guest_memory.h"

#include <algorithm>

namespace tetrisphere {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t scene = 0x800E121Cu;
constexpr std::uint32_t pause_row = 0x800E1228u;
constexpr std::uint32_t mode = 0x800E44A8u;
constexpr std::uint32_t p1_hearts = 0x80110220u + 0x2D7Eu;  // s16, -1 at game over
constexpr std::int16_t training = 3;
constexpr std::uint16_t button_a = 0x8000;
constexpr std::uint16_t button_start = 0x1000;
constexpr auto press_time = std::chrono::milliseconds(100);
constexpr auto release_time = std::chrono::milliseconds(250);
constexpr auto timeout = std::chrono::seconds(3);

// Pressed for press_time out of every press_time + release_time.
bool pressing(PauseBridge::Clock::duration elapsed) {
    return elapsed % (press_time + release_time) >= release_time;
}

} // namespace

bool restarts_chapter(int game_mode) {
    // Chaptered modes whose TRY AGAIN after a game over goes back to the
    // chapter's first level (tested: Rescue, Hide & Seek): getting through a
    // chapter without a game over is the challenge, so the pause menu offers
    // Restart chapter, after a confirmation, instead of Restart level.
    return game_mode == 1 || game_mode == 5;
}

bool can_restart(int game_mode, int training_type) {
    switch (game_mode) {
    case 1: case 2: case 4: case 5: case 6: case 9: return true;  // Rescue, Time Trial, Puzzle, Hide & Seek, VS CPU, Lines
    case 3: return training_type == 1 || training_type == 3;      // Training Basics or Puzzle (Chapter 0)
    default: return false;
    }
}

std::vector<blockplanet::ui::ListItem> pause_items(int game_mode, int training_type, bool can_skip) {
    std::vector<blockplanet::ui::ListItem> items{{"resume", "Resume", "", "command:pause_resume", true, 1, true}};
    if (can_restart(game_mode, training_type))
        items.push_back(restarts_chapter(game_mode)
            ? blockplanet::ui::ListItem{"restart", "Restart Chapter", "", "command:pause_restart_chapter", true}
            : blockplanet::ui::ListItem{"restart", game_mode == 3 ? "Restart Lesson" : "Restart Level", "",
                                        "command:pause_restart", true});
    // The game's SKIP jumps to the lesson's "skip to" text (0x8013D25C, from
    // the lesson record); a lesson without one (Basics) has nowhere to go and
    // play stops, so Skip is only offered when there is somewhere to skip to.
    if (game_mode == training && can_skip) items.push_back({"skip", "Skip Lesson", "", "command:pause_skip", true});
    // A short guide to the mode (BlockPlanet's own words), to read.
    if (game_mode == 1 || game_mode == 4 || game_mode == 5 || game_mode == 6 || game_mode == 9)
        items.push_back({"howto", "How to Play", "", "command:pause_howto", true});
    items.push_back({"options", "Options", "", "open:options", true});
    items.push_back({"quit", "Exit", "", "command:pause_quit", true});  // "Exit to <place>" (linux_main)
    // Icons (Lucide, the menus' icon font): play, restart, skip, help, settings, leave.
    for (auto& item : items)
        item.icon = item.id == "resume" ? "\uE140" : item.id == "restart" ? "\uE14C" : item.id == "skip" ? "\uE164"
                  : item.id == "howto" ? "\uE082" : item.id == "options" ? "\uE158" : "\uE112";
    return items;
}

void PauseBridge::choose(Choice choice, Clock::time_point now) {
    choice_ = choice;
    since_ = now;
}

bool PauseBridge::finish_resume(std::uint8_t* rdram) {
    if (!choice_ || *choice_ != Choice::Resume) return false;
    if (load16(rdram, scene) != -1 || load16(rdram, pause_row) < 0) return false;
    if (load16(rdram, 0x800DFF08u) != 5 || load16(rdram, 0x800E1202u) != 0) return false;
    // The game's pause request flag: while it is set and the row is -1, its
    // main loop (func_80070118) starts a pause again (found by the parent
    // session with a write watch, 2026-11-10).
    store16(rdram, 0x8013FAD0u, 0);
    store16(rdram, 0x800DFF08u, 0);
    store16(rdram, pause_row, -1);
    choice_.reset();
    showing_ = false;
    return true;
}

void PauseBridge::request_pause(Clock::time_point now) {
    pause_requested_ = true;
    since_ = now;
}

PauseBridge::Output PauseBridge::update(std::uint8_t* rdram, bool menus_open, Clock::time_point now,
                                       bool start_pressed) {
    Output out;
    const bool playing = load16(rdram, scene) == -1;
    const std::int16_t row = load16(rdram, pause_row);
    // The game-over menu (TRY AGAIN, EXIT, TRAINING) uses the same row while
    // the hearts read -1; it is left as the game shows it.
    const bool game_over = load16(rdram, p1_hearts) == -1;
    const bool paused = playing && row >= 0 && !game_over;

    if (pause_requested_) {
        // F1 during play: press Start until the game pauses.
        if (paused || !playing || now - since_ > timeout) {
            pause_requested_ = false;
        } else {
            out.inject = true;
            out.buttons = pressing(now - since_) ? button_start : 0;
            return out;
        }
    }
    if (!paused) not_a_pause_ = false;
    if (paused && !showing_ && !choice_ && (not_a_pause_ || !start_pressed)) {
        // The row was set without a Start press: a chapter-end scene, not a pause.
        not_a_pause_ = true;
        return out;
    }
    if (!paused) {
        // Resumed, or left play: nothing more to do.
        if (showing_) out.close_menu = menus_open;
        showing_ = false;
        choice_.reset();
        return out;
    }
    if (!showing_ && !choice_) {
        showing_ = true;
        out.open_menu = true;
        out.items = pause_items(load16(rdram, mode), load16(rdram, 0x800E44ACu), load32(rdram, 0x8013D25Cu) != 0);
        return out;
    }
    // Closing the menus (Back) means Resume.
    if (showing_ && !choice_ && !menus_open) choose(Choice::Resume, now);
    if (choice_) {
        if (now - since_ > timeout) {  // give up; show the menu again
            choice_.reset();
            showing_ = false;
            return out;
        }
        if (*choice_ != Choice::Resume || menus_open) out.close_menu = menus_open;
        store16(rdram, pause_row, static_cast<std::int16_t>(*choice_));
        out.inject = true;
        out.buttons = pressing(now - since_) ? button_a : 0;
    }
    return out;
}

std::vector<blockplanet::ui::ListItem> end_menu_items(const std::vector<EndMenuBridge::Label>& labels, int* exit_row) {
    // The game's items, top first (its rows), each once.
    std::vector<EndMenuBridge::Label> seen;
    for (const auto& l : labels)
        if ((l.text == "TRY AGAIN" || l.text == "CONTINUE" || l.text == "TRAINING" || l.text == "EXIT") &&
            std::none_of(seen.begin(), seen.end(), [&](const auto& s) { return s.text == l.text; }))
            seen.push_back(l);
    std::stable_sort(seen.begin(), seen.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
    const auto has = [&](const char* t) { return std::any_of(seen.begin(), seen.end(), [&](const auto& s) { return s.text == t; }); };
    if (exit_row) *exit_row = -1;
    if (!has("EXIT") || !(has("TRY AGAIN") || has("CONTINUE"))) return {};
    std::vector<blockplanet::ui::ListItem> items;
    for (const char* name : {"TRY AGAIN", "CONTINUE", "TRAINING", "EXIT"}) {
        const auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& s) { return s.text == name; });
        if (it == seen.end()) continue;
        const int row = int(it - seen.begin());
        const std::string text = name;
        blockplanet::ui::ListItem item{text == "TRY AGAIN" ? "try_again" : text == "CONTINUE" ? "continue" : text == "TRAINING" ? "training" : "exit",
                                       text == "TRY AGAIN" ? "Try Again" : text == "CONTINUE" ? "Continue" : text == "TRAINING" ? "Training" : "Exit",
                                       "", "command:end_choose:" + std::to_string(row), true};
        // Icons (Lucide, as the pause's): restart, play, help, leave.
        item.icon = text == "TRY AGAIN" ? "" : text == "CONTINUE" ? "" : text == "TRAINING" ? "" : "";
        if (text == "EXIT" && exit_row) *exit_row = row;
        items.push_back(item);
    }
    items.front().focus = true;
    return items;
}

EndMenuBridge::Output EndMenuBridge::update(std::uint8_t* rdram, const std::vector<Label>& labels, bool menus_open,
                                            Clock::time_point now, bool pause_showing) {
    Output out;
    const bool playing = load16(rdram, scene) == -1;
    const std::int16_t row = load16(rdram, pause_row);
    const bool game_over = load16(rdram, p1_hearts) == -1;
    const bool versus = load16(rdram, mode) == 6;
    const bool ended = playing && row >= 0 && (game_over || versus) && !pause_showing;
    if (!ended) {
        if (showing_ || choice_) out.close_menu = menus_open && playing;
        showing_ = false;
        choice_.reset();
        items_.clear();
        return out;
    }
    out.active = game_over || showing_ || choice_;
    if (choice_) {
        if (now - since_ > std::chrono::seconds(4)) {  // give up; show the panel again
            choice_.reset();
            showing_ = false;
            return out;
        }
        out.close_menu = menus_open;
        store16(rdram, pause_row, static_cast<std::int16_t>(*choice_));
        out.inject = true;
        out.buttons = pressing(now - since_) ? button_a : 0;
        return out;
    }
    if (!showing_) {
        opened_at_ = Clock::time_point{};
        // Once the game's menu has faded in and its items are drawn.
        if (load16(rdram, 0x800DFF08u) != 1) return out;
        auto items = end_menu_items(labels, &exit_row_);
        if (items.empty()) return out;
        items_ = std::move(items);
        title_ = game_over ? "Game Over" : "Match Over";
        showing_ = true;
        out.active = true;
    }
    // Opening, or closed with Back: the panel stays until a choice (asked
    // again only after a moment, as the menus take a frame or two to open).
    if (!menus_open && now - opened_at_ > std::chrono::milliseconds(500)) {
        opened_at_ = now;
        out.open_menu = true;
        out.items = items_;
        out.title = title_;
    }
    return out;
}

void EndMenuBridge::choose(int row, Clock::time_point now) {
    if (!showing_) return;
    choice_ = row;
    since_ = now;
}

} // namespace tetrisphere
