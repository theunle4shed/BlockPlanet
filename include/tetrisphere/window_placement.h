#pragma once

// Where the game's window was, so it opens there again (the owner's request,
// 2026-11-12). Kept in its own small file beside the settings
// (window.txt: "x y width height maximized"), written while the window is
// windowed. At launch it is used only when it still fits a connected screen.

#include <optional>
#include <string>
#include <vector>

namespace tetrisphere {

struct WindowPlacement {
    int x = 0, y = 0, width = 0, height = 0;
    bool maximized = false;
};

struct ScreenArea {
    int x = 0, y = 0, width = 0, height = 0;
};

std::string format_window_placement(const WindowPlacement& placement);
std::optional<WindowPlacement> parse_window_placement(const std::string& text);

// The placement to open with, or none (centred as before): its size is at
// least 320 x 240, and enough of its title bar (the top 32 pixels, 96
// across) lies on one of the screens to grab it. A window bigger than that
// screen is shrunk to fit it.
std::optional<WindowPlacement> usable_window_placement(const WindowPlacement& placement,
                                                       const std::vector<ScreenArea>& screens);

} // namespace tetrisphere
