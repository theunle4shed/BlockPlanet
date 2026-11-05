#pragma once

// BlockPlanet's pause menu over the game's own pause (docs/UI.md).
//
// The game's pause freezes play and draws its "paused" background, which
// also hides the board. BlockPlanet keeps that, hides the rest of the
// original pause (src/graphics/pause_draw_hooks.cpp) and shows its own
// pause screen. Choices drive the original pause the way ModeLauncher drives
// the menus: set its row, then press A.
//
// Original pause state (NTPE rev0): 0x800E1228 is the pause row during play
// (0x800E121C == -1): -1 when not paused, 0 CONTINUE, 1 SKIP (Training
// only), 2 AUDIO, 3 EXIT. The game-over menu (TRY AGAIN, EXIT, TRAINING)
// uses the same row while player 1's hearts (0x80112F9E) read -1; it is
// left alone.

#include "../../src/ui/ui_actions.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace tetrisphere {

class PauseBridge {
public:
    using Clock = std::chrono::steady_clock;
    enum class Choice { Resume = 0, Skip = 1, Quit = 3 };

    struct Output {
        bool open_menu = false;   // show the pause screen (with `items`)
        bool close_menu = false;  // close BlockPlanet's menus
        std::vector<blockplanet::ui::ListItem> items;
        std::uint16_t buttons = 0;  // N64 buttons to hold on port 1
        bool inject = false;        // whether `buttons` replaces the player's input
    };

    // Call every main-loop pass. `menus_open`: BlockPlanet's menus are open.
    // `start_pressed`: Start was pressed just now (within about a second).
    // The game opens its pause only on Start; chapter-end scenes use the same
    // row without it, and are left alone.
    Output update(std::uint8_t* rdram, bool menus_open, Clock::time_point now, bool start_pressed = true);
    // The player chose an item (the pause screen's commands).
    void choose(Choice choice, Clock::time_point now);
    // F1 during play: press Start to open the game's pause.
    void request_pause(Clock::time_point now);
    bool showing() const { return showing_; }
    // Whether the original pause's line, text and instructor should be hidden.
    bool hide_original() const { return showing_ || choice_.has_value(); }

private:
    bool showing_ = false;
    bool not_a_pause_ = false;  // the row is in use by something else (a scene)
    std::optional<Choice> choice_;
    bool pause_requested_ = false;
    Clock::time_point since_{};
};

// The pause screen's items for the game mode (0x800E44A8) and, in Training,
// its lesson type (0x800E44AC: 1 Basics, 2 Advanced, 3 Puzzle). Restart is
// offered where BlockPlanet can start the same place again.
std::vector<blockplanet::ui::ListItem> pause_items(int mode, int training_type);
bool can_restart(int mode, int training_type);
// Modes whose pause menu offers Restart chapter (Rescue, Hide & Seek).
bool restarts_chapter(int mode);

} // namespace tetrisphere
