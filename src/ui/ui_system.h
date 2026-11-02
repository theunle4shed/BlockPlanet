#pragma once

// BlockPlanet's menu layer: RmlUi drawn over the game through RT64's draw
// hook (docs/UI.md). The functions below may be called from any thread;
// RmlUi itself runs on RT64's presentation thread.

#include "ui_actions.h"

#include <filesystem>
#include <optional>
#include <string>

namespace blockplanet::ui {

// Registers the menu layer with RT64. Call before RT64 starts. `assets` is
// the folder holding the menu screens and fonts (ui/ beside the game).
// BLOCKPLANET_UI_TEST=1 opens the main menu at start.
void install(const std::filesystem::path& assets);

// Opens the menus at a screen (ui/screens/<screen>.rml), or closes them.
// Applied at the next frame.
void request_open(const std::string& screen);
void request_close();
// Navigation input, applied at the next frame.
void queue_action(Action action);

// True while a menu screen is open (or about to open).
bool menus_open();

// The next command the player chose (an item's command:NAME), if any.
std::optional<std::string> take_command();

} // namespace blockplanet::ui
