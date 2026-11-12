#include "tetrisphere/window_placement.h"

#include <cassert>

using namespace tetrisphere;

int main() {
    // Written and read back.
    const WindowPlacement p{120, 80, 1280, 720, true};
    const auto back = parse_window_placement(format_window_placement(p));
    assert(back && back->x == 120 && back->y == 80 && back->width == 1280 && back->height == 720 && back->maximized);
    assert(!parse_window_placement("") && !parse_window_placement("1 2 3"));

    const std::vector<ScreenArea> one{{0, 0, 1920, 1040}};
    // On the screen: kept.
    assert(usable_window_placement(p, one).has_value());
    // Off every screen (a monitor since unplugged): centred instead.
    assert(!usable_window_placement({2500, 100, 800, 600, false}, one));
    // Title bar above the screen: not grabbable.
    assert(!usable_window_placement({100, -200, 800, 600, false}, one));
    // Mostly off the right edge but the title bar still reachable: kept.
    assert(usable_window_placement({1800, 100, 800, 600, false}, one));
    // Too small: ignored.
    assert(!usable_window_placement({100, 100, 100, 100, false}, one));
    // Bigger than the screen: shrunk.
    const auto big = usable_window_placement({0, 0, 4000, 3000, false}, one);
    assert(big && big->width == 1920 && big->height == 1040);
    // A second screen to the left (negative x).
    const std::vector<ScreenArea> two{{0, 0, 1920, 1040}, {-1280, 0, 1280, 1000}};
    assert(usable_window_placement({-1200, 50, 1000, 700, false}, two).has_value());
    return 0;
}
