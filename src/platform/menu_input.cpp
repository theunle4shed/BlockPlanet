#include "tetrisphere/menu_input.h"

#include "ui_system.h"

#include <atomic>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>

namespace tetrisphere {
namespace {

using blockplanet::ui::Action;

SDL_GameControllerButton sdl_button(PhysicalButton button) {
    switch (button) {
    case PhysicalButton::South: return SDL_CONTROLLER_BUTTON_A;
    case PhysicalButton::East: return SDL_CONTROLLER_BUTTON_B;
    case PhysicalButton::West: return SDL_CONTROLLER_BUTTON_X;
    case PhysicalButton::North: return SDL_CONTROLLER_BUTTON_Y;
    }
    return SDL_CONTROLLER_BUTTON_INVALID;
}

std::atomic<bool> in_play{false};

// Left stick direction per controller and axis, so a push counts once.
std::map<std::pair<SDL_JoystickID, int>, int> stick;

} // namespace

bool handle_menu_event(const SDL_Event& event, bool assigned, const ControllerBindings& bindings) {
    // F5: restart the game from the title (for testing, when stuck).
    if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F5) {
        blockplanet::ui::send_command("reset");
        return true;
    }
    if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F1) {
        if (blockplanet::ui::menus_open()) blockplanet::ui::request_close();
        else if (in_play.load()) blockplanet::ui::send_command("pause");
        else blockplanet::ui::request_open("main");
        return true;
    }
    if (!blockplanet::ui::menus_open()) return false;
    // Follow the device in use: screens show controller-only parts (the
    // on-screen keyboard) only while a controller is used.
    static bool pad = false;
    const bool from_pad = assigned && (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERAXISMOTION);
    const bool from_keys = event.type == SDL_KEYDOWN || event.type == SDL_TEXTINPUT;
    if ((from_pad && !pad && (event.type == SDL_CONTROLLERBUTTONDOWN ||
                              std::abs(event.caxis.value) > 16000)) ||
        (from_keys && pad)) {
        pad = !pad;
        blockplanet::ui::set_pad(pad);
    }
    // The Alt button's badge in the menus' hints, following the device in use.
    {
        static std::string shown;
        const auto family = active_controller_family();
        const char f = family == ControllerFamily::Xbox ? 'x' : family == ControllerFamily::PlayStation ? 'p' : 'n';
        const std::string spec = !pad ? std::string("[k:T]")
                                      : std::string("[") + f + ":" + physical_button_label(family, bindings.face_left) + "]";
        if (spec != shown) blockplanet::ui::set_alt_button(shown = spec);
    }
    auto send = [](Action action) {
        blockplanet::ui::queue_action(action);
        return true;
    };
    // The name screen: typed text goes into the name; Backspace deletes and
    // Enter finishes instead of their usual meanings.
    const bool naming = blockplanet::ui::current_screen() == "name";
    if (naming && event.type == SDL_TEXTINPUT) {
        blockplanet::ui::send_command(std::string("name_type:") + event.text.text);
        return true;
    }
    if (naming && event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.scancode) {
        case SDL_SCANCODE_BACKSPACE:
            blockplanet::ui::send_command("name_key:delete");
            return true;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (!event.key.repeat) blockplanet::ui::send_command("name_done");
            return true;
        case SDL_SCANCODE_SPACE:
            return true;  // arrives as text
        default: break;
        }
    }
    if (naming && event.type == SDL_CONTROLLERBUTTONDOWN && assigned &&
        event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
        blockplanet::ui::send_command("name_done");
        return true;
    }
    if (event.type == SDL_TEXTINPUT) return true;
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.scancode) {
        case SDL_SCANCODE_UP: return send(Action::Up);
        case SDL_SCANCODE_DOWN: return send(Action::Down);
        case SDL_SCANCODE_LEFT: return send(Action::Left);
        case SDL_SCANCODE_RIGHT: return send(Action::Right);
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            return event.key.repeat ? true : send(Action::Confirm);
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE:
            return event.key.repeat ? true : send(Action::Back);
        case SDL_SCANCODE_T:
            return event.key.repeat ? true : send(Action::Alt);
        default: return true;  // other keys do nothing while the menus are open
        }
    }
    if (event.type == SDL_KEYUP) return true;
    if (!assigned) return false;
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        const auto button = static_cast<SDL_GameControllerButton>(event.cbutton.button);
        if (button == sdl_button(bindings.confirm)) return send(Action::Confirm);
        if (button == sdl_button(bindings.cancel)) return send(Action::Back);
        if (button == sdl_button(bindings.face_left)) return send(Action::Alt);
        switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return send(Action::Up);
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return send(Action::Down);
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return send(Action::Left);
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return send(Action::Right);
        case SDL_CONTROLLER_BUTTON_START: return send(Action::Confirm);
        case SDL_CONTROLLER_BUTTON_BACK: return send(Action::Back);
        default: return true;
        }
    }
    if (event.type == SDL_CONTROLLERBUTTONUP) return true;
    if (event.type == SDL_CONTROLLERAXISMOTION &&
        (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)) {
        const int direction = event.caxis.value > 16000 ? 1 : event.caxis.value < -16000 ? -1 : 0;
        int& previous = stick[{event.caxis.which, event.caxis.axis}];
        const bool pushed = direction != 0 && direction != previous;
        previous = direction;
        if (pushed) {
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) send(direction > 0 ? Action::Right : Action::Left);
            else send(direction > 0 ? Action::Down : Action::Up);
        }
        return true;
    }
    return false;
}

void set_menu_play_state(bool playing) { in_play.store(playing); }

} // namespace tetrisphere
