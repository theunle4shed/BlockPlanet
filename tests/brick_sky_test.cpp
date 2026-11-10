// Tests for the brick sky experiment (src/graphics/brick_sky.cpp): its
// settings, motion (easing, cap, wrap, direction) and display list.

#include "tetrisphere/brick_sky.h"
#include "tetrisphere/guest_memory.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace tetrisphere;
using namespace tetrisphere::guest;

int main() {
    // Settings.
    assert(brick_sky::set("sky_spin", "wild") && brick_sky::settings().spin == "wild");
    assert(brick_sky::set("sky_spin", "nonsense") && brick_sky::settings().spin == "wild");
    assert(brick_sky::set("sky_fov", "500") && brick_sky::settings().fov_degrees == 130.0f);
    assert(brick_sky::set("sky_brightness", "55") && std::fabs(brick_sky::settings().brightness - 0.55f) < 1e-6f);
    assert(brick_sky::set("sky_on", "on") && brick_sky::settings().on);
    assert(!brick_sky::set("grid_theme", "blue"));
    assert(brick_sky::values().size() == 8);

    // The angle step wraps at 16 bits.
    assert(brick_sky::angle_step(32760, -32766) == 10);
    assert(brick_sky::angle_step(10, 0) == -10);

    // Motion: the first call only takes the angles.
    brick_sky::Settings s;
    s.spin = "normal";
    brick_sky::Motion m;
    m.step(100, 0, 1.0f / 60, s);
    assert(m.yaw == 0 && m.yaw_speed == 0);
    // The sphere turning right (+x) turns the sky the other way, eased: the
    // speed builds over several frames, not at once.
    std::int16_t x = 100;
    m.step(x += 40, 0, 1.0f / 60, s);
    const float first = m.yaw_speed;
    for (int i = 0; i < 60; ++i) m.step(x += 40, 0, 1.0f / 60, s);
    assert(first < 0 && m.yaw_speed < first * 3);
    // 40 a frame at 60 frames a second is 1.84 rad/s; normal is 1.5 times, against.
    const float expected = -1.5f * 40.0f * 2.0f * 3.14159265f / 8192.0f * 60.0f + 0.05f;
    assert(std::fabs(m.yaw_speed - expected) < 0.05f);
    // Wild and a fast spin stay under the cap.
    s.spin = "wild";
    for (int i = 0; i < 120; ++i) m.step(x += 200, 0, 1.0f / 60, s);
    assert(std::hypot(m.yaw_speed, m.pitch_speed) <= brick_sky::max_speed + 1e-4f);
    // With the sphere: the same sign as its turn.
    s.against = false;
    s.spin = "gentle";
    for (int i = 0; i < 240; ++i) m.step(x += 40, 0, 1.0f / 60, s);
    assert(m.yaw_speed > 0);
    // Still: the sky comes to rest when the sphere does.
    s.spin = "still";
    for (int i = 0; i < 240; ++i) m.step(x, 0, 1.0f / 60, s);
    assert(std::fabs(m.yaw_speed) < 1e-3f && std::fabs(m.pitch_speed) < 1e-3f);
    // Drift: a slow turn of its own.
    s.spin = "drift";
    for (int i = 0; i < 240; ++i) m.step(x, 0, 1.0f / 60, s);
    assert(m.yaw_speed > 0.04f && m.yaw_speed < 0.06f);
    // The yaw stays wrapped.
    assert(m.yaw >= -3.1416f && m.yaw <= 3.1416f);

    // Both layouts lie on the sphere, eight vertices a cell.
    for (const char* layout : {"cube", "sphere"}) {
        s.layout = layout;
        const auto sky = brick_sky::mesh(s);
        assert(sky.vertices.size() == sky.cells() * brick_sky::vertices_per_cell);
        for (const auto& v : sky.vertices) {
            const double r = std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
            assert(std::fabs(r - 1000.0) < 2.0);
        }
        // A brick's outer corners (mortar) are darker than its face.
        const auto& v = sky.vertices;
        assert(v[0].r + v[0].g + v[0].b < v[4].r + v[4].g + v[4].b);
    }
    assert(brick_sky::cube_mesh(s).cells() == 6 * 8 * 8);
    // The sphere: 32 x 14 cells, as the board's 11.25 degrees, and 32 cap cells at each pole.
    const auto sphere = brick_sky::sphere_mesh(s);
    assert(sphere.cells() == std::size_t(32 * 14 + 64));
    {
        // Its bricks are one to four cells, connected, and each cell's
        // bevels are exactly its sides against another brick.
        const auto brick = brick_sky::sphere_bricks();
        const int bricks = *std::max_element(brick.begin(), brick.end()) + 1;
        std::vector<int> size(std::size_t(bricks), 0);
        for (const int b : brick) ++size[std::size_t(b)];
        int multi = 0;
        for (const int n : size) {
            assert(n >= 1 && n <= 4);
            multi += n > 1;
        }
        assert(multi > bricks / 2);  // mostly larger bricks, as on the board
        const auto id = [&](int r, int c) { return r < 0 || r >= 14 ? -1 : brick[std::size_t(r * 32 + (c + 32) % 32)]; };
        for (int r = 0; r < 14; ++r)
            for (int c = 0; c < 32; ++c) {
                const int me = id(r, c);
                const std::uint8_t expect = std::uint8_t((id(r - 1, c) != me) | (id(r, c + 1) != me) << 1 | (id(r + 1, c) != me) << 2 |
                                                         (id(r, c - 1) != me) << 3);
                assert(sphere.bevels[std::size_t(r * 32 + c)] == expect);
            }
        // The middle row is on the equator's edge: latitude 0 at row 7's bottom.
        assert(std::abs(sphere.vertices[std::size_t((7 * 32) * 8)].y) <= 1);
    }
    s.layout = "cube";

    // The display list fits, ends with G_ENDDL, and loads the game's
    // projection again at its end.
    std::vector<std::uint8_t> rdram(0x800000);
    s.comfort = "both";
    const std::uint32_t list = brick_sky::build(rdram.data(), s, m, 0x80123450u, 0);
    assert(list != 0);
    int vertex_loads = 0, triangles = 0;
    std::uint32_t last_projection = 0;
    bool ended = false;
    for (std::uint32_t a = list; a < brick_sky::memory_end; a += 8) {
        const std::uint32_t w0 = load32(rdram.data(), a), w1 = load32(rdram.data(), a + 4);
        const std::uint32_t op = w0 >> 24;
        if (op == 0xB8u) { ended = true; break; }
        if (op == 0x04u) {
            ++vertex_loads;
            const std::uint32_t n = ((w0 >> 20) & 0xFu) + 1u;
            assert((w0 & 0xFFFFu) == n * 16u && w1 >= brick_sky::memory && w1 + n * 16u <= brick_sky::memory_end);
        }
        if (op == 0xBFu) {
            ++triangles;
            assert(((w1 >> 16) & 0xFFu) < 160 && ((w1 >> 8) & 0xFFu) < 160 && (w1 & 0xFFu) < 160);
        }
        if (op == 0x01u && (w0 >> 16 & 1u)) last_projection = w1;
    }
    assert(ended && last_projection == 0x80123450u);
    assert(vertex_loads == 6 * 8 * 8 / 2 + 2);  // two cells a load, then the frame and the horizon
    // At 21:9 the comfort frame reaches the picture's edges, past the 4:3 frame's.
    brick_sky::build(rdram.data(), s, m, 0, 0, 21.0f / 9.0f);
    const std::uint32_t comfort = brick_sky::memory + 0xB000u + 16u * std::uint32_t(brick_sky::cube_mesh(s).vertices.size());
    assert(load16(rdram.data(), comfort) == -280 && load16(rdram.data(), comfort + 16) == 280);
    assert(triangles == int(brick_sky::cube_mesh(s).triangles()) + 8 + 2);
    // The sphere layout fits too.
    s.layout = "sphere";
    assert(brick_sky::build(rdram.data(), s, m, 0, 0) != 0);
    s.layout = "cube";
    // Stage contrast: the types the stage does not use, read from its records.
    {
        std::vector<std::uint8_t> ram(0x800000);
        store16(ram.data(), 0x8013D186u, 2);
        store32(ram.data(), 0x8013D188u, 0x80127CF0u);
        store32(ram.data(), 0x8013D18Cu, 0x80128458u);
        store16(ram.data(), 0x80127CF2u, 0x21);
        store16(ram.data(), 0x8012845Au, 0x20);
        const auto used = brick_sky::stage_brick_types(ram.data());
        assert((used == std::vector<int>{0x21, 0x20}));
        const auto contrast = brick_sky::stage_contrast(used);
        assert(contrast.size() == 4 && contrast[0] == brick_sky::type_colour(0x22));
        assert(brick_sky::stage_contrast({0x20, 0x21, 0x22, 0x23, 0x24, 0x25}).size() == 3);  // all used: the fixed set
        brick_sky::Settings st;
        st.colours = "stage";
        st.stage_types = used;
        assert(brick_sky::mesh(st).cells() > 0);
        assert(brick_sky::set("sky_colours", "stage") && brick_sky::settings().colours == "stage");
    }
    std::puts("brick_sky ok");
    return 0;
}
