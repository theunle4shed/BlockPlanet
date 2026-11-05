#include "tetrisphere/pause_bridge.h"

#include "tetrisphere/guest_memory.h"

#include <cassert>
#include <vector>

using namespace tetrisphere;
using namespace tetrisphere::guest;
using Clock = PauseBridge::Clock;

int main() {
    std::vector<std::uint8_t> memory(0x800000);
    auto* m = memory.data();
    auto set = [&](std::uint32_t address, std::int16_t value) { store16(m, address, value); };
    auto t = Clock::now();
    auto step = [&](PauseBridge& b, bool menus) { t += std::chrono::milliseconds(50); return b.update(m, menus, t); };

    PauseBridge bridge;
    set(0x800E121C, -1);  // playing
    set(0x800E1228, -1);  // not paused
    set(0x800E44A8, 5);   // Hide & Seek
    auto out = step(bridge, false);
    assert(!out.open_menu && !out.inject && !bridge.hide_original());

    // The game pauses: the pause screen opens, without Skip outside Training.
    set(0x800E1228, 0);
    out = step(bridge, false);
    assert(out.open_menu && bridge.hide_original());
    assert(out.items.size() == 5 && out.items[0].action == "command:pause_resume" && out.items[0].focus &&
           out.items[1].action == "command:pause_restart" && out.items[1].label == "Restart level" &&
           out.items[2].action == "open:audio" && out.items[4].action == "command:pause_quit");
    out = step(bridge, true);
    assert(!out.open_menu && !out.inject);

    // Quit: closes the menus, sets the row to EXIT and presses A.
    bridge.choose(PauseBridge::Choice::Quit, t);
    bool pressed = false;
    for (int i = 0; i < 10; ++i) {
        out = step(bridge, i == 0);
        assert(out.inject && load16(m, 0x800E1228) == 3);
        pressed = pressed || out.buttons == 0x8000;
        if (i == 0) assert(out.close_menu);
    }
    assert(pressed);
    // The game leaves play: done.
    set(0x800E121C, 0);
    set(0x800E1228, -1);
    out = step(bridge, false);
    assert(!out.inject && !bridge.hide_original());

    // Back (menus closed by the player) resumes; Training adds Skip.
    set(0x800E121C, -1);
    set(0x800E44A8, 3);
    set(0x800E44AC, 1);  // Basics
    set(0x800E1228, 0);
    out = step(bridge, false);
    assert(out.open_menu && out.items.size() == 6 && out.items[1].label == "Restart lesson" &&
           out.items[2].action == "command:pause_skip");
    out = step(bridge, false);  // menus are closed
    assert(out.inject && load16(m, 0x800E1228) == 0);
    set(0x800E1228, -1);  // resumed
    out = step(bridge, false);
    assert(!out.inject && !bridge.hide_original());

    // F1 during play presses Start until the game pauses.
    bridge.request_pause(t);
    pressed = false;
    for (int i = 0; i < 10; ++i) {
        out = step(bridge, false);
        assert(out.inject);
        pressed = pressed || out.buttons == 0x1000;
    }
    assert(pressed);
    set(0x800E1228, 0);
    out = step(bridge, false);
    assert(!out.inject || out.buttons != 0x1000);
    assert(out.open_menu);
    // The game-over menu uses the pause row too, with hearts at -1: left alone.
    PauseBridge over;
    set(0x800E121C, -1);
    set(0x800E1228, -1);
    set(0x80112F9E, -1);
    step(over, false);
    set(0x800E1228, 0);
    out = step(over, false);
    assert(!out.open_menu && !out.inject && !over.hide_original());
    set(0x80112F9E, 3);
    out = step(over, false);
    assert(out.open_menu);
    // Story has no restart (TRY AGAIN after a game over restarts the chapter).
    assert(pause_items(1, 0)[1].action != "command:pause_restart");
    // Restart is offered only where the same place can be started again.
    assert(!can_restart(1, 0) && can_restart(9, 0) && can_restart(3, 3) && !can_restart(3, 2) && !can_restart(7, 0));
    return 0;
}
