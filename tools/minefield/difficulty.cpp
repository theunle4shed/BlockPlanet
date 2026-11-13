// Minefield difficulty: plays each arena many times with a solver that sees
// only what a player sees (the numbers uncovered and its own flags), and
// reports how often it wins and how many forced guesses a game takes (the
// owner's question, 2026-11-13).
//
// The solver: (1) a number with as many flags round it as its count opens
// its other covered neighbours; a number whose covered neighbours are as many
// as the mines it still needs flags them all; (2) for two numbers whose
// covered neighbours are one inside the other, the difference is all safe or
// all mines when the counts say so; (3) otherwise it guesses the covered cell
// that looks safest (beside a number: its share of that number's remaining
// mines, the worst one; elsewhere: the mines left over the cells left).
// A real player can do a little better (longer chains of reasoning, counting
// the mines at the end), so these are a cautious estimate.
//
//   g++ -O2 -std=c++20 -Iinclude tools/minefield/difficulty.cpp src/platform/minefield.cpp -o difficulty
//   ./difficulty [games per arena, default 2000] [--sweep] [--hints]

#include "tetrisphere/minefield.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using tetrisphere::Minefield;

namespace {

struct Result {
    bool won = false;
    int guesses = 0;       // forced guesses after the first uncover
    int lost_on_guess = 0; // 1 if the loss came on a guess
};

bool hints = false;

Result play(const tetrisphere::MinefieldArena& arena, std::uint32_t seed) {
    const bool wrap = arena.width == tetrisphere::minefield_board && arena.height == tetrisphere::minefield_board;
    Minefield field(arena.width, arena.height, arena.mines, seed, wrap, arena.walls);
    const int w = field.width(), h = field.height();
    // The first uncover where the game puts the cursor: the field's middle,
    // or the nearest cell of the field.
    int start = -1, best = 1 << 30;
    for (int i = 0; i < field.size(); ++i) {
        if (field.wall(i)) continue;
        const int dx = i % w - w / 2, dy = i / w - h / 2, d = dx * dx + dy * dy;
        if (d < best) best = d, start = i;
    }
    field.open(start);
    // Hints (--hints): every separate area of the field (Decompartmental's
    // compartments) gets one empty cell uncovered at the start, as the game
    // could do for the player.
    if (hints) {
        std::vector<int> area(std::size_t(field.size()), -1);
        int areas = 0;
        for (int i = 0; i < field.size(); ++i) {
            if (field.wall(i) || area[std::size_t(i)] >= 0) continue;
            std::vector<int> todo{i}, members;
            area[std::size_t(i)] = areas;
            while (!todo.empty()) {
                const int c = todo.back();
                todo.pop_back();
                members.push_back(c);
                for (int n : field.neighbours(c))
                    if (area[std::size_t(n)] < 0) area[std::size_t(n)] = areas, todo.push_back(n);
            }
            bool opened = false;
            for (int c : members) opened = opened || field.cell(c).open;
            if (!opened) {
                // Its emptiest safe cell (a 0 if there is one).
                int pick = -1;
                for (int c : members)
                    if (!field.cell(c).mine && (pick < 0 || field.cell(c).count < field.cell(pick).count)) pick = c;
                if (pick >= 0) field.open(pick);
            }
            ++areas;
        }
    }
    Result r;
    std::vector<bool> flagged(std::size_t(field.size()), false);
    const auto covered = [&](int i) { return !field.wall(i) && !field.cell(i).open && !flagged[std::size_t(i)]; };
    while (field.state() == Minefield::State::Playing) {
        bool progress = false;
        // Each uncovered number: its covered neighbours and mines still to find.
        struct Constraint { std::vector<int> cells; int mines; };
        std::vector<Constraint> constraints;
        for (int i = 0; i < field.size(); ++i) {
            const auto& c = field.cell(i);
            if (!c.open || c.count == 0) continue;
            Constraint k{{}, c.count};
            for (int n : field.neighbours(i)) {
                if (flagged[std::size_t(n)]) --k.mines;
                else if (covered(n)) k.cells.push_back(n);
            }
            if (!k.cells.empty()) constraints.push_back(std::move(k));
        }
        std::set<int> safe, mine;
        for (const auto& k : constraints) {
            if (k.mines == 0) safe.insert(k.cells.begin(), k.cells.end());
            else if (k.mines == int(k.cells.size())) mine.insert(k.cells.begin(), k.cells.end());
        }
        if (safe.empty() && mine.empty())
            for (std::size_t a = 0; a < constraints.size(); ++a)
                for (std::size_t b = 0; b < constraints.size(); ++b) {
                    if (a == b) continue;
                    const auto& A = constraints[a];
                    const auto& B = constraints[b];
                    if (A.cells.size() >= B.cells.size()) continue;
                    if (!std::includes(B.cells.begin(), B.cells.end(), A.cells.begin(), A.cells.end())) continue;
                    std::vector<int> rest;
                    std::set_difference(B.cells.begin(), B.cells.end(), A.cells.begin(), A.cells.end(), std::back_inserter(rest));
                    const int m = B.mines - A.mines;
                    if (m == 0) safe.insert(rest.begin(), rest.end());
                    else if (m == int(rest.size())) mine.insert(rest.begin(), rest.end());
                }
        for (int i : mine)
            if (covered(i)) flagged[std::size_t(i)] = true, field.toggle_flag(i), progress = true;
        for (int i : safe)
            if (covered(i)) field.open(i), progress = true;
        if (progress) continue;
        // A guess: the covered cell that looks safest.
        int flags = 0, unknown = 0;
        for (int i = 0; i < field.size(); ++i) flags += flagged[std::size_t(i)], unknown += covered(i);
        if (unknown == 0) break;
        const double elsewhere = double(field.mines() - flags) / unknown;
        std::vector<double> risk(std::size_t(field.size()), -1.0);
        for (const auto& k : constraints)
            for (int i : k.cells)
                risk[std::size_t(i)] = std::max(risk[std::size_t(i)], double(k.mines) / double(k.cells.size()));
        int pick = -1;
        double lowest = 2.0;
        for (int i = 0; i < field.size(); ++i) {
            if (!covered(i)) continue;
            const double p = risk[std::size_t(i)] < 0 ? elsewhere : risk[std::size_t(i)];
            if (p < lowest) lowest = p, pick = i;
        }
        ++r.guesses;
        field.open(pick);
        if (field.state() == Minefield::State::Lost) r.lost_on_guess = 1;
    }
    r.won = field.state() == Minefield::State::Won;
    return r;
}

} // namespace

int main(int argc, char** argv) {
    const int games = argc > 1 ? std::max(1, std::atoi(argv[1])) : 2000;
    for (int i = 2; i < argc; ++i) hints = hints || std::string(argv[i]) == "--hints";
    // --sweep: the win rate at several densities, arena by arena.
    bool sweep = false;
    for (int i = 2; i < argc; ++i) sweep = sweep || std::string(argv[i]) == "--sweep";
    if (sweep) {
        const double densities[] = {0.08, 0.10, 0.125, 0.15, 0.175, 0.20, 0.225};
        std::printf("%-16s", "win % at");
        for (double d : densities) std::printf(" %6.1f%%", d * 100);
        std::printf("\n");
        for (auto arena : tetrisphere::minefield_arenas()) {
            int playable = 0;
            for (std::size_t i = 0; i < arena.walls.size(); ++i) playable += !arena.walls[i];
            if (arena.walls.empty()) playable = arena.width * arena.height;
            std::printf("%-16s", arena.name);
            for (double d : densities) {
                arena.mines = int(playable * d + 0.5);
                int won = 0;
                for (int g = 0; g < games; ++g) won += play(arena, std::uint32_t(g * 2654435761u + 17)).won;
                std::printf(" %6.1f%%", 100.0 * won / games);
            }
            std::printf("\n");
        }
        return 0;
    }
    std::printf("%-16s %5s %5s %7s %8s %9s %10s\n", "arena", "cells", "mines", "density", "won", "no guess", "guesses");
    for (const auto& arena : tetrisphere::minefield_arenas()) {
        int won = 0, clean = 0, guesses = 0, playable = 0;
        for (std::size_t i = 0; i < arena.walls.size(); ++i) playable += !arena.walls[i];
        if (arena.walls.empty()) playable = arena.width * arena.height;
        for (int g = 0; g < games; ++g) {
            const auto r = play(arena, std::uint32_t(g * 2654435761u + 17));
            won += r.won;
            clean += r.guesses == 0;
            guesses += r.guesses;
        }
        std::printf("%-16s %5d %5d %6.1f%% %7.1f%% %8.1f%% %10.2f\n", arena.name, playable, arena.mines,
                    100.0 * arena.mines / playable, 100.0 * won / games, 100.0 * clean / games, double(guesses) / games);
    }
    return 0;
}
