#pragma once

// Minefield on the sphere, step 1 (the owner's design, docs/ROADMAP.md).
//
// A real game stage hosts it: Casual 1:1, whose board is a single layer of
// bricks over the core. Once play begins, BlockPlanet makes every layer-1
// cell an inert block (value 423, type 47: the purple wall Puzzle uses, which
// the game never matches or clears), drawn blue by BlockPlanet's sphere
// drawing while the field is active. The drop button (A) uncovers the cell
// under the cursor (8 + player 1 +0x2B2A, 8 + +0x2B2E) through the Minefield rules;
// the slide button (B) flags it. Both are hidden from the game, and its drop
// timer is held so it never runs out.
//
// Uncovering a cell clears it the way the board stores an empty cell (value,
// face mask and appearance entry 0, column map 0), shows the core cell
// beneath (face mask 80) and opens the side faces of the cells round it
// (face mask bits 8, 4, 2, 1 for the neighbour at x - 1, x + 1, y - 1, y + 1),
// as the game's own boards do (read from a Puzzle board, 2026-11-12).
//
// The numbers are part of the core: BlockPlanet's own core tiles stand in for
// the picture tiles Hide & Seek uses, and an uncovered cell's core value names
// its number. Flagged cubes are drawn yellow; at the end the mines green (won)
// or red (lost). The first version's numbers floating over the HUD (marks())
// show with BLOCKPLANET_MINEFIELD_FLOATING=1.

#include "tetrisphere/minefield.h"

#include <atomic>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace tetrisphere::minefield_sphere {

// While on, BlockPlanet's sphere drawing shows inert blocks (value 423) as
// blue cubes (src/graphics/sphere_quadrants.cpp) where in_field() marks the
// board cell as part of the field; elsewhere they stay the purple wall.
inline std::atomic<bool>& blue_field() {
    static std::atomic<bool> on{false};
    return on;
}
inline std::atomic<bool>* field_cells() {
    static std::atomic<bool> cells[1024]{};
    return cells;
}
inline bool in_field(int board_cell) {
    return board_cell >= 0 && board_cell < 1024 && field_cells()[board_cell].load(std::memory_order_relaxed);
}

// A cube colour per board cell (a block type, 32 to 37: yellow, blue, green,
// magenta, lavender, red), or 0 for the field's blue. Flagged cells are
// yellow (the owner's idea, 2026-11-12: tidier than a flag icon); at the end
// the mines are green when won, red when lost. Set by the field, read by the
// sphere drawing.
inline std::atomic<int>* cube_colours() {
    static std::atomic<int> cells[1024]{};
    return cells;
}
inline int cube_colour(int board_cell) {
    return board_cell >= 0 && board_cell < 1024 ? cube_colours()[board_cell].load(std::memory_order_relaxed) : 0;
}

// The arenas: tetrisphere::minefield_arenas() (minefield.h).
using Arena = tetrisphere::MinefieldArena;
inline const std::vector<Arena>& arenas() { return tetrisphere::minefield_arenas(); }

// The next game started becomes arena `arena`'s minefield (the menu's items);
// `mines` >= 0 sets the count instead (for testing).
void arm(int arena, int mines = -1);
bool armed();
bool active();
// Called each frame with player 1's buttons on their way to the game: notes
// presses of A (uncover) and B (flag) and removes both while active.
std::uint16_t filter_input(std::uint16_t buttons);
// The stick (the same frame's), likewise kept from leaving the field.
void filter_stick(float& x, float& y);
// Called each frame from the host's poll (after the game's options).
void poll(std::uint8_t* rdram);
// Whether the game's cursor marker (its shadow of the piece) is hidden: while
// the field is in play the cell under the cursor shows it instead.
bool hide_cursor_marker();

// A number, flag or mine to draw over a cell, as an offset in cells from the
// cursor on screen (dx to the right; dy up, as the board's rows run).
struct Mark {
    float dx = 0.0f, dy = 0.0f;
    std::string text;
    std::string kind;  // "n1" .. "n8", "flag", "mine", "hit"
};
std::vector<Mark> marks(const std::uint8_t* rdram);
// For the HUD's panel: the mines flagged of all of them ("7 / 20"),
// "Cleared" or "Boom"; empty
// when not active.
std::string counter();
// The field being played, for the HUD and records (when active): its arena
// (arenas() index), the seconds from the first uncover (stopping at the end),
// and whether it is won or lost.
struct Progress {
    int arena = 0;
    double seconds = 0.0;
    bool started = false, won = false, lost = false;
};
std::optional<Progress> progress();

} // namespace tetrisphere::minefield_sphere
