// Escape mode, step 1: the inside camera. See include/tetrisphere/escape_view.h.

#include "tetrisphere/escape_view.h"

#include "tetrisphere/guest_memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>

namespace tetrisphere::escape_view {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t display_list_pointer = 0x800F22B4u;  // the game's next free command
constexpr std::uint32_t mesh_pointer = 0x800E44E4u;          // 16 x 16 points, 12 bytes each (base, layer step)
constexpr std::uint32_t mesh_row_bytes = 0xC0u;
constexpr std::uint32_t projection_matrix = memory, view_matrix = memory + 0x40u;

using Vec = std::array<float, 3>;
Vec sub(Vec a, Vec b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(Vec a, Vec b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float dot(Vec a, Vec b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec unit(Vec a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? Vec{a[0] / l, a[1] / l, a[2] / l} : Vec{0, 0, 1};
}

Vec base_point(const std::uint8_t* rdram, std::uint32_t mesh, int row, int column) {
    const std::uint32_t e = mesh + std::uint32_t(row) * mesh_row_bytes + std::uint32_t(column) * 12u;
    return {float(load16(rdram, e)), float(load16(rdram, e + 2)), float(load16(rdram, e + 4))};
}

void put_matrix(std::uint8_t* rdram, std::uint32_t address, const float (&m)[4][4]) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const auto fixed = static_cast<std::int32_t>(std::lround(m[i][j] * 65536.0f));
            store16(rdram, address + 2u * std::uint32_t(4 * i + j), static_cast<std::int16_t>((fixed >> 16) & 0xFFFF));
            store16(rdram, address + 32u + 2u * std::uint32_t(4 * i + j), static_cast<std::int16_t>(fixed & 0xFFFF));
        }
}

void append(std::uint8_t* rdram, std::uint32_t w0, std::uint32_t w1) {
    const std::uint32_t at = load32(rdram, display_list_pointer);
    store32(rdram, at, w0);
    store32(rdram, at + 4u, w1);
    store32(rdram, display_list_pointer, at + 8u);
}

std::uint32_t saved_projection = 0;

} // namespace

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_ESCAPE_VIEW");
        return v != nullptr && std::string(v) != "0";
    }();
    return on;
}

bool write_camera(std::uint8_t* rdram, float fov_degrees, float distance) {
    const std::uint32_t mesh = load32(rdram, mesh_pointer);
    if (mesh < 0x80000000u || mesh >= 0x80800000u) return false;
    // From the centre (the mesh's origin) towards the patch's middle, its up
    // along the patch's rows (row 0 at the top).
    const Vec middle = base_point(rdram, mesh, 7, 7), middle2 = base_point(rdram, mesh, 8, 8);
    const Vec forward = unit({(middle[0] + middle2[0]) / 2, (middle[1] + middle2[1]) / 2, (middle[2] + middle2[2]) / 2});
    const float radius = std::max(1.0f, std::sqrt(dot(middle, middle)));
    Vec up = sub(base_point(rdram, mesh, 0, 7), base_point(rdram, mesh, 15, 7));
    const float along = dot(up, forward);
    up = unit({up[0] - forward[0] * along, up[1] - forward[1] * along, up[2] - forward[2] * along});
    const Vec back{-forward[0], -forward[1], -forward[2]};
    const Vec right = unit(cross(up, back));
    // The owner's framing (2026-11-10): "as far back inside the core as it
    // can and facing the brick opposite". The camera is pulled back from
    // the centre towards the far side's inner surface, by `distance` of the
    // radius, still looking across at the patch.
    const float d = std::clamp(distance, 0.0f, 0.98f) * radius;
    const Vec eye{-forward[0] * d, -forward[1] * d, -forward[2] * d};
    // Row vectors: eye space = (v - eye) * R (x right, y up, the camera looking down -z).
    const float v[4][4] = {{right[0], up[0], back[0], 0},
                           {right[1], up[1], back[1], 0},
                           {right[2], up[2], back[2], 0},
                           {-dot(eye, right), -dot(eye, up), -dot(eye, back), 1}};
    const float f = 1.0f / std::tan(std::clamp(fov_degrees, 30.0f, 150.0f) * 3.14159265f / 360.0f), aspect = 4.0f / 3.0f;
    const float near = radius * 0.02f, far = radius * 8.0f;
    const float p[4][4] = {{f / aspect, 0, 0, 0}, {0, f, 0, 0}, {0, 0, (far + near) / (near - far), -1}, {0, 0, 2.0f * far * near / (near - far), 0}};
    put_matrix(rdram, projection_matrix, p);
    put_matrix(rdram, view_matrix, v);
    return true;
}

bool begin(std::uint8_t* rdram) {
    const std::uint32_t dl = load32(rdram, display_list_pointer);
    if (dl < 0x80000000u || dl >= 0x80400000u) return false;
    static const float fov = [] {
        const char* v = std::getenv("BLOCKPLANET_ESCAPE_FOV");
        return v != nullptr ? float(std::atof(v)) : 70.0f;
    }();
    // 0 at the centre, 1 at the far side's inner surface.
    static const float distance = [] {
        const char* v = std::getenv("BLOCKPLANET_ESCAPE_DISTANCE");
        return v != nullptr ? float(std::atof(v)) : 0.85f;
    }();
    if (!write_camera(rdram, fov, distance)) return false;
    // The projection in use, to load again after (the last projection load
    // in this frame's list, looked for a little way back).
    saved_projection = 0;
    for (std::uint32_t at = dl - 8u, n = 0; n < 4096 && at >= 0x80000000u; at -= 8u, ++n) {
        const std::uint32_t w0 = load32(rdram, at);
        if ((w0 >> 24) == 0x01u && (w0 >> 16 & 0x01u) != 0) {
            saved_projection = load32(rdram, at + 4u);
            break;
        }
    }
    append(rdram, 0x01030040u, projection_matrix);  // projection: load
    append(rdram, 0x01060040u, view_matrix);        // modelview: load, push
    return true;
}

void end(std::uint8_t* rdram) {
    append(rdram, 0xBD000000u, 0);  // pop the modelview
    if (saved_projection != 0) append(rdram, 0x01030040u, saved_projection);
}

} // namespace tetrisphere::escape_view
