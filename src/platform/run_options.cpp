#include "tetrisphere/run_options.h"
#include "tetrisphere/profiles.h"

#include <string_view>

namespace tetrisphere {
namespace {

bool parse_msaa(std::string_view value, int& samples) {
    if (value == "off" || value == "0") samples = 0;
    else if (value == "2" || value == "2x") samples = 2;
    else if (value == "4" || value == "4x") samples = 4;
    else if (value == "8" || value == "8x") samples = 8;
    else return false;
    return true;
}

RunOptions invalid(const std::string& message) {
    RunOptions result;
    result.valid = false;
    result.error = message;
    return result;
}

} // namespace

RunOptions parse_run_options(int argc, const char* const argv[]) {
    RunOptions result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            result.help = true;
            continue;
        }
        if (argument == "--fullscreen") {
            result.fullscreen = true;
            continue;
        }
        if (argument == "--windowed") {
            result.fullscreen = false;
            continue;
        }
        if (argument == "--msaa2x" || argument == "--msaa4x" || argument == "--msaa8x") {
            result.msaa_samples = argument[6] - '0';
            continue;
        }
        if (argument == "--msaa-off") {
            result.msaa_samples = 0;
            continue;
        }
        if (argument == "--msaa" || argument.starts_with("--msaa=")) {
            const auto value = argument == "--msaa"
                ? (index + 1 < argc ? std::string_view(argv[++index]) : std::string_view{})
                : argument.substr(7);
            int samples = 0;
            if (!parse_msaa(value, samples)) return invalid("MSAA must be off, 2x, 4x, or 8x");
            result.msaa_samples = samples;
            continue;
        }
        if (argument == "--presentation-filter" ||
            argument.starts_with("--presentation-filter=")) {
            const auto value = argument == "--presentation-filter"
                ? (index + 1 < argc ? std::string_view(argv[++index]) : std::string_view{})
                : argument.substr(sizeof("--presentation-filter=") - 1);
            if (value == "nearest") result.presentation_filter = PresentationFilter::Nearest;
            else if (value == "linear") result.presentation_filter = PresentationFilter::Linear;
            else if (value == "pixel") result.presentation_filter = PresentationFilter::Pixel;
            else return invalid("presentation filter must be nearest, linear, or pixel");
            continue;
        }
        if (argument == "--texture-filter" || argument.starts_with("--texture-filter=")) {
            const auto value = argument == "--texture-filter"
                ? (index + 1 < argc ? std::string_view(argv[++index]) : std::string_view{})
                : argument.substr(sizeof("--texture-filter=") - 1);
            if (value == "three-point") result.three_point_filter = true;
            else if (value == "linear") result.three_point_filter = false;
            else return invalid("texture filter must be three-point or linear");
            continue;
        }
        if (argument == "--present-mode" || argument.starts_with("--present-mode=")) {
            const auto value = argument == "--present-mode"
                ? (index + 1 < argc ? std::string_view(argv[++index]) : std::string_view{})
                : argument.substr(sizeof("--present-mode=") - 1);
            if (value == "console") result.present_mode = PresentMode::Console;
            else if (value == "skip-buffering") result.present_mode = PresentMode::SkipBuffering;
            else if (value == "early") result.present_mode = PresentMode::Early;
            else return invalid("present mode must be console, skip-buffering, or early");
            continue;
        }
        if (argument == "--framebuffer-readback" ||
            argument.starts_with("--framebuffer-readback=")) {
            const auto value = argument == "--framebuffer-readback"
                ? (index + 1 < argc ? std::string_view(argv[++index]) : std::string_view{})
                : argument.substr(sizeof("--framebuffer-readback=") - 1);
            if (value == "on") result.framebuffer_readback = true;
            else if (value == "off") result.framebuffer_readback = false;
            else return invalid("framebuffer readback must be on or off");
            continue;
        }
        if (argument.starts_with("--profile=")) {
            result.profile = std::string(argument.substr(sizeof("--profile=") - 1));
            if (!valid_profile_name(*result.profile))
                return invalid("profile names are 1 to 24 printable characters");
            continue;
        }
        if (argument == "--guest") {
            result.guest = true;
            continue;
        }
        if (argument.starts_with("--sphere=")) {
            const auto value = argument.substr(sizeof("--sphere=") - 1);
            if (value == "original") result.sphere_extended = false;
            else if (value == "extended") result.sphere_extended = true;
            else return invalid("sphere must be original or extended");
            continue;
        }
        if (argument == "--casual") {
            result.casual = true;
            continue;
        }
        if (argument == "--credits") {
            result.credits = true;
            continue;
        }
        if (argument == "--unlock-all") {
            result.unlock_all = true;
            continue;
        }
        if (argument == "--start-chapter" || argument.starts_with("--start-chapter=")) {
            const auto value = argument == "--start-chapter"
                ? (index + 1 < argc ? std::string_view(argv[++index]) : std::string_view{})
                : argument.substr(sizeof("--start-chapter=") - 1);
            int chapter = 0;
            for (const char c : value) {
                if (c < '0' || c > '9' || chapter > 1000) return invalid("start chapter must be a number from 1 to 99");
                chapter = chapter * 10 + (c - '0');
            }
            if (chapter < 1 || chapter > 99) return invalid("start chapter must be a number from 1 to 99");
            result.start_chapter = chapter;
            continue;
        }
        if (argument == "--perf-log") {
            result.perf_log = true;
            continue;
        }
        if (argument.starts_with("-")) return invalid("unknown option: " + std::string(argument));
        if (!result.rom_path.empty()) return invalid("provide at most one ROM path");
        result.rom_path = argument;
    }
    if (result.guest && result.profile) return invalid("choose either --guest or --profile");
    return result;
}

const char* run_options_help() {
    return "Usage: tetrisphere [options] [ROM.z64|ROM.zip]\n"
           "Without a cached ROM, check beside the executable for a Tetrisphere ROM, then open the selector.\n"
           "  --fullscreen | --windowed       Override saved window mode for this run\n"
           "  --msaa=off|2x|4x|8x            Multisample antialiasing (also --msaa2x, etc.)\n"
           "  --presentation-filter=nearest|linear|pixel\n"
           "                                   Filter the final presented image\n"
           "  --texture-filter=linear|three-point\n"
           "                                   Filter N64 textures, not only 2D elements\n"
           "  --present-mode=console|skip-buffering|early\n"
           "                                   When finished frames are shown (default console)\n"
           "  --framebuffer-readback=on|off    Copy each frame back to game memory (default on)\n"
           "  --profile=NAME                   Play as this profile (created if new)\n"
           "  --guest                          Play as a guest: nothing is saved, no trophies\n"
           "  --sphere=original|extended       Draw the game's sphere patch, or more of it\n"
           "                                   (overrides the saved SPHERE setting)\n"
           "  --casual                         No game over: hearts refill, Time Trial clock\n"
           "                                   stops at 30 s. Trophies are paused\n"
           "  --start-chapter=N                Start Rescue at chapter N (testing). Trophies are paused\n"
           "  --unlock-all                     Every Story episode and stage open in the menus (testing)\n"
           "  --perf-log                       Report frame timings every 5 seconds\n"
           "  --help                           Show this help\n"
           "Examples: tetrisphere --fullscreen --msaa4x\n"
           "          tetrisphere --windowed --presentation-filter=pixel game.zip\n"
           "Options for this run do not overwrite saved preferences.\n";
}

} // namespace tetrisphere
