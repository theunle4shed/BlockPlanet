#include "tetrisphere/mouse_play.h"

#include <cassert>
#include <vector>

using namespace tetrisphere;

namespace {
std::vector<std::uint16_t> frames(MousePlay& m, int n) {
    std::vector<std::uint16_t> out;
    for (int i = 0; i < n; ++i) out.push_back(m.next_frame());
    return out;
}
int presses(const std::vector<std::uint16_t>& f, std::uint16_t bit) {
    int n = 0;
    for (std::size_t i = 0; i < f.size(); ++i)
        if ((f[i] & bit) && (i == 0 || !(f[i - 1] & bit))) ++n;
    return n;
}
} // namespace

int main() {
    // A click (no drag) drops; nothing moves.
    {
        MousePlay m;
        m.set_window_height(720);  // a cell is 60 pixels
        m.button(1, true);
        m.motion(3, 2);
        m.button(1, false);
        const auto f = frames(m, 10);
        assert(presses(f, 0x8000) == 1 && presses(f, 0x0F00) == 0);
    }
    // Dragging right three cells turns the sphere right: three steps left, no drop.
    {
        MousePlay m;
        m.set_window_height(720);
        m.button(1, true);
        for (int i = 0; i < 18; ++i) m.motion(10, 0);  // 180 px = 3 cells
        m.button(1, false);
        const auto f = frames(m, 40);
        assert(presses(f, 0x0200) == 3 && presses(f, 0x0100) == 0 && presses(f, 0x8000) == 0);
    }
    // A diagonal drag: left and down together (pointer up-left: cursor right and down).
    {
        MousePlay m;
        m.set_window_height(720);
        m.button(1, true);
        m.motion(-130, -130);
        const auto f = frames(m, 20);
        int both = 0;
        for (auto b : f) both += (b & 0x0100) && (b & 0x0400);
        assert(presses(f, 0x0100) == 2 && presses(f, 0x0400) == 2 && both > 0);
    }
    // The right button holds slide, and the piece follows the pointer.
    {
        MousePlay m;
        m.set_window_height(720);
        m.button(3, true);
        m.motion(70, 0);
        const auto f = frames(m, 12);
        for (auto b : f) assert(b & 0x4000);
        assert(presses(f, 0x0100) == 1);
        m.button(3, false);
        assert(!(m.next_frame() & 0x4000));
    }
    // The wheels step the cursor; the middle button is magic.
    {
        MousePlay m;
        m.wheel(0, 2);
        m.wheel(-1, 0);
        m.button(2, true);
        const auto f = frames(m, 20);
        assert(presses(f, 0x0800) == 2 && presses(f, 0x0200) == 1 && presses(f, 0x0004) == 1);
    }
    // Reset forgets everything.
    {
        MousePlay m;
        m.wheel(0, 5);
        m.button(3, true);
        m.reset();
        for (auto b : frames(m, 10)) assert(b == 0);
    }
    return 0;
}
