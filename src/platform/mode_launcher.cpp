#include "tetrisphere/mode_launcher.h"

#include "tetrisphere/guest_memory.h"

namespace tetrisphere {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t main_row = 0x800E121Cu;
constexpr std::uint32_t player_page_row = 0x800E1222u;
constexpr std::uint32_t mode_row = 0x800E1224u;
constexpr std::uint32_t submenu_row = 0x800E1220u;  // Options and similar pages
constexpr std::int16_t single = 0;
constexpr std::int16_t training = 4;
constexpr std::int16_t basics = 0;
constexpr std::int16_t start = 0;
constexpr std::uint16_t button_a = 0x8000;

// A press is held for a few frames, then released before the next one, so
// the game sees each as a new press.
constexpr auto press_time = std::chrono::milliseconds(100);
constexpr auto release_time = std::chrono::milliseconds(250);
constexpr auto timeout = std::chrono::seconds(15);

std::int16_t row_for(LaunchMode mode) {
    switch (mode) {
    case LaunchMode::Rescue: return 0;
    case LaunchMode::HideAndSeek: return 1;
    case LaunchMode::Puzzle: return 2;
    case LaunchMode::TimeTrial: return 3;
    case LaunchMode::VsCpu: return 4;
    case LaunchMode::Lines: return 5;
    case LaunchMode::TrainingBasics: return 0;
    }
    return 0;
}

} // namespace

std::optional<LaunchMode> launch_mode_for_command(const std::string& command) {
    if (command == "story") return LaunchMode::Rescue;
    if (command == "hide_and_seek") return LaunchMode::HideAndSeek;
    if (command == "puzzle") return LaunchMode::Puzzle;
    if (command == "time_trial") return LaunchMode::TimeTrial;
    if (command == "versus") return LaunchMode::VsCpu;
    if (command == "lines") return LaunchMode::Lines;
    if (command == "training") return LaunchMode::TrainingBasics;
    return std::nullopt;
}

const char* launch_mode_name(LaunchMode mode) {
    switch (mode) {
    case LaunchMode::Rescue: return "rescue";
    case LaunchMode::HideAndSeek: return "hide_and_seek";
    case LaunchMode::Puzzle: return "puzzle";
    case LaunchMode::TimeTrial: return "time_trial";
    case LaunchMode::VsCpu: return "vs_cpu";
    case LaunchMode::Lines: return "lines";
    case LaunchMode::TrainingBasics: return "training_basics";
    }
    return "";
}

bool ModeLauncher::request(LaunchMode mode, const std::uint8_t* rdram, Clock::time_point now) {
    if (load16(rdram, main_row) < 0) return false;     // in play
    if (load16(rdram, submenu_row) >= 0) return false;  // inside another page
    mode_ = mode;
    started_ = now;
    phase_start_ = now;
    pressing_ = false;
    return true;
}

ModeLauncher::Result ModeLauncher::update(std::uint8_t* rdram, Clock::time_point now, std::uint16_t& buttons) {
    buttons = 0;
    if (!mode_) return Result::Idle;
    if (load16(rdram, main_row) < 0) {  // play has begun
        mode_.reset();
        return Result::Started;
    }
    if (now - started_ > timeout) {
        mode_.reset();
        return Result::TimedOut;
    }
    // Put the original menus' cursor where the next press should act, then
    // press: on the main menu (or title) SINGLE, on the player page START,
    // in the mode list the mode's row.
    if (*mode_ == LaunchMode::TrainingBasics) {
        // Main menu TRAINING, then BASICS on the Training page.
        if (load16(rdram, submenu_row) < 0) store16(rdram, main_row, training);
        else store16(rdram, submenu_row, basics);
    } else if (load16(rdram, player_page_row) < 0) {
        store16(rdram, main_row, single);
    } else if (load16(rdram, mode_row) < 0) {
        store16(rdram, player_page_row, start);
    } else {
        store16(rdram, mode_row, row_for(*mode_));
    }
    const auto elapsed = now - phase_start_;
    if (pressing_ && elapsed >= press_time) {
        pressing_ = false;
        phase_start_ = now;
    } else if (!pressing_ && elapsed >= release_time) {
        pressing_ = true;
        phase_start_ = now;
    }
    buttons = pressing_ ? button_a : 0;
    return Result::Working;
}

} // namespace tetrisphere
