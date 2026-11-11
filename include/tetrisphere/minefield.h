#pragma once

// Minefield: BlockPlanet's own take on the classic hidden-mines puzzle (the
// owner's idea, 2026-11-11). A grid hides mines; uncovering a cell shows how
// many of its eight neighbours hold one (a cell with none opens its
// neighbours too); flags mark the cells the player thinks are mines.
// Uncovering a mine loses; uncovering every safe cell wins. The first cell
// uncovered, and its neighbours, never hold a mine.
//
// The grid can wrap at its edges (a later version on the sphere, whose board
// wraps both ways); the flat prototype does not.
//
// Menu commands (handled by the game, docs/UI.md):
//   minefield                  the size choice
//   minefield_start:N          a new field of size N (minefield_sizes())
//   minefield_open:I           uncover cell I (on an uncovered number with
//                              as many flags around it: its other neighbours)
//   minefield_flag:I           flag or unflag cell I
//   minefield_again            a new field of the same size

#include "../../src/ui/ui_actions.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tetrisphere {

class Minefield {
public:
    enum class State { Ready, Playing, Won, Lost };
    struct Cell {
        bool mine = false;
        bool open = false;
        bool flag = false;
        int count = 0;  // mines among the neighbours
    };

    Minefield(int width, int height, int mines, std::uint32_t seed, bool wrap = false);

    int width() const { return width_; }
    int height() const { return height_; }
    int mines() const { return mines_; }
    State state() const { return state_; }
    const Cell& cell(int index) const { return cells_[static_cast<std::size_t>(index)]; }
    int size() const { return width_ * height_; }
    int flags() const { return flags_; }
    // The mine uncovered that lost the game, or -1.
    int exploded() const { return exploded_; }

    // Uncovers a cell (placing the mines on the first one). On an uncovered
    // number whose flagged neighbours match it, uncovers its other
    // neighbours instead. Returns whether anything changed.
    bool open(int index);
    // Flags or unflags a covered cell while playing.
    bool toggle_flag(int index);

    std::vector<int> neighbours(int index) const;

private:
    void place_mines(int safe);
    void flood(int index);
    void check_won();

    int width_, height_, mines_;
    bool wrap_;
    std::uint32_t seed_;
    State state_ = State::Ready;
    std::vector<Cell> cells_;
    int flags_ = 0;
    int opened_ = 0;
    int exploded_ = -1;
};

// The sizes offered: name, width, height, mines.
struct MinefieldSize {
    const char* label;
    int width, height, mines;
};
const std::vector<MinefieldSize>& minefield_sizes();
std::vector<blockplanet::ui::ListItem> minefield_size_choices();

// The grid as list items (row by row): covered cells, flags, numbers, mines.
// `focus` is the cell to focus (when the screen opens).
std::vector<blockplanet::ui::ListItem> minefield_cells(const Minefield& field, int focus = -1);
// The line under the title: mines left, or the result.
std::string minefield_status(const Minefield& field);

} // namespace tetrisphere
