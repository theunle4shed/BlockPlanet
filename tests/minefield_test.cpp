#include "tetrisphere/minefield.h"

#include <cassert>
#include <set>

using namespace tetrisphere;

int main() {
    // Neighbours: eight inside, three in a corner; with wrapping, eight everywhere.
    {
        Minefield flat(5, 4, 3, 1);
        assert(flat.neighbours(6).size() == 8 && flat.neighbours(0).size() == 3);
        Minefield wrapped(5, 4, 3, 1, true);
        const auto around = wrapped.neighbours(0);
        assert(around.size() == 8 && std::set<int>(around.begin(), around.end()).count(19) == 1);
    }
    // The first cell and its neighbours are always clear, and the counts add up.
    for (std::uint32_t seed = 0; seed < 200; ++seed) {
        Minefield field(9, 9, 10, seed);
        assert(field.state() == Minefield::State::Ready);
        assert(field.open(40));
        assert(field.state() != Minefield::State::Lost);
        assert(!field.cell(40).mine && field.cell(40).open && field.cell(40).count == 0);
        for (int n : field.neighbours(40)) assert(!field.cell(n).mine && field.cell(n).open);
        int mines = 0;
        for (int i = 0; i < field.size(); ++i) {
            mines += field.cell(i).mine;
            int count = 0;
            for (int n : field.neighbours(i)) count += field.cell(n).mine;
            assert(field.cell(i).count == count);
        }
        assert(mines == 10 && field.mines() == 10);
    }
    // Uncovering every safe cell wins (and flags every mine); a mine loses.
    {
        Minefield field(9, 9, 10, 7);
        field.open(0);
        for (int i = 0; i < field.size(); ++i)
            if (!field.cell(i).mine) field.open(i);
        assert(field.state() == Minefield::State::Won && field.flags() == 10);
        assert(!field.open(1) && !field.toggle_flag(1));
    }
    {
        Minefield field(9, 9, 10, 7);
        field.open(0);
        int mine = -1;
        for (int i = 0; i < field.size() && mine < 0; ++i)
            if (field.cell(i).mine) mine = i;
        // A flag protects the cell; unflagged, it explodes.
        assert(field.toggle_flag(mine) && field.flags() == 1 && !field.open(mine));
        assert(field.toggle_flag(mine) && field.flags() == 0);
        assert(field.open(mine) && field.state() == Minefield::State::Lost && field.exploded() == mine);
    }
    // Chording: a number with its mines flagged uncovers the rest around it.
    {
        Minefield field(9, 9, 10, 3);
        field.open(40);
        int number = -1;
        for (int i = 0; i < field.size() && number < 0; ++i) {
            const auto& c = field.cell(i);
            if (!c.open || c.count == 0) continue;
            bool covered_safe = false;
            for (int n : field.neighbours(i)) covered_safe = covered_safe || (!field.cell(n).open && !field.cell(n).mine);
            if (covered_safe) number = i;
        }
        if (number >= 0) {
            assert(!field.open(number));  // not enough flags yet
            for (int n : field.neighbours(number))
                if (field.cell(n).mine) field.toggle_flag(n);
            assert(field.open(number));
            for (int n : field.neighbours(number)) assert(field.cell(n).open || field.cell(n).flag);
            assert(field.state() != Minefield::State::Lost);
        }
    }
    // Walls: a 9 x 9 field with its middle column walled off. Walls hold no
    // mines, are nobody's neighbours, cannot be uncovered or flagged, and the
    // field is won without them.
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        std::vector<bool> walls(81, false);
        for (int y = 0; y < 9; ++y) walls[std::size_t(y * 9 + 4)] = true;
        Minefield field(9, 9, 12, seed, false, walls);
        assert(field.playable() == 72 && field.wall(4) && !field.wall(3));
        assert(!field.open(4) && !field.toggle_flag(4));
        assert(field.neighbours(3 + 9 * 4).size() == 5);  // beside the wall: no wall neighbours
        field.open(9 * 4 + 1);
        for (int i = 0; i < field.size(); ++i) {
            assert(!(field.wall(i) && (field.cell(i).mine || field.cell(i).open)));
            if (!field.wall(i) && !field.cell(i).mine) field.open(i);
        }
        assert(field.state() == Minefield::State::Won);
    }
    return 0;
}
