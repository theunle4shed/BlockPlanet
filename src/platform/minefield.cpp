#include "tetrisphere/minefield.h"

#include <algorithm>
#include <random>

namespace tetrisphere {

Minefield::Minefield(int width, int height, int mines, std::uint32_t seed, bool wrap)
    : width_(std::max(1, width)), height_(std::max(1, height)), mines_(0), wrap_(wrap), seed_(seed),
      cells_(static_cast<std::size_t>(width_ * height_)) {
    // At least the first cell and its neighbours stay clear.
    mines_ = std::clamp(mines, 0, std::max(0, size() - 9));
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
            if (n != index && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
    return out;
}

void Minefield::place_mines(int safe) {
    std::vector<int> free;
    const auto around = neighbours(safe);
    for (int i = 0; i < size(); ++i)
        if (i != safe && std::find(around.begin(), around.end(), i) == around.end()) free.push_back(i);
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
    if (state_ == State::Playing && opened_ == size() - mines_) {
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
    if (index < 0 || index >= size()) return false;
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
    if (index < 0 || index >= size() || state_ == State::Won || state_ == State::Lost) return false;
    auto& c = cells_[static_cast<std::size_t>(index)];
    if (c.open) return false;
    c.flag = !c.flag;
    flags_ += c.flag ? 1 : -1;
    return true;
}

const std::vector<MinefieldSize>& minefield_sizes() {
    static const std::vector<MinefieldSize> sizes{
        {"Small", 9, 9, 10},
        {"Medium", 12, 12, 24},
        {"Large", 16, 16, 40},
    };
    return sizes;
}

std::vector<blockplanet::ui::ListItem> minefield_size_choices() {
    std::vector<blockplanet::ui::ListItem> items;
    int i = 0;
    for (const auto& s : minefield_sizes()) {
        blockplanet::ui::ListItem item{"size" + std::to_string(i), s.label,
                                       std::to_string(s.width) + " × " + std::to_string(s.height) + " · " +
                                           std::to_string(s.mines) + " mines",
                                       "command:minefield_start:" + std::to_string(i), true};
        items.push_back(item);
        ++i;
    }
    return items;
}

std::vector<blockplanet::ui::ListItem> minefield_cells(const Minefield& field, int focus) {
    std::vector<blockplanet::ui::ListItem> items;
    const bool over = field.state() == Minefield::State::Won || field.state() == Minefield::State::Lost;
    for (int i = 0; i < field.size(); ++i) {
        const auto& c = field.cell(i);
        blockplanet::ui::ListItem item;
        item.id = "cell" + std::to_string(i);
        item.focus = i == focus;
        std::string classes = "cell";
        if (over && c.mine) {
            item.icon = c.flag ? "" : "";  // a flag kept, or the bomb shown
            classes += i == field.exploded() ? " mine hit" : c.flag ? " flag" : " mine";
        } else if (over && c.flag) {
            item.icon = "";
            classes += " wrong";  // flagged, but no mine
        } else if (c.flag) {
            item.icon = "";
            classes += " flag";
        } else if (c.open) {
            classes += " open";
            if (c.count > 0) {
                item.label = std::to_string(c.count);
                classes += " n" + std::to_string(c.count);
            }
        } else {
            classes += " covered";
        }
        item.classes = classes;
        if (!over) {
            item.action = "command:minefield_open:" + std::to_string(i);
            item.alt = "command:minefield_flag:" + std::to_string(i);
            item.alt_label = c.flag ? "Unflag" : "Flag";
        }
        items.push_back(item);
    }
    return items;
}

std::string minefield_status(const Minefield& field) {
    switch (field.state()) {
    case Minefield::State::Won: return "Cleared! Every mine found.";
    case Minefield::State::Lost: return "Boom! You hit a mine.";
    default: break;
    }
    const int left = field.mines() - field.flags();
    return std::to_string(left) + (left == 1 ? " mine left" : " mines left");
}

} // namespace tetrisphere
