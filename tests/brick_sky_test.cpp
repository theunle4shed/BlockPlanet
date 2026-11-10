// Tests for the brick sky experiment (src/graphics/brick_sky.cpp): its
// settings, motion (easing, cap, wrap, direction) and display list.

#include "tetrisphere/brick_sky.h"
#include "tetrisphere/guest_memory.h"

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
    assert(brick_sky::values().size() == 7);

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

    // The bricks lie on the sphere, eight vertices each.
    const auto bricks = brick_sky::bricks(s);
    assert(bricks.size() == std::size_t(6 * 8 * 8 * brick_sky::vertices_per_brick));
    for (const auto& v : bricks) {
        const double r = std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
        assert(std::fabs(r - 1000.0) < 2.0);
    }
    // A brick's outer corners (mortar) are darker than its face.
    assert(bricks[0].r + bricks[0].g + bricks[0].b < bricks[4].r + bricks[4].g + bricks[4].b);

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
    assert(vertex_loads == 6 * 8 * 8 / 2 + 2);  // two bricks a load, then the frame and the horizon
    // At 21:9 the comfort frame reaches the picture's edges, past the 4:3 frame's.
    brick_sky::build(rdram.data(), s, m, 0, 0, 21.0f / 9.0f);
    const std::uint32_t comfort = brick_sky::memory + 0x9000u + 16u * std::uint32_t(bricks.size());
    assert(load16(rdram.data(), comfort) == -280 && load16(rdram.data(), comfort + 16) == 280);
    assert(triangles == 6 * 8 * 8 * brick_sky::triangles_per_brick + 8 + 2);
    std::puts("brick_sky ok");
    return 0;
}
