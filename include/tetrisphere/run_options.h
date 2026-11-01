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
    bool perf_log = false;
};

RunOptions parse_run_options(int argc, const char* const argv[]);
const char* run_options_help();

} // namespace tetrisphere
