#pragma once

// The mouse in play (the owner's design, 2026-11-15), turned into the N64
// buttons the game reads, frame by frame:
//   left button held and dragged  turns the sphere (what is under the pointer
//                                 follows it: drag right, and the sphere turns
//                                 right), any direction including diagonals
//   left click (no drag)          drop (A)
//   right button held             slide (B), and dragging moves the piece the
//                                 way the pointer goes
//   wheel up / down               the cursor up / down a cell a notch
//   horizontal wheel              the cursor left / right a cell a notch
//   middle button                 magic (C-down)
// A step is a short press of the d-pad (a few frames), then a few frames
// released, so the game moves one cell for each.

#include <cstdint>
#include <mutex>

namespace tetrisphere {

class MousePlay {
public:
    // The window's height in pixels: a cell of drag is a twelfth of it.
    void set_window_height(int height);
    void motion(int dx, int dy);
    void button(int which, bool down);  // 1 left, 2 middle, 3 right (SDL's numbering)
    void wheel(int x, int y);
    // Forget everything held or queued (leaving play, opening the menus).
    void reset();
    // The buttons for the next frame (call once a frame).
    std::uint16_t next_frame();

private:
    void queue_steps_locked(int dx, int dy);
    std::mutex mutex_;
    int cell_ = 60;
    bool left_ = false, right_ = false, dragged_ = false;
    int drag_x_ = 0, drag_y_ = 0;   // pixels moved since the button went down (to tell a click from a drag)
    int carry_x_ = 0, carry_y_ = 0;  // pixels not yet turned into steps
    int steps_x_ = 0, steps_y_ = 0;  // queued cells: + right / up, - left / down
    int pulse_ = 0;                  // frames left in the current press (+) or gap (-)
    std::uint16_t pulse_bits_ = 0;
    int drop_ = 0, magic_ = 0;       // frames left of a drop or magic press
};

} // namespace tetrisphere
