#include "tetrisphere/mode_launcher.h"

#include "tetrisphere/guest_memory.h"

#include <cassert>
#include <vector>

using namespace tetrisphere;
using namespace tetrisphere::guest;
using Clock = ModeLauncher::Clock;

int main() {
    std::vector<std::uint8_t> rdram(8u * 1024u * 1024u, 0);
    auto* m = rdram.data();
    auto set = [&](std::uint32_t a, std::int16_t v) { store16(m, a, v); };
    // The title: main row 0, no player page.
    set(0x800E121C, 0);
    set(0x800E1220, -1);
    set(0x800E1222, -1);
    set(0x800E1224, -1);

    assert(launch_mode_for_command("story") == LaunchMode::Rescue);
    assert(launch_mode_for_command("lines") == LaunchMode::Lines);
    assert(launch_mode_for_command("versus") == LaunchMode::VsCpu);
    assert(!launch_mode_for_command("credits"));

    ModeLauncher launcher;
    std::uint16_t buttons = 0;
    auto t = Clock::now();
    assert(launcher.update(m, t, buttons) == ModeLauncher::Result::Idle && buttons == 0);

    // Refused in play and inside another page.
    set(0x800E121C, -1);
    assert(!launcher.request(LaunchMode::Puzzle, m, t));
    set(0x800E121C, 4);
    set(0x800E1220, 2);
    assert(!launcher.request(LaunchMode::Puzzle, m, t));
    set(0x800E1220, -1);

    assert(launcher.request(LaunchMode::Puzzle, m, t) && launcher.active());
    // Main menu: SINGLE is selected and A is pressed, then released.
    bool pressed = false;
    for (int i = 0; i < 20; ++i) {
        t += std::chrono::milliseconds(50);
        assert(launcher.update(m, t, buttons) == ModeLauncher::Result::Working);
        pressed = pressed || buttons == 0x8000;
    }
    assert(pressed && load16(m, 0x800E121C) == 0);
    // Player page open: START is selected.
    set(0x800E1222, 2);
    launcher.update(m, t += std::chrono::milliseconds(50), buttons);
    assert(load16(m, 0x800E1222) == 0);
    // Mode list open: Puzzle's row (2) is selected.
    set(0x800E1224, 0);
    launcher.update(m, t += std::chrono::milliseconds(50), buttons);
    assert(load16(m, 0x800E1224) == 2);
    // Play begins.
    set(0x800E121C, -1);
    assert(launcher.update(m, t += std::chrono::milliseconds(50), buttons) == ModeLauncher::Result::Started);
    assert(!launcher.active() && buttons == 0);

    // Gives up after a while.
    set(0x800E121C, 0);
    set(0x800E1222, -1);
    set(0x800E1224, -1);
    assert(launcher.request(LaunchMode::Lines, m, t));
    assert(launcher.update(m, t + std::chrono::seconds(20), buttons) == ModeLauncher::Result::TimedOut);
    assert(!launcher.active());

    // Training Basics: main menu row 4 (TRAINING), then BASICS (0) on the
    // Training page.
    assert(launch_mode_for_command("training") == LaunchMode::TrainingBasics);
    set(0x800E121C, 0);
    assert(launcher.request(LaunchMode::TrainingBasics, m, t));
    launcher.update(m, t += std::chrono::milliseconds(50), buttons);
    assert(load16(m, 0x800E121C) == 4);
    set(0x800E1220, 2);
    launcher.update(m, t += std::chrono::milliseconds(50), buttons);
    assert(load16(m, 0x800E1220) == 0);
    set(0x800E121C, -1);
    assert(launcher.update(m, t += std::chrono::milliseconds(50), buttons) == ModeLauncher::Result::Started);

    // Training Puzzle: PUZZLE (2) on the Training page.
    set(0x800E121C, 0);
    set(0x800E1220, -1);
    assert(launcher.request(LaunchMode::TrainingPuzzle, m, t));
    launcher.update(m, t += std::chrono::milliseconds(50), buttons);
    assert(load16(m, 0x800E121C) == 4);
    set(0x800E1220, 0);
    launcher.update(m, t += std::chrono::milliseconds(50), buttons);
    assert(load16(m, 0x800E1220) == 2);
    return 0;
}
