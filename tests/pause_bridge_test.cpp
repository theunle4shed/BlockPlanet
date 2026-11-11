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
           out.items[1].action == "command:pause_restart_chapter" && out.items[1].label == "Restart Chapter" &&
           out.items[2].action == "command:pause_howto" && out.items[2].label == "How to Play" &&
           out.items[3].action == "open:options" && out.items[4].action == "command:pause_quit");
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
    store32(m, 0x8013D25C, 0x8013D27D);  // the lesson has a "skip to" text
    set(0x800E1228, 0);
    out = step(bridge, false);
    assert(out.open_menu && out.items.size() == 5 && out.items[1].label == "Restart Lesson" &&
           out.items[2].action == "command:pause_skip");
    out = step(bridge, false);  // menus are closed
    assert(out.inject && load16(m, 0x800E1228) == 0);
    set(0x800E1228, -1);  // resumed
    out = step(bridge, false);
    assert(!out.inject && !bridge.hide_original());

    // Ending the game's exit early after Resume (research): only once the
    // exit (0x800DFF08 = 5) has faded in (0x800E1202 = 0); then the row is
    // -1 and the bridge no longer writes it back (which re-paused the game).
    set(0x800E1228, 0);
    set(0x800E44A8, 1);
    out = step(bridge, false);
    assert(out.open_menu);
    bridge.choose(PauseBridge::Choice::Resume, t);
    out = step(bridge, false);
    assert(out.inject && load16(m, 0x800E1228) == 0);
    assert(!bridge.finish_resume(m));  // not in the exit yet
    set(0x800DFF08, 5);
    set(0x800E1202, 120);
    assert(!bridge.finish_resume(m));  // still fading
    set(0x800E1202, 0);
    set(0x8013FAD0, 1);  // the game's pause request flag
    assert(bridge.finish_resume(m) && load16(m, 0x800E1228) == -1 && load16(m, 0x800DFF08) == 0 &&
           load16(m, 0x8013FAD0) == 0);
    out = step(bridge, false);
    assert(!out.inject && !bridge.hide_original() && load16(m, 0x800E1228) == -1);
    assert(!bridge.finish_resume(m));  // once

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
    // A row set without a Start press (a chapter-end scene) is not a pause,
    // even if Start is pressed later while it lasts.
    PauseBridge scene;
    set(0x80112F9E, 3);
    set(0x800E1228, -1);
    step(scene, false);
    set(0x800E1228, 0);
    t += std::chrono::milliseconds(50);
    out = scene.update(m, false, t, false);
    assert(!out.open_menu && !scene.hide_original());
    t += std::chrono::milliseconds(50);
    out = scene.update(m, false, t, true);
    assert(!out.open_menu);
    set(0x800E1228, -1);
    t += std::chrono::milliseconds(50);
    scene.update(m, false, t, false);
    set(0x800E1228, 0);
    t += std::chrono::milliseconds(50);
    out = scene.update(m, false, t, true);
    assert(out.open_menu);  // a real pause afterwards
    // Story and Hide & Seek restart the chapter, after asking; others the level.
    assert(restarts_chapter(1) && restarts_chapter(5) && !restarts_chapter(4));
    // No "skip to" text: no Skip lesson.
    assert(pause_items(3, 1, false).size() == 4 && pause_items(3, 1, true).size() == 5);
    assert(pause_items(4, 0)[1].label == "Restart Level" && pause_items(4, 0)[1].action == "command:pause_restart");
    assert(pause_items(1, 0)[1].label == "Restart Chapter" &&
           pause_items(1, 0)[1].action == "command:pause_restart_chapter");
    // Restart is offered only where the same place can be started again.
    assert(can_restart(1, 0) && can_restart(9, 0) && can_restart(3, 3) && !can_restart(3, 2) && !can_restart(7, 0));

    // The end menus: BlockPlanet's panel with the game's items, rows in the
    // game's order, shown as Try again, Training, Exit.
    {
        std::vector<EndMenuBridge::Label> labels{{"TRY AGAIN", 100}, {"EXIT", 120}, {"TRAINING", 140}, {"PUSH", 10}};
        int exit_row = -2;
        const auto items = end_menu_items(labels, &exit_row);
        assert(items.size() == 3 && items[0].label == "Try Again" && items[1].label == "Training" && items[2].label == "Exit");
        assert(items[0].action == "command:end_choose:0" && items[1].action == "command:end_choose:2" &&
               items[2].action == "command:end_choose:1" && exit_row == 1 && items[0].focus);
        assert(end_menu_items({{"TRY AGAIN", 1}}).empty());  // no Exit: not the end menu
        const auto versus = end_menu_items({{"EXIT", 50}, {"CONTINUE", 30}});
        assert(versus.size() == 2 && versus[0].label == "Continue" && versus[0].action == "command:end_choose:0");

        EndMenuBridge end;
        set(0x800E121C, -1);
        set(0x800E44A8, 1);   // Story
        set(0x80112F9E, 3);   // hearts left
        set(0x800E1228, 0);   // a row in use with hearts left (a chapter's end): left alone
        set(0x800DFF08, 1);
        t += std::chrono::milliseconds(50);
        auto e = end.update(m, labels, false, t);
        assert(!e.open_menu && !e.active && !end.hide_original());
        // Game over: the grid while the menu fades in, the panel once it has.
        set(0x80112F9E, -1);
        set(0x800DFF08, 4);
        t += std::chrono::milliseconds(50);
        e = end.update(m, labels, false, t);
        assert(e.active && !e.open_menu);
        set(0x800DFF08, 1);
        t += std::chrono::milliseconds(50);
        e = end.update(m, labels, false, t);
        assert(e.open_menu && e.title == "Game Over" && e.items.size() == 3 && end.hide_original() && end.exit_row() == 1);
        t += std::chrono::milliseconds(50);
        assert(!end.update(m, {}, true, t).open_menu);  // open: nothing more (the game's text is hidden now)
        t += std::chrono::milliseconds(50);
        assert(!end.update(m, {}, false, t).open_menu);  // not yet open: not asked again at once
        t += std::chrono::milliseconds(600);
        assert(end.update(m, {}, false, t).open_menu);  // closed with Back: shown again
        // Choosing Training (row 2): the row is set and A pressed.
        end.choose(2, t);
        bool pressed = false;
        for (int k = 0; k < 10; ++k) {
            t += std::chrono::milliseconds(50);
            e = end.update(m, {}, true, t);
            assert(e.inject && load16(m, 0x800E1228) == 2);
            pressed = pressed || e.buttons == 0x8000;
        }
        assert(pressed);
        // The game leaves its menu: done.
        set(0x800E1228, -1);
        t += std::chrono::milliseconds(50);
        e = end.update(m, {}, false, t);
        assert(!e.inject && !e.active && !end.hide_original());
        // BlockPlanet's pause in charge: left alone.
        set(0x800E1228, 0);
        t += std::chrono::milliseconds(50);
        assert(!end.update(m, labels, false, t, true).open_menu);
        set(0x800E1228, -1);
        set(0x80112F9E, 3);
    }
    return 0;
}
