#include "tetrisphere/minefield.h"

#include <cassert>
#include <set>
#include <string>

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
        assert(minefield_status(field) == "Cleared! Every mine found.");
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
        assert(minefield_status(field) == "9 mines left");
        assert(field.toggle_flag(mine) && field.flags() == 0);
        assert(field.open(mine) && field.state() == Minefield::State::Lost && field.exploded() == mine);
        const auto cells = minefield_cells(field);
        assert(cells.size() == 81 && cells[static_cast<std::size_t>(mine)].classes == "cell mine hit" &&
               cells[static_cast<std::size_t>(mine)].action.empty());
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
    // List items: covered cells uncover with A and flag with Alt; numbers show.
    {
        Minefield field(9, 9, 10, 11);
        auto cells = minefield_cells(field, 40);
        assert(cells[40].focus && cells[0].classes == "cell covered" && cells[0].action == "command:minefield_open:0" &&
               cells[0].alt == "command:minefield_flag:0" && cells[0].alt_label == "Flag");
        field.toggle_flag(0);
        cells = minefield_cells(field);
        assert(cells[0].classes == "cell flag" && cells[0].alt_label == "Unflag" && !cells[0].icon.empty());
        field.toggle_flag(0);
        field.open(40);
        cells = minefield_cells(field);
        assert(cells[40].classes == "cell open" && cells[40].label.empty());
        for (int i = 0; i < field.size(); ++i)
            if (field.cell(i).open && field.cell(i).count > 0)
                assert(cells[static_cast<std::size_t>(i)].label == std::to_string(field.cell(i).count) &&
                       cells[static_cast<std::size_t>(i)].classes == "cell open n" + std::to_string(field.cell(i).count));
    }
    assert(minefield_sizes().size() == 3 && minefield_size_choices()[0].action == "command:minefield_start:0");
    return 0;
}
