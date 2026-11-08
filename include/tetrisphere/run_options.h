#pragma once

#include <optional>
#include <string>

namespace tetrisphere {

enum class PresentationFilter { Nearest, Linear, Pixel };

// RT64 presentation modes. Console shows frames with the N64's own buffering
// delay and is Blocksphere's default. SkipBuffering presents the newest frame
// the game has finished. Early presents each frame as soon as it is drawn.
enum class PresentMode { Console, SkipBuffering, Early };

constexpr const char* present_mode_name(PresentMode mode) {
    switch (mode) {
        case PresentMode::Console: return "console";
        case PresentMode::SkipBuffering: return "skip-buffering";
        case PresentMode::Early: return "early";
    }
    return "console";
}

struct RunOptions {
    bool valid = true;
    bool help = false;
    std::string error;
    std::string rom_path;
    std::optional<bool> fullscreen;
    std::optional<int> msaa_samples;
    std::optional<PresentationFilter> presentation_filter;
    std::optional<bool> three_point_filter;
    std::optional<PresentMode> present_mode;
    // RT64 "render to RAM": copy every finished frame back into game memory.
    // Needed only where the game reads frames with the CPU, and it makes the
    // game wait for the GPU every frame.
    std::optional<bool> framebuffer_readback;
    // How much of the sphere to draw: true for Extended (overrides the
    // saved SPHERE setting for this run).
    std::optional<bool> sphere_extended;
    // Play as this profile (created if new) and make it the active one.
    std::optional<std::string> profile;
    // Play as a guest: nothing is saved and no trophies are earned.
    bool guest = false;
    bool perf_log = false;
    // Casual play: no game over. Trophies are paused.
    bool casual = false;
    // Start Rescue at this chapter (1-based), for testing. Trophies are paused.
    std::optional<int> start_chapter;
    // Every Story episode and stage open in the menus, for testing.
    bool unlock_all = false;
    // Play the credits once the title shows (Credits on the main menu
    // restarts the game with this; not kept on later restarts).
    bool credits = false;
};

RunOptions parse_run_options(int argc, const char* const argv[]);
const char* run_options_help();

} // namespace tetrisphere
