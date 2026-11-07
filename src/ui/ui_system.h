#pragma once

// BlockPlanet's menu layer: RmlUi drawn over the game through RT64's draw
// hook (docs/UI.md). The functions below may be called from any thread;
// RmlUi itself runs on RT64's presentation thread.

#include "ui_actions.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blockplanet::ui {

// Registers the menu layer with RT64. Call before RT64 starts. `assets` is
// the folder holding the menu screens and fonts (ui/ beside the game).
// BLOCKPLANET_UI_TEST=1 opens the main menu at start.
void install(const std::filesystem::path& assets);

// Opens the menus at a screen (ui/screens/<screen>.rml), or closes them.
// Applied at the next frame.
void request_open(const std::string& screen);
// Shows a screen on top of the open ones (or opens the menus at it).
void request_show(const std::string& screen);
void request_close();
// Navigation input, applied at the next frame.
void queue_action(Action action);

// Current values for setting rows (data-setting), and the contents of lists
// (data-list). Applied at the next frame.
void set_setting(const std::string& name, const std::string& value);
void set_list(const std::string& name, std::vector<ListItem> items);
// Text for elements with data-text="NAME". Applied at the next frame.
void set_text(const std::string& name, const std::string& text);

// Whether the player is using a controller (true) or the keyboard; screens
// get the class "pad" while a controller is used.
void set_pad(bool pad);

// Reports a command as if a menu item had been chosen (for typed text).
void send_command(const std::string& command);
// The screen on top, as of the last frame; empty when the menus are closed.
std::string current_screen();

// BlockPlanet's in-game HUD (docs/HUD_DESIGN.md), drawn over the game while
// no menu is open. Placeholder art until the owner's designs are in.
struct HudPieceView {
    std::string colour;                        // CSS colour, e.g. "#e8c020"
    std::vector<std::pair<int, int>> cells;    // cell positions (column, row)
    bool mystery = false;                      // the "?" piece
};
struct HudView {
    int hearts = 3;            // 0 to 3
    float drop_left = 1.0f;    // the drop timer, 1 (full) to 0
    int magic = -1;            // -1 none, 0 to 5
    bool magic_model = false;  // the game draws the magic's model in the box
    float meter = 0.0f;        // the magic meter, 0 to 1
    std::vector<HudPieceView> next;  // next first
    bool show_next = true;     // Lines has no queue preview
    std::string mode;          // e.g. "Rescue"
    std::string stage;         // e.g. "1:3"
    // Puzzle: drops and slides left, shown on the left instead of health,
    // magic and the next pieces (-1: not Puzzle).
    int drops_left = -1, slides_left = -1;
    std::string goal;          // Hide & Seek: the level's goal, in words (empty: none)
    std::string time;          // the episode's time, e.g. "2:41"
    std::string clock;         // Score Attack: the time left, e.g. "4:37" (empty: none)
    bool clock_low = false;    // the last 30 seconds
    std::string score;         // e.g. "12,340" (empty: not shown)
    std::string score_best;    // e.g. "Best 482,000 · 6:11" (empty: none)
    std::string time_label;    // the Time section's title (default "Time")
    std::string best;          // a single stage: "Best 0:42" (empty: not shown)
    std::string time_delta;    // the net difference so far, e.g. "-0:12", or empty
    bool time_ahead = false;
    // The episode's splits, LiveSplit style: completed levels, then the current.
    struct Split {
        std::string label;     // "1:3"
        std::string time;      // "0:41"
        std::string delta;     // "+3.2", or empty (no best yet, or current)
        bool ahead = false;
        bool current = false;
    };
    std::vector<Split> splits;
    float progress = -1.0f;    // 0 to 1, or -1 to hide the bar
    // Placement: the game's picture as fractions of the window (x, y, width,
    // height), and the sphere's radius on screen in picture heights.
    float picture_x = 0.0f, picture_y = 0.0f, picture_w = 1.0f, picture_h = 1.0f;
    float sphere_radius = 0.275f;
    // Floating numbers (docs/HUD_DESIGN.md): text rising from a point on the
    // sphere, given as offsets from the picture's centre in picture heights,
    // with their age in seconds (they rise and fade over about 1.4 s).
    struct Popup {
        std::string text;
        std::string note;  // a second, smaller line (e.g. "21 blocks"), or empty
        float x = 0.0f, y = 0.0f;
        float age = 0.0f;
    };
    std::vector<Popup> popups;
    // VS (docs/HUD_DESIGN.md, VS): one column per player at their half's
    // outer edge (rows above and below on tall screens), only what is needed.
    struct VersusPlayer {
        int hearts = 3;
        float drop_left = 1.0f;
        int multiplier = 1;    // the X count (attack strength)
        int magic = -1;
        float meter = 0.0f;
        std::vector<HudPieceView> next;  // the next one or two
    };
    // The title screen: only the original game's notice lines and
    // BlockPlanet's disclaimer, centred in a band given as fractions of the
    // window (x, y, width). Empty: not the title.
    std::vector<std::string> title_notice;
    std::string title_disclaimer;
    float title_x = 0, title_y = 0, title_w = 0;
    // The game's own text, in BlockPlanet's font (the game's strings, placed
    // as the game placed them; fractions of the window, size in window
    // heights). With nothing else set, only these are shown.
    struct Text { std::string text; float x = 0, y = 0, size = 0; std::string colour; };
    std::vector<Text> game_text;
    bool text_only = false;
    bool versus = false;
    VersusPlayer players[2];
    // 0 near (as on a 4:3 screen, still), 1 the screen's edges, 2 near and
    // pushed out as the sphere grows on screen.
    int distance_mode = 0;
};
// Shows the HUD with these values, or hides it (nullopt). Applied at the next frame.
void set_hud(std::optional<HudView> hud);

// Where the HUD's magic item box is, as last laid out (pixels in a frame of
// frame_width x frame_height), while the HUD is shown.
struct HudBox {
    float x, y, width, height;
    float frame_width, frame_height;
};
std::optional<HudBox> hud_item_box();

// True while a menu screen is open (or about to open).
bool menus_open();

// The next command the player chose (an item's command:NAME), if any.
std::optional<std::string> take_command();

} // namespace blockplanet::ui
