// Tests for the Escape view prototype (src/graphics/escape_view.cpp) on a
// made-up patch: a 16 x 16 mesh on a sphere and a board with layer 1 filled
// except for a few holes dug to the core.

#include "tetrisphere/escape_view.h"
#include "tetrisphere/guest_memory.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace tetrisphere;
using namespace tetrisphere::guest;

// The made-up game state, shared with the preview tool.
void make_state(std::vector<std::uint8_t>& ram) {
    ram.assign(0x800000, 0);
    std::uint8_t* m = ram.data();
    const std::uint32_t mesh = 0x80127000u, values = 0x80130000u, context = 0x80110220u;
    store32(m, 0x800E44E4u, mesh);
    store32(m, 0x800E44D0u, values);
    store32(m, 0x8013DD00u, context);
    store32(m, context + 0x2B20u, 3);
    store32(m, context + 0x2B24u, 5);
    // Points about 10.5 degrees apart either way round the patch's middle, facing +z.
    for (int row = 0; row < 16; ++row)
        for (int column = 0; column < 16; ++column) {
            const double a = (column - 7.5) * 0.184, b = (7.5 - row) * 0.184;
            const double d[3] = {std::sin(a) * std::cos(b), std::sin(b), std::cos(a) * std::cos(b)};
            const std::uint32_t e = mesh + std::uint32_t(row) * 0xC0u + std::uint32_t(column) * 12u;
            for (int k = 0; k < 3; ++k) {
                store16(m, e + 2u * std::uint32_t(k), std::int16_t(std::lround(d[k] * 16384.0 / 3.0)));
                store16(m, e + 6u + 2u * std::uint32_t(k), std::int16_t(std::lround(d[k] * 16384.0 / 10.0)));
            }
        }
    // Layer 1: blue and yellow bricks, with a hole dug to the core every so often.
    for (std::uint32_t cell = 0; cell < 1024; ++cell) {
        store16(m, values + 2u * cell, 81);  // the core
        const bool hole = cell % 37 == 0 || cell % 41 == 0;
        store16(m, values + 2u * (1024u + cell), std::int16_t(hole ? 0 : ((cell / 3) % 2 ? 32 : 33) * 9));
    }
}

#ifndef ESCAPE_PREVIEW
int main() {
    std::vector<std::uint8_t> ram;
    make_state(ram);
    const auto quads = escape_view::cells(ram.data());
    assert(quads.size() == 15 * 15);
    int holes = 0;
    for (const auto& q : quads) {
        holes += q.hole;
        // On the underside of layer 1: 7/30 of the mesh's unit from the centre.
        for (const auto& c : q.corner) {
            const double r = std::sqrt(double(c[0]) * c[0] + double(c[1]) * c[1] + double(c[2]) * c[2]);
            assert(std::fabs(r - 16384.0 * 7.0 / 30.0) < 3.0);
        }
    }
    assert(holes > 0 && holes < 40);
    // Blue (type 33) and yellow (32) as the game's colours.
    bool blue = false, yellow = false;
    for (const auto& q : quads)
        if (!q.hole) blue = blue || (q.colour[2] > q.colour[0]), yellow = yellow || (q.colour[0] > q.colour[2]);
    assert(blue && yellow);
    // The display list fits, ends, and loads the game's projection again.
    const std::uint32_t list = escape_view::build(ram.data(), 0x80123450u, 100.0f);
    assert(list != 0);
    int loads = 0, triangles = 0;
    std::uint32_t last_projection = 0;
    bool ended = false;
    for (std::uint32_t a = list; a < escape_view::memory_end; a += 8) {
        const std::uint32_t w0 = load32(ram.data(), a), w1 = load32(ram.data(), a + 4);
        if ((w0 >> 24) == 0xB8u) { ended = true; break; }
        if ((w0 >> 24) == 0x04u) {
            ++loads;
            const std::uint32_t n = ((w0 >> 20) & 0xFu) + 1u;
            assert(w1 >= escape_view::memory && w1 + n * 16u <= escape_view::memory_end);
        }
        if ((w0 >> 24) == 0xBFu) ++triangles;
        if ((w0 >> 24) == 0x01u && (w0 >> 16 & 1u)) last_projection = w1;
    }
    const int drawn = 15 * 15 - holes;
    assert(ended && last_projection == 0x80123450u && triangles == drawn * 10 && loads == (drawn + 1) / 2);
    std::puts("escape_view ok");
    return 0;
}
#endif
