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

} // namespace tetrisphere
