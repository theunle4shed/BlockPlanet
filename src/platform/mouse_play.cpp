#include "tetrisphere/mouse_play.h"

#include <algorithm>
#include <cstdlib>

namespace tetrisphere {
namespace {
constexpr std::uint16_t n64_a = 0x8000, n64_b = 0x4000, n64_up = 0x0800, n64_down = 0x0400, n64_left = 0x0200,
                        n64_right = 0x0100, n64_c_down = 0x0004;
constexpr int click_slack = 8;   // pixels a click may wander and still be a click
constexpr int press_frames = 3;  // a step's press, then as long released
constexpr int max_queued = 8;    // cells queued at most each way
} // namespace

void MousePlay::set_window_height(int height) {
    std::lock_guard lock(mutex_);
    cell_ = std::max(16, height / 12);
}

void MousePlay::queue_steps_locked(int dx, int dy) {
    steps_x_ = std::clamp(steps_x_ + dx, -max_queued, max_queued);
    steps_y_ = std::clamp(steps_y_ + dy, -max_queued, max_queued);
}

void MousePlay::motion(int dx, int dy) {
    std::lock_guard lock(mutex_);
    if (!left_ && !right_) return;
    drag_x_ += dx, drag_y_ += dy;
    if (!dragged_ && std::abs(drag_x_) + std::abs(drag_y_) <= click_slack) return;
    dragged_ = true;
    carry_x_ += dx, carry_y_ += dy;
    const int cx = carry_x_ / cell_, cy = carry_y_ / cell_;
    carry_x_ -= cx * cell_, carry_y_ -= cy * cell_;
    if (cx == 0 && cy == 0) return;
    // Screen y grows downwards; the cursor's "up" is +.
    if (right_) queue_steps_locked(cx, -cy);   // the piece follows the pointer
    else queue_steps_locked(-cx, cy);          // the sphere follows the pointer: the cursor moves the other way
}

void MousePlay::button(int which, bool down) {
    std::lock_guard lock(mutex_);
    if (which == 1) {
        if (down) {
            left_ = true, dragged_ = false, drag_x_ = drag_y_ = carry_x_ = carry_y_ = 0;
        } else {
            if (left_ && !dragged_) drop_ = press_frames;
            left_ = false;
        }
    } else if (which == 3) {
        right_ = down;
        if (down) dragged_ = false, drag_x_ = drag_y_ = carry_x_ = carry_y_ = 0;
    } else if (which == 2 && down) {
        magic_ = press_frames;
    }
}

void MousePlay::wheel(int x, int y) {
    std::lock_guard lock(mutex_);
    queue_steps_locked(x, y);
}

void MousePlay::reset() {
    std::lock_guard lock(mutex_);
    left_ = right_ = dragged_ = false;
    drag_x_ = drag_y_ = carry_x_ = carry_y_ = steps_x_ = steps_y_ = pulse_ = drop_ = magic_ = 0;
    pulse_bits_ = 0;
}

std::uint16_t MousePlay::next_frame() {
    std::lock_guard lock(mutex_);
    std::uint16_t bits = 0;
    if (right_) bits |= n64_b;
    if (drop_ > 0) bits |= n64_a, --drop_;
    if (magic_ > 0) bits |= n64_c_down, --magic_;
    if (pulse_ > 0) {
        bits |= pulse_bits_;
        if (--pulse_ == 0) pulse_ = -press_frames;
    } else if (pulse_ < 0) {
        ++pulse_;
    } else if (steps_x_ != 0 || steps_y_ != 0) {
        // The next step: one cell on each axis that has steps queued (so
        // diagonals move both ways at once).
        pulse_bits_ = 0;
        if (steps_x_ > 0) pulse_bits_ |= n64_right, --steps_x_;
        else if (steps_x_ < 0) pulse_bits_ |= n64_left, ++steps_x_;
        if (steps_y_ > 0) pulse_bits_ |= n64_up, --steps_y_;
        else if (steps_y_ < 0) pulse_bits_ |= n64_down, ++steps_y_;
        bits |= pulse_bits_;
        pulse_ = press_frames - 1;
        if (pulse_ == 0) pulse_ = -press_frames;
    }
    return bits;
}

} // namespace tetrisphere
