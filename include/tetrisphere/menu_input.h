#pragma once

// Turns SDL input into navigation for BlockPlanet's menus (docs/UI.md).

#include "tetrisphere/linux_platform.h"

#include <SDL.h>

namespace tetrisphere {

// Handles one SDL event for the menus. F1 opens or closes them; while they
// are open, navigation keys and buttons are turned into menu actions.
// `assigned` is true for events from a controller assigned to a port.
// Returns true if the event was used by the menus.
bool handle_menu_event(const SDL_Event& event, bool assigned, const ControllerBindings& bindings);

} // namespace tetrisphere
