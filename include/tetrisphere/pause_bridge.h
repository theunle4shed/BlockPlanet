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
#include <string>
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
    // After Resume (unless BLOCKPLANET_RESUME_SKIP=0), once the game's exit
    // (0x800DFF08 = 5) has faded its picture in (0x800E1202 = 0), end the
    // exit at once, as the game does at its natural end (0x800DFF08 = 0, the
    // row -1, and its pause request flag 0x8013FAD0 cleared, which otherwise
    // starts a pause again on the next frame), and stop driving the row,
    // which otherwise wrote it back to 0 too. Returns whether it did.
    bool finish_resume(std::uint8_t* rdram);

private:
    bool showing_ = false;
    bool not_a_pause_ = false;  // the row is in use by something else (a scene)
    std::optional<Choice> choice_;
    bool pause_requested_ = false;
    Clock::time_point since_{};
};

// BlockPlanet's panel over the game's end-of-game menus (the owner's
// request, 2026-11-10: "can we make this menu align with our menu"): the
// game over menu (TRY AGAIN, EXIT and sometimes TRAINING, with Wheels) after
// hearts reach -1, and Versus' end (CONTINUE, EXIT). They use the pause row
// (0x800E1228) during play. The items are the ones the game draws (its menu
// text, read with their heights, so rows follow the game's order, top first)
// once its menu has faded in (0x800DFF08 = 1); choosing sets the row and
// presses A, as the pause does. Not the chapter-end menu (CONTINUE, EXIT
// with hearts left in Story), which is left as it is.
class EndMenuBridge {
public:
    using Clock = std::chrono::steady_clock;
    struct Label { std::string text; int y = 0; };  // the game's menu text (capitals) and its height
    struct Output {
        bool open_menu = false;   // show the end screen (with `items` and `title`)
        bool close_menu = false;  // close BlockPlanet's menus
        bool active = false;      // the end menu (or its fade) is up: the grid shows, in red
        std::vector<blockplanet::ui::ListItem> items;
        std::string title;
        std::uint16_t buttons = 0;
        bool inject = false;
    };
    // `labels`: the game's text seen just now. `pause_showing`: BlockPlanet's
    // pause is in charge of the row.
    Output update(std::uint8_t* rdram, const std::vector<Label>& labels, bool menus_open, Clock::time_point now,
                  bool pause_showing = false);
    // The player chose the item on this row (its command "end_choose:ROW").
    void choose(int row, Clock::time_point now);
    bool showing() const { return showing_; }
    bool hide_original() const { return showing_ || choice_.has_value(); }
    // The row of EXIT in the menu shown (-1 if none).
    int exit_row() const { return exit_row_; }

private:
    bool showing_ = false;
    std::optional<int> choice_;
    Clock::time_point since_{}, opened_at_{};
    std::vector<blockplanet::ui::ListItem> items_;
    std::string title_;
    int exit_row_ = -1;
};
// The end screen's items from the game's menu text: rows in the game's order
// (top first), shown in BlockPlanet's order (Try again or Continue,
// Training, Exit), in sentence case, with icons. Empty unless the menu has
// EXIT and TRY AGAIN or CONTINUE.
std::vector<blockplanet::ui::ListItem> end_menu_items(const std::vector<EndMenuBridge::Label>& labels, int* exit_row = nullptr);

// The pause screen's items for the game mode (0x800E44A8) and, in Training,
// its lesson type (0x800E44AC: 1 Basics, 2 Advanced, 3 Puzzle). Restart is
// offered where BlockPlanet can start the same place again; Skip lesson only
// when the lesson has somewhere to skip to (can_skip: 0x8013D25C is set).
std::vector<blockplanet::ui::ListItem> pause_items(int mode, int training_type, bool can_skip = true);
bool can_restart(int mode, int training_type);
// Modes whose pause menu offers Restart chapter (Rescue, Hide & Seek).
bool restarts_chapter(int mode);

} // namespace tetrisphere
