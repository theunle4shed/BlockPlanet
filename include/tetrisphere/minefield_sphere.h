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
// The numbers, flags and mines are drawn by BlockPlanet's HUD over the cells
// (marks()), placed as the score pop-ups are: the cursor faces the camera, so
// a cell dx cells away sits at the sphere's radius x sin(dx x 2pi/32).

#include "tetrisphere/minefield.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace tetrisphere::minefield_sphere {

// While on, BlockPlanet's sphere drawing shows inert blocks (value 423) as
// blue cubes (src/graphics/sphere_quadrants.cpp).
inline std::atomic<bool>& blue_field() {
    static std::atomic<bool> on{false};
    return on;
}

// The next game started becomes the minefield (the menu's test item).
void arm(int mines);
bool armed();
bool active();
// Called each frame with player 1's buttons on their way to the game: notes
// presses of A (uncover) and B (flag) and removes both while active.
std::uint16_t filter_input(std::uint16_t buttons);
// Called each frame from the host's poll (after the game's options).
void poll(std::uint8_t* rdram);

// A number, flag or mine to draw over a cell, as an offset in cells from the
// cursor on screen (dx to the right; dy up, as the board's rows run).
struct Mark {
    float dx = 0.0f, dy = 0.0f;
    std::string text;
    std::string kind;  // "n1" .. "n8", "flag", "mine", "hit"
};
std::vector<Mark> marks(const std::uint8_t* rdram);
// "N mines left", or the result; empty when not active.
std::string status();
// For the HUD's panel: the mines left to flag, "Cleared" or "Boom"; empty
// when not active.
std::string counter();

} // namespace tetrisphere::minefield_sphere
