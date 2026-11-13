#pragma once

// Minefield: BlockPlanet's own take on the classic hidden-mines puzzle (the
// owner's idea, 2026-11-11), played on the sphere (minefield_sphere.h). A
// grid hides mines; uncovering a cell shows how many of its eight neighbours
// hold one (a cell with none opens its neighbours too); flags mark the cells
// the player thinks are mines. Uncovering a mine loses; uncovering every safe
// cell wins. The first cell uncovered, and its neighbours, never hold a mine.
//
// The grid can wrap at its edges (the sphere's board wraps both ways), and
// can have walls: cells that are not part of the field (they hold no mine,
// are not anyone's neighbour and cannot be uncovered), which give the field
// its shape (the owner's arenas: a cross, compartments and others).

#include <cstdint>
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

    // `walls`: width x height, row by row, true for a wall; empty for none.
    Minefield(int width, int height, int mines, std::uint32_t seed, bool wrap = false, std::vector<bool> walls = {});

    int width() const { return width_; }
    int height() const { return height_; }
    int mines() const { return mines_; }
    State state() const { return state_; }
    const Cell& cell(int index) const { return cells_[static_cast<std::size_t>(index)]; }
    int size() const { return width_ * height_; }
    bool wall(int index) const { return !walls_.empty() && walls_[static_cast<std::size_t>(index)]; }
    // Cells that are part of the field (not walls).
    int playable() const { return playable_; }
    int flags() const { return flags_; }
    // The mine uncovered that lost the game, or -1.
    int exploded() const { return exploded_; }

    // Uncovers a cell (placing the mines on the first one). On an uncovered
    // number whose flagged neighbours match it, uncovers its other
    // neighbours instead. Returns whether anything changed.
    bool open(int index);
    // Flags or unflags a covered cell while playing.
    bool toggle_flag(int index);

    // A cell's neighbours that are part of the field.
    std::vector<int> neighbours(int index) const;

private:
    void place_mines(int safe);
    void flood(int index);
    void check_won();

    int width_, height_, mines_;
    bool wrap_;
    std::uint32_t seed_;
    std::vector<bool> walls_;
    int playable_ = 0;
    State state_ = State::Ready;
    std::vector<Cell> cells_;
    int flags_ = 0;
    int opened_ = 0;
    int exploded_ = -1;
};

// The arenas (the owner's choices, 2026-11-13), in order of size, each a
// little harder than the last: Mini (12 x 12) and Half (16 x 16), framed by
// the purple wall; the shapes on the whole sphere, with walls of their own,
// Bow, Crossover, Decompartmental and Horizon; and Full (the whole 32 x 32
// sphere, which wraps). Each is centred on the cursor's starting cell.
struct MinefieldArena {
    const char* name;
    int width, height;          // the field (the walls inside it below)
    int mines;
    std::vector<bool> walls;    // width x height, row by row; empty for none
};
const std::vector<MinefieldArena>& minefield_arenas();
// The board size the arenas are made for (the sphere's, which wraps).
constexpr int minefield_board = 32;

} // namespace tetrisphere
