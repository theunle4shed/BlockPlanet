#include "tetrisphere/minefield.h"

#include <algorithm>
#include <random>
#include <utility>

namespace tetrisphere {

Minefield::Minefield(int width, int height, int mines, std::uint32_t seed, bool wrap, std::vector<bool> walls)
    : width_(std::max(1, width)), height_(std::max(1, height)), mines_(0), wrap_(wrap), seed_(seed),
      walls_(std::move(walls)), cells_(static_cast<std::size_t>(width_ * height_)) {
    if (walls_.size() != static_cast<std::size_t>(size())) walls_.clear();
    for (int i = 0; i < size(); ++i) playable_ += !wall(i);
    // At least the first cell and its neighbours stay clear.
    mines_ = std::clamp(mines, 0, std::max(0, playable_ - 9));
}

std::vector<int> Minefield::neighbours(int index) const {
    std::vector<int> out;
    const int x = index % width_, y = index / width_;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (wrap_) {
                nx = (nx + width_) % width_;
                ny = (ny + height_) % height_;
            } else if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
                continue;
            }
            const int n = ny * width_ + nx;
            if (wall(n)) continue;
            if (n != index && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
    return out;
}

void Minefield::place_mines(int safe) {
    std::vector<int> free;
    const auto around = neighbours(safe);
    for (int i = 0; i < size(); ++i)
        if (i != safe && !wall(i) && std::find(around.begin(), around.end(), i) == around.end()) free.push_back(i);
    std::mt19937 random(seed_);
    std::shuffle(free.begin(), free.end(), random);
    const int count = std::min<int>(mines_, static_cast<int>(free.size()));
    mines_ = count;
    for (int i = 0; i < count; ++i) cells_[static_cast<std::size_t>(free[static_cast<std::size_t>(i)])].mine = true;
    for (int i = 0; i < size(); ++i)
        for (int n : neighbours(i)) cells_[static_cast<std::size_t>(i)].count += cells_[static_cast<std::size_t>(n)].mine;
    state_ = State::Playing;
}

void Minefield::flood(int index) {
    std::vector<int> todo{index};
    while (!todo.empty()) {
        const int i = todo.back();
        todo.pop_back();
        auto& c = cells_[static_cast<std::size_t>(i)];
        if (c.open || c.flag || c.mine) continue;
        c.open = true;
        ++opened_;
        if (c.count == 0)
            for (int n : neighbours(i)) todo.push_back(n);
    }
}

void Minefield::check_won() {
    if (state_ == State::Playing && opened_ == playable_ - mines_) {
        state_ = State::Won;
        // Every mine shown as flagged.
        for (auto& c : cells_)
            if (c.mine && !c.flag) {
                c.flag = true;
                ++flags_;
            }
    }
}

bool Minefield::open(int index) {
    if (index < 0 || index >= size() || wall(index)) return false;
    if (state_ == State::Ready) place_mines(index);
    if (state_ != State::Playing) return false;
    auto& c = cells_[static_cast<std::size_t>(index)];
    if (c.flag) return false;
    std::vector<int> targets;
    if (c.open) {
        // A number with as many flags around it: its other neighbours.
        int flagged = 0;
        for (int n : neighbours(index)) flagged += cells_[static_cast<std::size_t>(n)].flag;
        if (c.count == 0 || flagged != c.count) return false;
        for (int n : neighbours(index)) {
            const auto& other = cells_[static_cast<std::size_t>(n)];
            if (!other.open && !other.flag) targets.push_back(n);
        }
        if (targets.empty()) return false;
    } else {
        targets.push_back(index);
    }
    for (int t : targets) {
        auto& target = cells_[static_cast<std::size_t>(t)];
        if (target.mine) {
            target.open = true;
            exploded_ = t;
            state_ = State::Lost;
            return true;
        }
        flood(t);
    }
    check_won();
    return true;
}

bool Minefield::toggle_flag(int index) {
    if (index < 0 || index >= size() || wall(index) || state_ == State::Won || state_ == State::Lost) return false;
    auto& c = cells_[static_cast<std::size_t>(index)];
    if (c.open) return false;
    c.flag = !c.flag;
    flags_ += c.flag ? 1 : -1;
    return true;
}

const std::vector<MinefieldArena>& minefield_arenas() {
    // A shape on the whole sphere from the owner's pictures (1-based row and
    // column, as drawn), with mines at the Full field's density (130 of 1,024).
    const auto shape = [](const char* name, auto playable) {
        MinefieldArena a{name, minefield_board, minefield_board, 0, std::vector<bool>(std::size_t(minefield_board * minefield_board))};
        int cells = 0;
        for (int r = 1; r <= minefield_board; ++r)
            for (int c = 1; c <= minefield_board; ++c) {
                const bool in = playable(r, c);
                a.walls[std::size_t((r - 1) * minefield_board + (c - 1))] = !in;
                cells += in;
            }
        a.mines = (cells * 130 + 512) / 1024;
        return a;
    };
    static const std::vector<MinefieldArena> list = [&] {
        std::vector<MinefieldArena> l;
        l.push_back({"Full", 32, 32, 130, {}});
        l.push_back({"Half", 16, 16, 32, {}});
        l.push_back({"Mini", 12, 12, 20, {}});
        l.push_back(shape("Crossover", [](int r, int c) { return (c >= 12 && c <= 21) || (r >= 12 && r <= 21); }));
        l.push_back(shape("Decompartmental", [](int r, int c) {
            if ((r >= 1 && r <= 4) || (r >= 17 && r <= 20)) return false;
            if (r <= 16) return !((c >= 1 && c <= 4) || (c >= 17 && c <= 20));
            return !((c >= 9 && c <= 12) || (c >= 25 && c <= 28));
        }));
        l.push_back(shape("Bow", [](int r, int c) {
            return (c >= 13 && c <= 20) || (r >= 13 && r <= 20) || (r >= 9 && r <= 24 && c >= 9 && c <= 24);
        }));
        // Rows 11 to 22 walled: the field is one band of 20 rows over the poles.
        l.push_back(shape("Horizon", [](int r, int) { return r <= 10 || r >= 23; }));
        return l;
    }();
    return list;
}

} // namespace tetrisphere
