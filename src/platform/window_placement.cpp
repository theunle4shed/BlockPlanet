#include "tetrisphere/window_placement.h"

#include <algorithm>
#include <sstream>

namespace tetrisphere {

std::string format_window_placement(const WindowPlacement& p) {
    std::ostringstream out;
    out << p.x << ' ' << p.y << ' ' << p.width << ' ' << p.height << ' ' << (p.maximized ? 1 : 0) << '\n';
    return out.str();
}

std::optional<WindowPlacement> parse_window_placement(const std::string& text) {
    std::istringstream in(text);
    WindowPlacement p;
    int maximized = 0;
    if (!(in >> p.x >> p.y >> p.width >> p.height >> maximized)) return std::nullopt;
    p.maximized = maximized != 0;
    return p;
}

std::optional<WindowPlacement> usable_window_placement(const WindowPlacement& placement,
                                                       const std::vector<ScreenArea>& screens) {
    constexpr int smallest_width = 320, smallest_height = 240, grab_width = 96, grab_height = 32;
    if (placement.width < smallest_width || placement.height < smallest_height) return std::nullopt;
    for (const auto& s : screens) {
        // The overlap of the title bar strip with this screen.
        const int left = std::max(placement.x, s.x), right = std::min(placement.x + placement.width, s.x + s.width);
        const int top = std::max(placement.y, s.y), bottom = std::min(placement.y + grab_height, s.y + s.height);
        if (right - left < grab_width || bottom - top < grab_height) continue;
        WindowPlacement fitted = placement;
        fitted.width = std::min(fitted.width, s.width);
        fitted.height = std::min(fitted.height, s.height);
        return fitted;
    }
    return std::nullopt;
}

} // namespace tetrisphere
