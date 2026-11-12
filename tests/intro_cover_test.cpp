#include "tetrisphere/intro_cover.h"

#include <cassert>

using tetrisphere::expand_aspect_target;

int main() {
    constexpr float source = 4.0f / 3.0f, wide = 16.0f / 9.0f;
    // Outside the intro: the window's shape, never narrower than the game's.
    static_assert(expand_aspect_target(3.5f, source, false, wide) == 3.5f);
    static_assert(expand_aspect_target(0.5f, source, false, wide) == source);
    // The intro: tall windows zoom to cover; wide ones stop at the limit.
    static_assert(expand_aspect_target(0.5f, source, true, wide) == 0.5f);
    static_assert(expand_aspect_target(1.5f, source, true, wide) == 1.5f);
    static_assert(expand_aspect_target(3.5f, source, true, wide) == wide);
    // No limit, or a limit below the game's own shape.
    static_assert(expand_aspect_target(3.5f, source, true, 0.0f) == 3.5f);
    static_assert(expand_aspect_target(3.5f, source, true, 1.0f) == source);
    assert(tetrisphere::intro_max_aspect() > 0.0f);
    return 0;
}
