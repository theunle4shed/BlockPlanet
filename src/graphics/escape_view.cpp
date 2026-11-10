// Escape mode, step 1: the view from inside the play sphere (a research
// prototype). See include/tetrisphere/escape_view.h.

#include "tetrisphere/escape_view.h"

#include "tetrisphere/brick_sky.h"
#include "tetrisphere/guest_memory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace tetrisphere::escape_view {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t display_list_pointer = 0x800F22B4u;  // the game's next free command
constexpr std::uint32_t mesh_pointer = 0x800E44E4u;          // 16 x 16 points, 12 bytes each (base, layer step)
constexpr std::uint32_t mesh_row_bytes = 0xC0u;
constexpr std::uint32_t active_context = 0x8013DD00u;
constexpr std::uint32_t board_column_offset = 0x2B20u;  // context word, added to the mesh row
constexpr std::uint32_t board_row_offset = 0x2B24u;     // context word, added to the mesh column
constexpr std::uint32_t block_values = 0x800E44D0u;     // pointer: 9 layers of 32 x 32 halfwords
constexpr int points = 16, patch = 15;

constexpr std::uint32_t projection_matrix = memory, view_matrix = memory + 0x40u;
constexpr std::uint32_t commands_start = memory + 0x100u, commands_end = memory + 0x6000u;
constexpr std::uint32_t vertices_start = commands_end;
constexpr float inset = 0.12f;

using Vec = std::array<float, 3>;
Vec sub(Vec a, Vec b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(Vec a, Vec b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float dot(Vec a, Vec b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec unit(Vec a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? Vec{a[0] / l, a[1] / l, a[2] / l} : Vec{0, 0, 1};
}

// A mesh point's position at the underside of layer 1 (layer n lies between
// base + (n - 2) step and base + (n - 1) step).
Vec underside(const std::uint8_t* rdram, std::uint32_t mesh, int row, int column) {
    const std::uint32_t e = mesh + std::uint32_t(row) * mesh_row_bytes + std::uint32_t(column) * 12u;
    Vec p{};
    for (int axis = 0; axis < 3; ++axis)
        p[std::size_t(axis)] = float(load16(rdram, e + 2u * std::uint32_t(axis))) - float(load16(rdram, e + 6u + 2u * std::uint32_t(axis)));
    return p;
}

std::array<std::uint8_t, 3> colour_of(int type) {
    std::array<float, 3> c{0.5f, 0.5f, 0.52f};  // unknown kinds: grey
    if (type >= 32 && type <= 37) c = brick_sky::type_colour(type);
    else if (type == 47) c = {0.45f, 0.22f, 0.62f};  // Puzzle's border
    else if (type == 7) c = {0.92f, 0.92f, 0.88f};   // a special block
    std::array<std::uint8_t, 3> out{};
    for (int k = 0; k < 3; ++k) out[std::size_t(k)] = std::uint8_t(std::lround(std::clamp(c[std::size_t(k)] * 0.8f, 0.0f, 1.0f) * 255));
    return out;
}

void put_matrix(std::uint8_t* rdram, std::uint32_t address, const float (&m)[4][4]) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const auto fixed = static_cast<std::int32_t>(std::lround(m[i][j] * 65536.0f));
            store16(rdram, address + 2u * std::uint32_t(4 * i + j), static_cast<std::int16_t>((fixed >> 16) & 0xFFFF));
            store16(rdram, address + 32u + 2u * std::uint32_t(4 * i + j), static_cast<std::int16_t>(fixed & 0xFFFF));
        }
}

} // namespace

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_ESCAPE_VIEW");
        return v != nullptr && std::string(v) != "0";
    }();
    return on;
}

std::vector<Quad> cells(const std::uint8_t* rdram) {
    std::vector<Quad> out;
    const std::uint32_t mesh = load32(rdram, mesh_pointer);
    const std::uint32_t values = load32(rdram, block_values);
    const std::uint32_t context = load32(rdram, active_context);
    if (mesh < 0x80000000u || values < 0x80000000u || context < 0x80000000u) return out;
    const std::uint32_t column_offset = load32(rdram, context + board_column_offset);
    const std::uint32_t row_offset = load32(rdram, context + board_row_offset);
    for (int row = 0; row < patch; ++row)
        for (int column = 0; column < patch; ++column) {
            // The board cell under this patch cell, as the game's cell setup finds it.
            const std::uint32_t x = (std::uint32_t(row) + column_offset) & 0x1Fu;
            const std::uint32_t y = (std::uint32_t(column) + row_offset) & 0x1Fu;
            const std::uint32_t cell = (y << 5) + x;
            const int value = load16(rdram, values + 2u * (1024u + cell));  // layer 1, just above the core
            const int type = value / 9;
            Quad q{};
            q.hole = value <= 0 || type == 10;  // dug to the core (not drawn): a hole
            q.colour = colour_of(type);
            const std::array<std::array<int, 2>, 4> at{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            for (std::size_t k = 0; k < 4; ++k) {
                const Vec p = underside(rdram, mesh, row + at[k][0], column + at[k][1]);
                for (int axis = 0; axis < 3; ++axis)
                    q.corner[k][std::size_t(axis)] = static_cast<std::int16_t>(std::lround(std::clamp(p[std::size_t(axis)], -32768.0f, 32767.0f)));
            }
            out.push_back(q);
        }
    return out;
}

std::uint32_t build(std::uint8_t* rdram, std::uint32_t projection, float fov_degrees) {
    const std::uint32_t mesh = load32(rdram, mesh_pointer);
    if (mesh < 0x80000000u) return 0;
    const auto quads = cells(rdram);
    if (quads.empty()) return 0;
    // The camera at the centre, looking out at the patch's middle, its up
    // along the patch's rows.
    const float mid = float(points - 1) / 2.0f;
    const auto at = [&](float r, float c) {
        const int r0 = std::clamp(int(std::floor(r)), 0, points - 1), c0 = std::clamp(int(std::floor(c)), 0, points - 1);
        return underside(rdram, mesh, r0, c0);
    };
    const Vec centre = unit(sub(at(mid, mid), Vec{0, 0, 0}));
    float radius = std::sqrt(dot(at(mid, mid), at(mid, mid)));
    if (radius < 1.0f) radius = 1.0f;
    Vec up = sub(at(0, mid), at(float(points - 1), mid));
    up = unit(sub(up, Vec{centre[0] * dot(up, centre), centre[1] * dot(up, centre), centre[2] * dot(up, centre)}));
    const Vec back{-centre[0], -centre[1], -centre[2]};
    const Vec right = unit(cross(up, back));
    // Row vectors: eye = v * V (x right, y up, the camera looking down -z).
    const float v[4][4] = {{right[0], up[0], back[0], 0}, {right[1], up[1], back[1], 0}, {right[2], up[2], back[2], 0}, {0, 0, 0, 1}};
    const float f = 1.0f / std::tan(std::clamp(fov_degrees, 30.0f, 150.0f) * 3.14159265f / 360.0f), aspect = 4.0f / 3.0f;
    const float near = radius * 0.05f, far = radius * 4.0f;
    const float p[4][4] = {{f / aspect, 0, 0, 0}, {0, f, 0, 0}, {0, 0, (far + near) / (near - far), -1}, {0, 0, 2.0f * far * near / (near - far), 0}};
    put_matrix(rdram, projection_matrix, p);
    put_matrix(rdram, view_matrix, v);

    // Vertices: 8 a cell (its corners dark, then its face inset), holes left out.
    std::uint32_t vtx = vertices_start, drawn = 0;
    for (const Quad& q : quads) {
        if (q.hole) continue;
        if (vtx + 8u * 16u > memory_end) return 0;
        const auto corner = [&](std::size_t k) {
            return Vec{float(q.corner[k][0]), float(q.corner[k][1]), float(q.corner[k][2])};
        };
        // The face: each corner pulled in towards the cell's middle.
        Vec middle{};
        for (std::size_t k = 0; k < 4; ++k)
            for (int a = 0; a < 3; ++a) middle[std::size_t(a)] += corner(k)[std::size_t(a)] / 4.0f;
        for (int ring = 0; ring < 2; ++ring)
            for (std::size_t k = 0; k < 4; ++k) {
                Vec pt = corner(k);
                if (ring == 1)
                    for (int a = 0; a < 3; ++a) pt[std::size_t(a)] += (middle[std::size_t(a)] - pt[std::size_t(a)]) * inset * 2.0f;
                const float light = ring == 0 ? 0.3f : (k == 2 || k == 3 ? 1.1f : 0.85f);
                const std::uint32_t a = vtx;
                for (int axis = 0; axis < 3; ++axis) store16(rdram, a + 2u * std::uint32_t(axis), std::int16_t(std::lround(pt[std::size_t(axis)])));
                store16(rdram, a + 6, 0), store16(rdram, a + 8, 0), store16(rdram, a + 10, 0);
                for (int c = 0; c < 3; ++c)
                    store8(rdram, a + 12u + std::uint32_t(c), std::uint8_t(std::min(255.0f, float(q.colour[std::size_t(c)]) * light)));
                store8(rdram, a + 15, 0xFF);
                vtx += 16u;
            }
        ++drawn;
    }

    std::uint32_t dl = commands_start;
    bool full = false;
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
        if (dl + 8u > commands_end - 8u) { full = true; return; }
        store32(rdram, dl, w0);
        store32(rdram, dl + 4u, w1);
        dl += 8u;
    };
    const auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) { cmd(0xBF000000u, (a * 10u) << 16 | (b * 10u) << 8 | (c * 10u)); };
    cmd(0xE7000000u, 0);              // pipe sync
    cmd(0xBA001402u, 0x00000000u);    // one-cycle
    cmd(0xB900031Du, 0x0F0A4000u);    // opaque surface, no z (from the centre each direction meets the shell once)
    cmd(0xFCFFFFFFu, 0xFFFE793Cu);    // combine: shade
    cmd(0xBB000000u, 0x00000000u);    // texture off
    cmd(0xB6000000u, 0xFFFFFFFFu);    // clear geometry modes (no culling, no lighting)
    cmd(0xB7000000u, 0x00000204u);    // shade, smooth
    cmd(0x01030040u, projection_matrix);
    cmd(0x01060040u, view_matrix);
    for (std::uint32_t c = 0; c < drawn; c += 2) {
        const std::uint32_t n = std::min<std::uint32_t>(2, drawn - c);
        cmd(0x04000000u | ((n * 8u - 1u) << 20) | (n * 8u * 16u), vertices_start + c * 8u * 16u);
        for (std::uint32_t k = 0; k < n; ++k) {
            const std::uint32_t o = k * 8u, in = o + 4u;
            tri(in + 0, in + 1, in + 2);
            tri(in + 0, in + 2, in + 3);
            for (std::uint32_t s = 0; s < 4; ++s) {
                const std::uint32_t s1 = (s + 1) % 4;
                tri(o + s, o + s1, in + s1);
                tri(o + s, in + s1, in + s);
            }
        }
    }
    cmd(0xBD000000u, 0x00000000u);    // pop the modelview
    if (projection != 0) cmd(0x01030040u, projection);
    cmd(0xE7000000u, 0);
    if (full) return 0;
    store32(rdram, dl, 0xB8000000u);  // G_ENDDL
    store32(rdram, dl + 4u, 0);
    return commands_start;
}

bool draw(std::uint8_t* rdram) {
    if (!enabled()) return false;
    std::uint32_t dl = load32(rdram, display_list_pointer);
    if (dl < 0x80000000u || dl >= 0x80400000u) return false;
    std::uint32_t projection = 0;  // the projection in use, loaded again after
    for (std::uint32_t at = dl - 8u, n = 0; n < 4096 && at >= 0x80000000u; at -= 8u, ++n) {
        const std::uint32_t w0 = load32(rdram, at);
        if ((w0 >> 24) == 0x01u && (w0 >> 16 & 0x01u) != 0) {
            projection = load32(rdram, at + 4u);
            break;
        }
    }
    static const float fov = [] {
        const char* v = std::getenv("BLOCKPLANET_ESCAPE_FOV");
        return v != nullptr ? float(std::atof(v)) : 100.0f;
    }();
    const std::uint32_t list = build(rdram, projection, fov);
    if (list == 0) return false;
    store32(rdram, dl, 0x06000000u);  // G_DL: call ours, then come back
    store32(rdram, dl + 4u, list);
    store32(rdram, display_list_pointer, dl + 8u);
    return true;
}

} // namespace tetrisphere::escape_view
