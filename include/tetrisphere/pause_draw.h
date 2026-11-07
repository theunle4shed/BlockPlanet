#pragma once

#include <optional>
#include <string>
#include <vector>

// BlockPlanet's pause menu over the game's pause (src/graphics/pause_draw_hooks.cpp).

namespace blockplanet {

// While true and the game is paused, the game draws only its pause
// background (no line, menu text, hints or instructor).
void set_pause_menu_shown(bool shown);

// While true, the game's single-player HUD routine (func_80075CD8) skips its
// 2D draws and the 3D piece queue is not drawn, so BlockPlanet's own HUD can
// replace them. The routine's logic (magic launch, jingles) still runs.
void set_original_hud_hidden(bool hidden);

// The game's text in BlockPlanet's font (docs/ROADMAP.md, every screen in
// BlockPlanet's style): strings the game draws with its text routine
// (func_800382B4, called by 0x80038478 and others) are not drawn in its pixel font but kept here, with their
// place in the 320 x 240 frame and colour, for the UI to draw. Strings seen
// within the last moment (the game redraws them every frame).
struct GameText {
    std::string text;
    int font = 0;               // the game's font number (0 menus; others to measure)
    int x = 0, y = 0;           // the game's position, top left, in its 320 x 240 frame
    unsigned r = 255, g = 255, b = 255, a = 255;
    int width = 0;              // its width in the game's own font (func_800387CC's table), 0 if unknown
};
std::vector<GameText> recent_game_text();

// Training's speech bubble (a 150 x 115 one-byte image), with its "more" or
// "watch" prompt beneath: not drawn while the game's text is replaced, and
// reported here (seen in the last 150 ms) so BlockPlanet draws its own panel.
struct CardBox {
    int x = 0, y = 0, w = 0, h = 0;  // the bubble image, in the game's 320 x 240 frame
    int prompt = 0;                   // 0 none, 1 "more", 2 "watch"
    int kind = 0;                     // 0 Training's speech bubble, 1 a rules or goal card's box (203 x 143)
};
std::optional<CardBox> recent_card_box();
void set_game_text_replaced(bool replaced);

} // namespace blockplanet
