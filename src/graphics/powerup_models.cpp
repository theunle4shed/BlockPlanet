// BlockPlanet's own 3D power-up models (the owner, 2026-11-21: keep the 3D,
// in the flat icons' style, with level 2 and 3 forms).
//
// How (docs/research/blockplanet-powerup-models.md): the game draws a
// power-up through its model helper (0x8002D93C), which writes straight into
// the frame's display list (F3D): the projection and the model's matrices, two
// lights (a white one and a grey ambient), then each part's texture, vertices
// and triangles. After the helper has run, the geometry it wrote (from its
// first vertex load on) is cut and BlockPlanet's own written in its place, in
// the first part's space, so the game's placing, turning and flight are kept.
// Each part is one flat colour, given by the lights (the light's colour is the
// part's, the ambient a darker share of it); textures are off. A dark outline
// is drawn first: the same shape a little larger, only its inside faces
// (front faces culled), lit black.

#include "tetrisphere/powerup_models.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace tetrisphere::powerup_models {
namespace {

constexpr std::uint32_t display_list_pointer = 0x800F22B4u;  // the game's next free command
constexpr std::uint32_t model_memory = 0x807FA000u;          // vertices and lights (free upper memory)
constexpr std::uint32_t model_memory_end = 0x807FFF00u;

std::uint32_t load32(const std::uint8_t* rdram, std::uint32_t a) {
    std::uint32_t v;
    std::memcpy(&v, rdram + (a & 0x007FFFFCu), 4);
    return v;
}
void store32(std::uint8_t* rdram, std::uint32_t a, std::uint32_t v) { std::memcpy(rdram + (a & 0x007FFFFCu), &v, 4); }
void store8(std::uint8_t* rdram, std::uint32_t a, std::uint8_t v) { rdram[(a ^ 3u) & 0x007FFFFFu] = v; }  // big-endian byte in host-order words
void store16(std::uint8_t* rdram, std::uint32_t a, std::uint16_t v) { store8(rdram, a, std::uint8_t(v >> 8)), store8(rdram, a + 1u, std::uint8_t(v)); }

struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 unit(V3 a) {
    const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 0 ? a * (1.0f / l) : V3{0, 0, 1};
}

struct Tri { V3 p[3]; };
// A part: one colour, flat-shaded triangles (each its own normal).
struct Part {
    std::uint32_t rgb;
    std::vector<Tri> tris;
};

// Shapes along the z axis (the game's models point along z): a tube and a
// cone, `sides` faces round.
void tube(Part& part, float z0, float z1, float r, int sides = 8, bool caps = true) {
    for (int i = 0; i < sides; ++i) {
        const float a0 = 6.2831853f * float(i) / float(sides), a1 = 6.2831853f * float(i + 1) / float(sides);
        const V3 p00{r * std::cos(a0), r * std::sin(a0), z0}, p01{r * std::cos(a1), r * std::sin(a1), z0};
        const V3 p10{r * std::cos(a0), r * std::sin(a0), z1}, p11{r * std::cos(a1), r * std::sin(a1), z1};
        part.tris.push_back({{p00, p01, p11}});
        part.tris.push_back({{p00, p11, p10}});
        if (caps) {
            part.tris.push_back({{V3{0, 0, z0}, p01, p00}});
            part.tris.push_back({{V3{0, 0, z1}, p10, p11}});
        }
    }
}
void cone(Part& part, float z0, float z1, float r, int sides = 8) {
    for (int i = 0; i < sides; ++i) {
        const float a0 = 6.2831853f * float(i) / float(sides), a1 = 6.2831853f * float(i + 1) / float(sides);
        const V3 p0{r * std::cos(a0), r * std::sin(a0), z0}, p1{r * std::cos(a1), r * std::sin(a1), z0};
        part.tris.push_back({{p0, p1, V3{0, 0, z1}}});
        part.tris.push_back({{V3{0, 0, z0}, p1, p0}});
    }
}

// The models, in the first part's space of the game's own (its units; the
// Firecracker runs along z from about +1,600, the fuse's end, to -5,800, the
// cone's tip, about 1,000 across).
std::vector<Part> firecracker(int /*level*/) {
    Part body{0x8A3DF0, {}}, stripes{0xC9A2FF, {}}, tip{0xF28A2E, {}}, fuse{0xF3DC9C, {}};
    tube(body, 0.0f, -4200.0f, 620.0f);
    for (int s = 0; s < 3; ++s) tube(stripes, -900.0f - 1200.0f * float(s), -1300.0f - 1200.0f * float(s), 650.0f, 8, false);
    cone(tip, -4200.0f, -5800.0f, 820.0f);
    tube(fuse, 1500.0f, 0.0f, 120.0f, 6);
    return {body, stripes, tip, fuse};
}

std::vector<Part> model(int kind, int level) {
    switch (kind) {
    case 0: return firecracker(level);
    default: return {};
    }
}

// Writes the parts as F3D commands at `at` (lit vertices: position, then the
// normal in the colour bytes), the vertices in model memory; returns the end.
std::uint32_t write_parts(std::uint8_t* rdram, std::uint32_t at, const std::vector<Part>& parts, std::uint32_t light_dir) {
    std::uint32_t mem = model_memory;
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
        store32(rdram, at, w0), store32(rdram, at + 4u, w1);
        at += 8u;
    };
    // Shade only (no texture), one cycle.
    cmd(0xE7000000u, 0);
    cmd(0xBB000000u, 0x00000000u);           // texture off
    cmd(0xBA001402u, 0x00000000u);           // one cycle
    cmd(0xFCFFFFFFu, 0xFFFE793Cu);           // G_CC_SHADE
    cmd(0xB7000000u, 0x00020000u);           // lighting on
    const auto lights = [&](std::uint32_t rgb, float ambient) {
        if (mem + 32u > model_memory_end) return;
        const std::uint8_t r = std::uint8_t(rgb >> 16), g = std::uint8_t(rgb >> 8), b = std::uint8_t(rgb);
        const std::uint32_t l = mem;
        mem += 32u;
        // Light: colour, its copy, direction (the game's own), then the ambient.
        for (std::uint32_t k = 0; k < 2; ++k) store8(rdram, l + 4u * k, r), store8(rdram, l + 4u * k + 1u, g), store8(rdram, l + 4u * k + 2u, b), store8(rdram, l + 4u * k + 3u, 0);
        store32(rdram, l + 8u, light_dir), store32(rdram, l + 12u, 0);
        const auto amb = [&](std::uint8_t c) { return std::uint8_t(float(c) * ambient); };
        for (std::uint32_t k = 0; k < 2; ++k)
            store8(rdram, l + 16u + 4u * k, amb(r)), store8(rdram, l + 17u + 4u * k, amb(g)), store8(rdram, l + 18u + 4u * k, amb(b)), store8(rdram, l + 19u + 4u * k, 0);
        store32(rdram, l + 24u, 0), store32(rdram, l + 28u, 0);
        cmd(0x03860010u, l);
        cmd(0x03880010u, l + 16u);
    };
    const auto draw = [&](const Part& part, float grow) {
        // Five triangles a load (15 vertices of the 16).
        for (std::size_t i = 0; i < part.tris.size(); i += 5) {
            const std::size_t n = std::min<std::size_t>(5, part.tris.size() - i);
            if (mem + 48u * std::uint32_t(n) > model_memory_end) return;
            const std::uint32_t base = mem;
            for (std::size_t t = 0; t < n; ++t) {
                // Wound the other way round from the shapes' order (their outer
                // faces are front faces), the normal outwards.
                const Tri& tri = part.tris[i + t];
                const V3 q[3] = {tri.p[0], tri.p[2], tri.p[1]};
                const V3 normal = unit(cross(q[1] - q[0], q[2] - q[0]));
                for (int k = 0; k < 3; ++k) {
                    // Grown outwards (the outline) from the shape's own axis.
                    V3 p = q[k];
                    if (grow != 1.0f) p = V3{p.x * grow, p.y * grow, p.z} + V3{0, 0, p.z > 0 ? 60.0f : -60.0f} * (grow - 1.0f) * 10.0f;
                    const std::uint32_t v = mem;
                    mem += 16u;
                    store16(rdram, v, std::uint16_t(std::int16_t(std::lround(std::clamp(p.x, -32000.0f, 32000.0f)))));
                    store16(rdram, v + 2u, std::uint16_t(std::int16_t(std::lround(std::clamp(p.y, -32000.0f, 32000.0f)))));
                    store16(rdram, v + 4u, std::uint16_t(std::int16_t(std::lround(std::clamp(p.z, -32000.0f, 32000.0f)))));
                    store16(rdram, v + 6u, 0);
                    store32(rdram, v + 8u, 0);
                    store8(rdram, v + 12u, std::uint8_t(std::int8_t(std::lround(normal.x * 120.0f))));
                    store8(rdram, v + 13u, std::uint8_t(std::int8_t(std::lround(normal.y * 120.0f))));
                    store8(rdram, v + 14u, std::uint8_t(std::int8_t(std::lround(normal.z * 120.0f))));
                    store8(rdram, v + 15u, 0xFF);
                }
            }
            const std::uint32_t count = std::uint32_t(n) * 3u;
            cmd(0x04000000u | ((count - 1u) << 20) | (count * 16u), base);
            for (std::uint32_t t = 0; t < n; ++t) cmd(0xBF000000u, ((t * 3u) * 10u) << 16 | ((t * 3u + 1u) * 10u) << 8 | ((t * 3u + 2u) * 10u));
        }
    };
    // The outline: each part a little larger, its inside faces only, dark.
    cmd(0xB6000000u, 0x00002000u);  // back faces not culled
    cmd(0xB7000000u, 0x00001000u);  // front faces culled
    lights(0x1A1033, 1.0f);
    for (const auto& part : parts) draw(part, 1.07f);
    // The parts, their front faces.
    cmd(0xB6000000u, 0x00001000u);
    cmd(0xB7000000u, 0x00002000u);
    for (const auto& part : parts) {
        lights(part.rgb, 0.45f);
        draw(part, 1.0f);
    }
    return at;
}

}  // namespace

bool replace(std::uint8_t* rdram, std::uint32_t start, int kind, int level) {
    if (rdram == nullptr || start < 0x80000000u) return false;
    const std::uint32_t end = load32(rdram, display_list_pointer);
    if (end <= start || end - start > 0x10000u) return false;
    // The geometry starts at the first vertex load; the light's direction
    // is the game's (its white light, G_MV_L0).
    std::uint32_t first_vtx = 0, light_dir = 0x5A33C500u;
    for (std::uint32_t a = start; a < end; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if ((w0 >> 24) == 0x03u && ((w0 >> 16) & 0xFFu) == 0x86u && w1 >= 0x80000000u) light_dir = load32(rdram, w1 + 8u);
        if ((w0 >> 24) == 0x04u) {
            first_vtx = a;
            break;
        }
    }
    const auto parts = model(kind, level);
    if (first_vtx == 0 || parts.empty()) return false;
    const std::uint32_t new_end = write_parts(rdram, first_vtx, parts, light_dir);
    store32(rdram, display_list_pointer, new_end);
    return true;
}

}  // namespace tetrisphere::powerup_models
