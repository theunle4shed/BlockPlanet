#include "tetrisphere/pause_bridge.h"

#include "tetrisphere/guest_memory.h"

namespace tetrisphere {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t scene = 0x800E121Cu;
constexpr std::uint32_t pause_row = 0x800E1228u;
constexpr std::uint32_t mode = 0x800E44A8u;
constexpr std::int16_t training = 3;
constexpr std::uint16_t button_a = 0x8000;
constexpr std::uint16_t button_start = 0x1000;
constexpr auto press_time = std::chrono::milliseconds(100);
constexpr auto release_time = std::chrono::milliseconds(250);
constexpr auto timeout = std::chrono::seconds(3);

// Pressed for press_time out of every press_time + release_time.
bool pressing(PauseBridge::Clock::duration elapsed) {
    return elapsed % (press_time + release_time) >= release_time;
}

} // namespace

std::vector<blockplanet::ui::ListItem> pause_items(bool training_mode) {
    std::vector<blockplanet::ui::ListItem> items{{"resume", "Resume", "", "command:pause_resume", true, 1, true}};
    if (training_mode) items.push_back({"skip", "Skip lesson", "", "command:pause_skip", true});
    items.push_back({"options", "Options", "", "open:options", true});
    items.push_back({"quit", "Quit to menu", "", "command:pause_quit", true});
    return items;
}

void PauseBridge::choose(Choice choice, Clock::time_point now) {
    choice_ = choice;
    since_ = now;
}

void PauseBridge::request_pause(Clock::time_point now) {
    pause_requested_ = true;
    since_ = now;
}

PauseBridge::Output PauseBridge::update(std::uint8_t* rdram, bool menus_open, Clock::time_point now) {
    Output out;
    const bool playing = load16(rdram, scene) == -1;
    const std::int16_t row = load16(rdram, pause_row);
    const bool paused = playing && row >= 0;

    if (pause_requested_) {
        // F1 during play: press Start until the game pauses.
        if (paused || !playing || now - since_ > timeout) {
            pause_requested_ = false;
        } else {
            out.inject = true;
            out.buttons = pressing(now - since_) ? button_start : 0;
            return out;
        }
    }
    if (!paused) {
        // Resumed, or left play: nothing more to do.
        if (showing_) out.close_menu = menus_open;
        showing_ = false;
        choice_.reset();
        return out;
    }
    if (!showing_ && !choice_) {
        showing_ = true;
        out.open_menu = true;
        out.items = pause_items(load16(rdram, mode) == training);
        return out;
    }
    // Closing the menus (Back) means Resume.
    if (showing_ && !choice_ && !menus_open) choose(Choice::Resume, now);
    if (choice_) {
        if (now - since_ > timeout) {  // give up; show the menu again
            choice_.reset();
            showing_ = false;
            return out;
        }
        if (*choice_ != Choice::Resume || menus_open) out.close_menu = menus_open;
        store16(rdram, pause_row, static_cast<std::int16_t>(*choice_));
        out.inject = true;
        out.buttons = pressing(now - since_) ? button_a : 0;
    }
    return out;
}

} // namespace tetrisphere
