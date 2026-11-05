#include "tetrisphere/pause_bridge.h"

#include "tetrisphere/guest_memory.h"

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

std::vector<blockplanet::ui::ListItem> pause_items(int game_mode, int training_type) {
    std::vector<blockplanet::ui::ListItem> items{{"resume", "Resume", "", "command:pause_resume", true, 1, true}};
    if (can_restart(game_mode, training_type))
        items.push_back(restarts_chapter(game_mode)
            ? blockplanet::ui::ListItem{"restart", "Restart chapter", "", "command:pause_restart_chapter", true}
            : blockplanet::ui::ListItem{"restart", game_mode == 3 ? "Restart lesson" : "Restart level", "",
                                        "command:pause_restart", true});
    if (game_mode == training) items.push_back({"skip", "Skip lesson", "", "command:pause_skip", true});
    items.push_back({"options", "Options", "", "open:options", true});
    items.push_back({"quit", "Quit to menu", "", "command:pause_quit", true});
    return items;
}

void PauseBridge::choose(Choice choice, Clock::time_point now) {
    choice_ = choice;
    since_ = now;
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
        out.items = pause_items(load16(rdram, mode), load16(rdram, 0x800E44ACu));
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

} // namespace tetrisphere
