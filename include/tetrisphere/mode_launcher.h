#pragma once

// Starts the game's modes from BlockPlanet's menus by driving the original
// menus: it selects the item and presses confirm, so the game sets up the
// mode exactly as if the player had chosen it. See docs/UI.md.
//
// Original menu state (NTPE rev0):
//   0x800E121C  main menu row (SINGLE = 0), -1 when no menu is shown (play)
//   0x800E1222  row on the player page (START = 0), -1 when not shown
//   0x800E1224  row in its mode list, -1 until START is chosen
// Mode list rows (func_8004127C's table at 0x800EDAE4): 0 Rescue,
// 1 Hide & Seek, 2 Puzzle, 3 Time Trial, 4 VS CPU, 5 mode 9 (not listed in
// the original menu; probably Lines).
// Training is main menu row 4, then a page (0x800E1220) of BASICS 0,
// ADVANCED 1 and PUZZLE 2. Story's Chapter 0 is Training Basics;
// Puzzle's Chapter 0 is Training Puzzle.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace tetrisphere {

enum class LaunchMode { Rescue, HideAndSeek, Puzzle, TimeTrial, VsCpu, Lines, TrainingBasics, TrainingPuzzle };

// The menu command (see assets/ui/screens) for a mode, if it is one.
std::optional<LaunchMode> launch_mode_for_command(const std::string& command);
const char* launch_mode_name(LaunchMode mode);

class ModeLauncher {
public:
    using Clock = std::chrono::steady_clock;

    // Starts launching. Refused (false) during play and inside other
    // original menu pages such as Options: modes start from the main menu,
    // the title or the player page.
    bool request(LaunchMode mode, const std::uint8_t* rdram, Clock::time_point now);
    bool active() const { return mode_.has_value(); }

    enum class Result { Idle, Working, Started, TimedOut };
    // Call every main-loop pass. Sets `buttons` to the N64 buttons to hold on
    // port 1 while working (0 between presses).
    Result update(std::uint8_t* rdram, Clock::time_point now, std::uint16_t& buttons);

private:
    std::optional<LaunchMode> mode_;
    Clock::time_point started_{};
    Clock::time_point phase_start_{};
    bool pressing_ = false;
};

} // namespace tetrisphere
