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
// A full-screen loading cover over everything (0: none, 1: opaque), with the
// mode being started ("Story · Stage 1:1"), while a mode is started through
// the game's own menus behind it.
void set_loading(const std::string& text, float opacity);
// The menus last open (screens first to top, each with its focus), and
// opening such a path again (back to the menu a game was started from).
std::vector<std::pair<std::string, std::size_t>> menu_path();
void request_restore(const std::vector<std::pair<std::string, std::size_t>>& path);
// As set_text, with [family:name] tokens drawn as button icons (data-buttons
// elements): [k:Esc] a key cap, [x:B] an Xbox button, [d:pad] a d-pad.
void set_button_text(const std::string& name, const std::string& text);

// Whether the player is using a controller (true) or the keyboard; screens
// get the class "pad" while a controller is used.
void set_pad(bool pad);
// The menus' Alt button, as "[family:name]" (k, x, p or n: drawn as in the cards).
void set_alt_button(const std::string& spec);

// Reports a command as if a menu item had been chosen (for typed text).
void send_command(const std::string& command);
// The screen on top, as of the last frame; empty when the menus are closed.
std::string current_screen();

// BlockPlanet's in-game HUD (docs/HUD_DESIGN.md), drawn over the game while
// no menu is open. Placeholder art until the owner's designs are in.
struct HudPieceView {
    std::string colour;                        // CSS colour, e.g. "#e8c020"
    std::vector<std::pair<int, int>> cells;    // cell positions (column, row)
    bool random_block = false;                      // the "?" piece
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
    // Story: the total score under the time, with the last few points added
    // ("+2,800"), newest first (empty: not shown).
    std::string story_score;
    std::vector<std::string> story_gains;
    // Story: the combo counter (the owner's choice, 2026-11-08): the combo
    // building now (live), else the last one; and the stage's best.
    int combo = 0;             // 0: no combo yet this stage (not shown)
    bool combo_live = false;   // counting up now
    int combo_best = 0;
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
    // Training (docs/TRAINING_REDESIGN.md): one row along the bottom of the
    // screen, holding only the parts Wheels has introduced; a part just
    // introduced glows for a moment (`intro`: "next", "lives", "magic",
    // "progress", "xcount", "moves").
    bool bottom_row = false;
    bool show_lives = true, show_magic = true;
    int multiplier = -1;       // the X count, or -1 to hide it
    std::vector<std::string> intro;
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
    struct Text {
        std::string text; float x = 0, y = 0, size = 0, width = 0; std::string colour;
        std::vector<std::pair<std::string, std::string>> parts;  // a joined line: each word and its colour (then text is unused)
        float wrap = 0;  // a paragraph wrapped to this width (window widths); 0: one line
    };  // width: the game's span (window widths), 0 unknown
    std::vector<Text> game_text;
    // A card's panel in BlockPlanet's style, drawn behind its text (Training's
    // speech bubble): position and size as fractions of the window, the tail
    // under its bottom edge at tail_x, and a hint in its bottom right corner
    // ("[family:name]" codes for button symbols).
    // The panel holds its text: a heading (bold, upright) and the body (the
    // speech, in italics; the game's yellow highlights in bold). It grows
    // upwards from its bottom edge to fit the text, so the tail stays put.
    struct Panel {
        float right = 0, bottom = 0, w = 0;  // fractions of the window: its right and bottom edges, its width
        float tail = 0.8f;                   // the tail's place along the bottom edge, 0 left to 1 right; negative: none
        float top = -1.0f;                   // when set: the panel's top edge (it grows downwards), instead of `bottom`
        std::vector<std::pair<std::string, std::string>> heading, body;  // (text, colour) runs
        // Several pages at once (Training's joined pages): blocks in order,
        // each a heading or a paragraph; a run "\n" is a line break.
        struct Block {
            bool heading = false;
            std::vector<std::pair<std::string, std::string>> runs;
        };
        std::vector<Block> blocks;  // used instead of heading and body when not empty
        std::string hint;
        std::string scroll_hint;  // shown when the text is longer than the screen allows ("[x:RS] Scroll")
    };
    std::optional<Panel> card_panel;
    bool text_only = false;
    // The game's own end menu is up (game over, Versus' end): the HUD's panels
    // fade out (hud.rcss, #hud.ended), the game's text stays.
    bool end_menu = false;
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
// Scrolls the card panel's text when it is taller than the screen allows, by
// `lines` (positive: down). Right stick, Page Up / Page Down, mouse wheel.
void scroll_card(float lines);
// The card panel's text is taller than its space and not yet scrolled to its
// end: A should not move on yet.
bool card_needs_scroll();

// The next command the player chose (an item's command:NAME), if any.
std::optional<std::string> take_command();

} // namespace blockplanet::ui
