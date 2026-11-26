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
#include <chrono>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace tetrisphere::powerup_models {
namespace {

constexpr std::uint32_t display_list_pointer = 0x800F22B4u;  // the game's next free command
// Each model is built once into this memory as a display list of its own
// (lights, vertices, commands) and called from the frame's list. Unused: it
// was all zeros in memory dumps of Rescue 3:1, a lesson and Hide & Seek 29 in
// play (inside zeros from about 0x80608000 to 0x8073E000; the title's scenes
// start at 0x80740000). (0x807E0000, tried first, is written every frame in
// play.) A model is checked by its first word before reuse, and built again if
// something has written over it.
constexpr std::uint32_t model_memory = 0x80700000u;
constexpr std::uint32_t model_memory_end = 0x8073C000u;

std::uint32_t load32(const std::uint8_t* rdram, std::uint32_t a) {
    std::uint32_t v;
    std::memcpy(&v, rdram + (a & 0x007FFFFCu), 4);
    return v;
}
void store32(std::uint8_t* rdram, std::uint32_t a, std::uint32_t v) { std::memcpy(rdram + (a & 0x007FFFFCu), &v, 4); }
void store8(std::uint8_t* rdram, std::uint32_t a, std::uint8_t v) { rdram[(a ^ 3u) & 0x007FFFFFu] = v; }  // big-endian byte in host-order words
std::int16_t load16(const std::uint8_t* rdram, std::uint32_t a) {
    return std::int16_t(rdram[(a ^ 3u) & 0x007FFFFFu] << 8 | rdram[((a + 1u) ^ 3u) & 0x007FFFFFu]);
}
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

// A triangle, with the centre of the shape it belongs to (the outline grows
// each shape about its own centre).
struct Tri { V3 p[3]; V3 c; };
// A part: one colour, flat-shaded triangles (each its own normal).
struct Part {
    std::uint32_t rgb;
    std::vector<Tri> tris;
};

// A placement: a rotation (rows) and an offset; shapes are built about the
// origin along z, then placed.
struct Place {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    V3 at{0, 0, 0};
    V3 apply(V3 p) const {
        return V3{m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z, m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z,
                  m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z} + at;
    }
};
Place place_at(V3 at) { Place p; p.at = at; return p; }
// Turned by `deg` about x, then y, then z (in that order), then moved.
Place turned(float rx, float ry, float rz, V3 at = {0, 0, 0}) {
    const float d = 3.14159265f / 180.0f;
    const float cx = std::cos(rx * d), sx = std::sin(rx * d), cy = std::cos(ry * d), sy = std::sin(ry * d), cz = std::cos(rz * d), sz = std::sin(rz * d);
    const float X[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}}, Y[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}, Z[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
    float yx[3][3] = {}, out[3][3] = {};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) yx[r][c] += Y[r][k] * X[k][c];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) out[r][c] += Z[r][k] * yx[k][c];
    Place p;
    std::memcpy(p.m, out, sizeof out);
    p.at = at;
    return p;
}

void add(Part& part, const Place& pl, V3 a, V3 b, V3 c, V3 centre) {
    part.tris.push_back({{pl.apply(a), pl.apply(b), pl.apply(c)}, pl.apply(centre)});
}
// A tube along z from z0 to z1 (radius r0 at z0, r1 at z1: a cone when one is 0).
void tube(Part& part, const Place& pl, float z0, float z1, float r0, float r1, int sides = 8, bool caps = true) {
    const V3 centre{0, 0, (z0 + z1) / 2};
    for (int i = 0; i < sides; ++i) {
        const float a0 = 6.2831853f * float(i) / float(sides), a1 = 6.2831853f * float(i + 1) / float(sides);
        const V3 p00{r0 * std::cos(a0), r0 * std::sin(a0), z0}, p01{r0 * std::cos(a1), r0 * std::sin(a1), z0};
        const V3 p10{r1 * std::cos(a0), r1 * std::sin(a0), z1}, p11{r1 * std::cos(a1), r1 * std::sin(a1), z1};
        if (r0 > 0 || r1 > 0) {
            if (r0 > 0) add(part, pl, p00, p01, p11, centre);
            if (r1 > 0) add(part, pl, p00, p11, p10, centre);
        }
        if (caps && r0 > 0) add(part, pl, V3{0, 0, z0}, p01, p00, centre);
        if (caps && r1 > 0) add(part, pl, V3{0, 0, z1}, p10, p11, centre);
    }
}
void tube(Part& part, float z0, float z1, float r, int sides = 8, bool caps = true) { tube(part, Place{}, z0, z1, r, r, sides, caps); }
void cone(Part& part, float z0, float z1, float r, int sides = 8) { tube(part, Place{}, z0, z1, r, 0.0f, sides, true); }
// A box, half sizes hx, hy, hz.
void box(Part& part, const Place& pl, float hx, float hy, float hz) {
    const V3 v[8] = {{-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz}, {-hx, -hy, hz}, {hx, -hy, hz}, {hx, hy, hz}, {-hx, hy, hz}};
    const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3}};
    for (const auto& q : f) {
        add(part, pl, v[q[0]], v[q[1]], v[q[2]], V3{0, 0, 0});
        add(part, pl, v[q[0]], v[q[2]], v[q[3]], V3{0, 0, 0});
    }
}
// A ball (low: `rings` bands, `sides` round).
void ball(Part& part, const Place& pl, float r, int rings = 4, int sides = 8) {
    for (int i = 0; i < rings; ++i) {
        const float t0 = 3.14159265f * float(i) / float(rings), t1 = 3.14159265f * float(i + 1) / float(rings);
        for (int j = 0; j < sides; ++j) {
            const float a0 = 6.2831853f * float(j) / float(sides), a1 = 6.2831853f * float(j + 1) / float(sides);
            const auto pt = [&](float t, float a) { return V3{r * std::sin(t) * std::cos(a), r * std::sin(t) * std::sin(a), r * std::cos(t)}; };
            const V3 p00 = pt(t0, a0), p01 = pt(t0, a1), p10 = pt(t1, a0), p11 = pt(t1, a1);
            if (i > 0) add(part, pl, p00, p01, p11, V3{0, 0, 0});
            if (i < rings - 1) add(part, pl, p00, p11, p10, V3{0, 0, 0});
        }
    }
}
// A ring (torus) about z: radius R, thickness r.
void ring(Part& part, const Place& pl, float R, float r, int sides = 16, int round = 4) {
    for (int i = 0; i < sides; ++i) {
        const float a0 = 6.2831853f * float(i) / float(sides), a1 = 6.2831853f * float(i + 1) / float(sides);
        for (int j = 0; j < round; ++j) {
            const float b0 = 6.2831853f * float(j) / float(round), b1 = 6.2831853f * float(j + 1) / float(round);
            const auto pt = [&](float a, float b) { return V3{(R + r * std::cos(b)) * std::cos(a), (R + r * std::cos(b)) * std::sin(a), r * std::sin(b)}; };
            const V3 c{R * std::cos((a0 + a1) / 2), R * std::sin((a0 + a1) / 2), 0};
            add(part, pl, pt(a0, b0), pt(a1, b0), pt(a1, b1), c);
            add(part, pl, pt(a0, b0), pt(a1, b1), pt(a0, b1), c);
        }
    }
}
// A curved claw: one tube swept along the points (the owner, 2026-11-26: the
// magnet's U, then a chain of separate short tubes each with its own caps,
// twist and six sides, showed ridges and bumps at every joint). A ring of
// `sides` points at each point, square to the path there (the frame carried
// along the path, so it does not twist), joined to the next ring; caps only at
// the two ends (`caps`). The first ring starts along x when the path starts
// along z (as the arms' tubes do, so a U meets them seamlessly).
void chain(Part& part, const std::vector<V3>& pts, float r, int sides = 6, bool caps = true, V3 start_dir = {}, V3 end_dir = {}) {
    const std::size_t n = pts.size();
    if (n < 2) return;
    std::vector<V3> tangent(n), xs(n), ys(n);
    for (std::size_t i = 0; i < n; ++i) {
        const V3 a = i > 0 ? unit(pts[i] - pts[i - 1]) : V3{0, 0, 0};
        const V3 b = i + 1 < n ? unit(pts[i + 1] - pts[i]) : V3{0, 0, 0};
        tangent[i] = unit(a + b);
    }
    // The ends' directions, when given (a U meeting straight arms: square to them).
    if (start_dir.x != 0 || start_dir.y != 0 || start_dir.z != 0) tangent[0] = unit(start_dir);
    if (end_dir.x != 0 || end_dir.y != 0 || end_dir.z != 0) tangent[n - 1] = unit(end_dir);
    xs[0] = unit(std::abs(tangent[0].x) < 0.9f ? cross(V3{0, 1, 0}, tangent[0]) : cross(V3{0, 0, 1}, tangent[0]));
    ys[0] = cross(tangent[0], xs[0]);
    for (std::size_t i = 1; i < n; ++i) {
        // Carried along: the last x made square to the new direction.
        const V3 x = xs[i - 1] - tangent[i] * (xs[i - 1].x * tangent[i].x + xs[i - 1].y * tangent[i].y + xs[i - 1].z * tangent[i].z);
        xs[i] = unit(x), ys[i] = cross(tangent[i], xs[i]);
    }
    const auto at = [&](std::size_t i, int k) {
        const float a = 6.2831853f * float(k) / float(sides);
        return pts[i] + xs[i] * (r * std::cos(a)) + ys[i] * (r * std::sin(a));
    };
    const Place none{};
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const V3 mid = (pts[i] + pts[i + 1]) * 0.5f;
        for (int k = 0; k < sides; ++k) {
            add(part, none, at(i, k), at(i, k + 1), at(i + 1, k + 1), mid);
            add(part, none, at(i, k), at(i + 1, k + 1), at(i + 1, k), mid);
        }
    }
    if (caps)
        for (int k = 0; k < sides; ++k) {
            add(part, none, pts[0], at(0, k + 1), at(0, k), pts[1]);
            add(part, none, pts[n - 1], at(n - 1, k), at(n - 1, k + 1), pts[n - 2]);
        }
}
// Every triangle of a model moved and scaled (a level's bigger or extra form).
std::vector<Part> placed(std::vector<Part> parts, const Place& pl, float scale) {
    for (auto& part : parts)
        for (auto& t : part.tris) {
            for (auto& p : t.p) p = pl.apply(p * scale);
            t.c = pl.apply(t.c * scale);
        }
    return parts;
}
std::vector<Part> merged(std::vector<Part> a, const std::vector<Part>& b) {
    for (const auto& part : b) {
        bool found = false;
        for (auto& q : a)
            if (q.rgb == part.rgb) q.tris.insert(q.tris.end(), part.tris.begin(), part.tris.end()), found = true;
        if (!found) a.push_back(part);
    }
    return a;
}

// The colours (the flat icons', tools/art/powerups.py).
constexpr std::uint32_t purple = 0x8A3DF0, lilac = 0xC9A2FF, orange = 0xF28A2E, fuse_tan = 0xF3DC9C, red = 0xD93636, bright_red = 0xEF4A4A,
                        gold = 0xF2C53D, pink_tip = 0xF0D0F0, ring_green = 0x2FCF4A, atom_purple = 0xA24BFF, orbit = 0xB78CFF, electron = 0xFFD23F,
                        white = 0xF4F1EA, flame = 0xFF8A1F, flame_core = 0xFFF07A, smoke = 0xE9E0FA, gun_green = 0x2FBF68, grip_green = 0x1F8A4C, beam = 0x6FFFB0, spark_blue = 0x6FD3FF;

// The level's trim (the owner, 2026-11-21: one rule for levelling up): each
// power-up's trim (stripes, band, tips, orbits, rings) is its own colour at
// level 1, blue at level 2 and gold at level 3 (as the icons, powerups.py).
std::uint32_t trim(int level, std::uint32_t base) { return level == 1 ? base : level == 2 ? 0x4FB4FFu : 0xFFC53Du; }

// The models, each in the first part's space of the game's own (its units).
// Firecracker: along z from about +1,600 (the fuse's end) to -5,800 (the
// cone's tip), about 1,000 across.
std::vector<Part> one_rocket(std::uint32_t stripe = lilac) {
    Part body{purple, {}}, stripes{stripe, {}}, tip{orange, {}}, fuse{fuse_tan, {}};
    tube(body, 0.0f, -4200.0f, 620.0f);
    for (int s = 0; s < 3; ++s) tube(stripes, -900.0f - 1200.0f * float(s), -1300.0f - 1200.0f * float(s), 650.0f, 8, false);
    cone(tip, -4200.0f, -5800.0f, 820.0f);
    tube(fuse, 1500.0f, 0.0f, 120.0f, 6);
    return {body, stripes, tip, fuse};
}
std::vector<Part> firecracker(int level) {
    if (level == 1) return one_rocket();
    const std::uint32_t t = trim(level, lilac);
    if (level == 2) return placed(one_rocket(t), place_at(V3{0, 0, 600}), 1.2f);
    // Three rockets, side by side, the middle one higher.
    auto out = placed(one_rocket(t), turned(0, -12, 0, V3{-1500, 0, 400}), 0.8f);
    out = merged(out, placed(one_rocket(t), turned(0, 12, 0, V3{1500, 0, 400}), 0.8f));
    return merged(out, placed(one_rocket(t), place_at(V3{0, 0, -300}), 0.85f));
}

// Dynamite: sticks along y, from about -1,500 to 1,500, fuses above to 2,100,
// across x and z about ±770.
std::vector<Part> dynamite(int level) {
    Part sticks{red, {}}, front{bright_red, {}}, band{trim(level, gold), {}}, fuse{fuse_tan, {}}, spark{electron, {}};
    // Five sticks at every level (the owner, 2026-11-22): in use the bundle
    // splits into five sticks, each its own blast, so the bundle shows five.
    // The level shows in the band's colour, the fuses and (level 3) bomblets.
    // A round bundle (the owner, 2026-11-26: clumped together and wrapped,
    // as it starts, before the five fly apart): five sticks in a ring, side
    // by side, the front one bright, two wraps of tape round them.
    const float r = 310.0f, ring_r = 540.0f;  // touching: 2 * 540 * sin 36° ≈ 2 * 310
    const Place up = turned(-90, 0, 0);  // tubes along y
    for (int k = 0; k < 5; ++k) {
        const float a = 6.2831853f * float(k) / 5.0f;  // k 0 faces the front (+z)
        Place pl = up;
        pl.at = V3{ring_r * std::sin(a), 0, ring_r * std::cos(a)};
        tube(k == 0 ? front : sticks, pl, -1500.0f, 1500.0f, r, r);
    }
    for (const float y : {-700.0f, 700.0f}) {
        Place pl = up;
        pl.at = V3{0, y, 0};
        tube(band, pl, -170.0f, 170.0f, ring_r + r + 40.0f, ring_r + r + 40.0f, 20, false);
    }
    // The fuses from the top of the bundle, one per level, from the middle
    // sticks of the ring's front.
    const int fuses = level;
    for (int f = 0; f < fuses; ++f) {
        const float a = 6.2831853f * float(f == 0 ? 0 : f == 1 ? 1 : 4) / 5.0f;
        const float x = ring_r * std::sin(a), z = ring_r * std::cos(a);
        chain(fuse, {V3{x, 1500, z}, V3{x + 150, 1850, z}, V3{x + 400, 2100, z}}, 70.0f);
        ball(spark, place_at(V3{x + 430, 2160, z}), 170.0f, 3, 6);
    }
    return {sticks, front, band, fuse, spark};
}

// Electro Magnet: its claws point along -z (to about -3,700) from a green
// ring at about z 150 to 730, about 940 across.
std::vector<Part> horseshoe(float s, std::uint32_t tip, bool sparks_on) {
    // Round arms and a smooth U at the bottom (the owner, 2026-11-21: square
    // bars looked like two towers), pale round tips.
    Part arms{red, {}}, tips{tip, {}}, sparks{spark_blue, {}};
    const float half = 720 * s, r = 380 * s, bend = 420 * s;
    for (int side = -1; side <= 1; side += 2) {
        tube(arms, place_at(V3{side * half, 0, 0}), bend, -1700 * s, r, r, 10, false);
        tube(tips, place_at(V3{side * half, 0, 0}), -1700 * s, -2300 * s, r * 1.04f, r * 1.04f, 10, true);
    }
    std::vector<V3> u;
    for (int i = 0; i <= 16; ++i) {
        const float t = 3.14159265f * float(i) / 16.0f;
        u.push_back(V3{half * std::cos(t), 0, bend + half * std::sin(t)});
    }
    chain(arms, u, r, 10, false, V3{0, 0, 1}, V3{0, 0, -1});  // (the arms' ten sides, no caps, square to them at both ends)
    if (sparks_on)
        for (int side = -1; side <= 1; side += 2)
            chain(sparks, {V3{side * 1300 * s, 0, -1800 * s}, V3{side * 1600 * s, 0, -1500 * s}, V3{side * 1350 * s, 0, -1200 * s}, V3{side * 1650 * s, 0, -900 * s}}, 55 * s);
    return {arms, tips, sparks};
}
std::vector<Part> magnet(int level) {
    // Level 1 a horseshoe; level 2 two, fanned out (the owner: a clear step);
    // level 3 the game's own form, four claws that curve out and hook back in
    // at the top (the owner); all on the green ring.
    Part base{ring_green, {}};
    if (level == 1) {
        ring(base, place_at(V3{0, 0, 1350}), 950.0f, 180.0f);
        return merged(horseshoe(1.0f, pink_tip, false), {base});  // (its sparks drawn by replace(), buzzing: magnet_sparks)
    }
    if (level == 2) {
        ring(base, place_at(V3{0, 0, 1350}), 1500.0f, 200.0f);
        auto out = placed(horseshoe(0.8f, trim(2, pink_tip), false), turned(0, -18, 0, V3{-950, 0, 300}), 1.0f);
        out = merged(out, placed(horseshoe(0.8f, trim(2, pink_tip), false), turned(0, 18, 0, V3{950, 0, 300}), 1.0f));
        return merged(out, {base});
    }
    Part claws{red, {}}, tips{trim(3, pink_tip), {}};
    for (int k = 0; k < 4; ++k) {
        const float a = 6.2831853f * (float(k) + 0.5f) / 4.0f, cx = std::cos(a), cy = std::sin(a);
        // Out from the ring, round, and back in at the top, the tip pointing inwards.
        const std::vector<V3> pts{V3{cx * 500, cy * 500, 600}, V3{cx * 1100, cy * 1100, -400}, V3{cx * 1550, cy * 1550, -1500},
                                  V3{cx * 1450, cy * 1450, -2500}, V3{cx * 900, cy * 900, -3100}};
        chain(claws, pts, 230.0f);
        ball(tips, place_at(pts.back() + V3{-cx * 150, -cy * 150, 60}), 300.0f, 3, 6);
    }
    ring(base, place_at(V3{0, 0, 700}), 900.0f, 200.0f);
    return {claws, tips, base};
}

// Atom: about its middle, the nucleus about 1,000 across, its electrons about
// 1,400 out.
// The Atom's rings: three (four at level 3), each tilted about the model's
// z. Its electrons are not part of the model: replace() draws one on each
// ring, going round it (the owner, 2026-11-26), through a matrix each frame.
constexpr float atom_orbit = 1400.0f;
int atom_orbits(int level) { return level < 3 ? 3 : 4; }
Place atom_ring(int o, int level) { return turned(70, 0, 180.0f * float(o) / float(atom_orbits(level))); }
std::vector<Part> atom(int level) {
    Part nucleus{atom_purple, {}}, orbits{trim(level, orbit), {}};
    ball(nucleus, Place{}, level == 1 ? 480.0f : 560.0f, 5, 10);
    for (int o = 0; o < atom_orbits(level); ++o) ring(orbits, atom_ring(o, level), atom_orbit, 60.0f, 20, 3);
    return {nucleus, orbits};
}
std::vector<Part> atom_electron() {
    Part electrons{electron, {}};
    ball(electrons, Place{}, 180.0f, 3, 6);
    return {electrons};
}

// Bomb: a missile along z, its nose to -z (about -4,000), fins about ±1,500.
// The flame behind a flying missile (the owner, 2026-11-22: level 1 flies
// with the game's own animated fire; levels 2 and 3 had a still spike). -1
// none (held in the Magic box, strapped in the launcher, or level 1, whose
// fire is the game's), 0 to 3 a frame of the flicker, each its own model.
int flame_phase = -1;
// The Magnet's sparks, beside each claw (the owner, 2026-11-26: make them buzz
// like electricity): a zigzag of six points, a different one at each phase
// (0 to 3), so they flicker as the flame does; replace() calls them after the
// magnet, through its matrix.
std::vector<Part> magnet_sparks() {
    static const float jitter[4][6] = {{0.0f, 1.0f, -0.6f, 0.9f, -1.0f, 0.4f},
                                       {0.3f, -0.8f, 1.0f, -0.5f, 0.7f, -0.9f},
                                       {-0.4f, 0.6f, -1.0f, 1.0f, -0.3f, 0.8f},
                                       {0.8f, -1.0f, 0.5f, -0.7f, 1.0f, -0.2f}};
    const int ph = std::clamp(flame_phase, 0, 3);
    Part sparks{spark_blue, {}};
    for (int side = -1; side <= 1; side += 2) {
        std::vector<V3> zig;
        for (int i = 0; i < 6; ++i) {
            const float out = 1450.0f + 250.0f * jitter[ph][i] * (i % 2 ? 1.0f : -1.0f);
            zig.push_back(V3{side * out, 120.0f * jitter[(ph + 1) % 4][i], -2000.0f + 230.0f * float(i)});
        }
        chain(sparks, zig, ph % 2 ? 45.0f : 60.0f, 4);
    }
    return {sparks};
}
// One of the Firecracker's smoke rings (replace() leaves them behind it): a
// ring 1,000 across its middle, square to z, thinner at each stage of its life
// (the flame's phase, 0 to 3).
std::vector<Part> smoke_ring() {
    static const float thick[4] = {110.0f, 80.0f, 55.0f, 30.0f};
    Part puff{smoke, {}};
    ring(puff, Place{}, 1000.0f, thick[std::clamp(flame_phase, 0, 3)], 20, 4);
    return {puff};
}
std::vector<Part> missile(float flame_len, std::uint32_t band_colour = red) {
    Part body{white, {}}, reds{red, {}}, band{band_colour, {}}, fire{flame, {}}, core{flame_core, {}};
    tube(body, 1100.0f, -2400.0f, 700.0f);
    tube(reds, Place{}, -2400.0f, -3900.0f, 700.0f, 0.0f, 8, true);
    tube(band, -700.0f, -1100.0f, 730.0f, 8, false);
    for (int k = 0; k < 4; ++k) {
        const Place pl = turned(0, 0, 45.0f + 90.0f * float(k));
        Place fin = pl;
        fin.at = pl.apply(V3{1050, 0, 1100});
        box(reds, fin, 450.0f, 70.0f, 400.0f);
    }
    if (flame_phase < 0) return {body, reds, band};
    // Two cones, an orange one round a shorter yellow one, their lengths and
    // widths changing frame to frame.
    static const float longer[4] = {0.8f, 1.15f, 0.95f, 1.35f}, wider[4] = {1.0f, 0.9f, 1.08f, 0.95f};
    const float len = flame_len * longer[flame_phase], r = 450.0f * wider[flame_phase];
    // Yellow at the nozzle, running into an orange tip; both drawn at full
    // brightness (see the lights in write_parts).
    const float split = 1100.0f + 0.32f * len;
    tube(core, Place{}, 1100.0f, split, r, 0.82f * r, 8, true);
    tube(fire, Place{}, split, 1100.0f + len, 0.82f * r, 0.0f, 8, true);
    return {body, reds, band, core, fire};
}
std::vector<Part> bomb(int level) {
    if (level == 1) return missile(1800.0f);  // (a long flame, as the Firecracker's fire: the owner, 2026-11-26)
    if (level == 2) return placed(missile(1200.0f, trim(2, red)), Place{}, 1.15f);
    return merged(placed(missile(900.0f, trim(3, red)), place_at(V3{-1100, 0, 200}), 0.72f),
                  placed(missile(900.0f, trim(3, red)), place_at(V3{1100, 0, -200}), 0.72f));
}

// Ray Gun: its barrel to -z, about ±1,800 across, -3,500 to 900 along z.
std::vector<Part> raygun(int level) {
    Part body{gun_green, {}}, grip{grip_green, {}}, rings{trim(level, gold), {}}, nozzle{gold, {}}, ray{beam, {}};
    tube(body, Place{}, 900.0f, -900.0f, 900.0f, 800.0f, 10, true);
    tube(nozzle, Place{}, -900.0f, -2300.0f, 500.0f, 260.0f, 8, true);
    for (int r = 0; r < (level == 1 ? 2 : 3); ++r) tube(rings, 400.0f - 500.0f * float(r), 150.0f - 500.0f * float(r), 940.0f, 10, true);
    box(grip, turned(-20, 0, 0, V3{0, -1200, 500}), 280.0f, 700.0f, 300.0f);
    const float reach = level == 1 ? 900.0f : level == 2 ? 1200.0f : 1500.0f, width = level == 1 ? 90.0f : level == 2 ? 150.0f : 220.0f;
    tube(ray, -2300.0f, -2300.0f - reach, width, 6);
    if (level == 3) ball(ray, place_at(V3{0, 0, -2400}), 380.0f, 3, 8);
    return {body, grip, rings, nozzle, ray};
}

// The four power-ups (the owner, 2026-11-21; as the icons, powerups.py): the
// Missile (the game's Firecracker and Bomb), Dynamite, the Magnet (the game's
// Magnet and Ray Gun; level 3 is the Ray Gun) and the Atom. Each level its own
// form, blue trim at level 2, gold at level 3 with its power surge.
// The barrage's launcher (the owner, 2026-11-22, second design: no
// cannons): three missiles strapped round a central column, noses to -z, held
// by two sets of clamps; empty, the column and its clamps.
std::vector<Part> launcher(bool loaded) {
    Part column{0x4A5060, {}}, cap{0x2E333D, {}}, clamps{0xFFC53D, {}};
    std::vector<Part> out;
    const float r = launcher_missile_radius;
    tube(column, Place{}, -1500.0f, 1300.0f, 420.0f, 420.0f, 10, false);
    tube(cap, Place{}, -1500.0f, -2300.0f, 420.0f, 0.0f, 10, true);   // its nose
    tube(cap, Place{}, 1300.0f, 1600.0f, 420.0f, 300.0f, 10, true);   // its tail
    for (int k = 0; k < 3; ++k) {
        const float a = 6.2831853f * float(k) / 3.0f;
        const V3 at{r * std::cos(a), r * std::sin(a), 0};
        if (loaded) {
            const int phase = flame_phase;
            flame_phase = -1;  // strapped in: no flame
            out = merged(out, placed(missile(400.0f, trim(3, red)), place_at(V3{at.x, at.y, 300.0f}), 0.55f));
            flame_phase = phase;
        }
        // A clamp arm from the column out to the missile, front and back.
        for (const float z : {-900.0f, 500.0f}) {
            Place arm = turned(0, 0, a * 57.29578f);
            arm.at = V3{0.5f * at.x, 0.5f * at.y, z};
            box(clamps, arm, 0.5f * r, 110.0f, 120.0f);
        }
    }
    for (const float z : {-900.0f, 500.0f}) ring(clamps, place_at(V3{0, 0, z}), 450.0f, 80.0f, 12, 3);
    out.push_back(column);
    out.push_back(cap);
    out.push_back(clamps);
    return out;
}

std::vector<Part> missile_family(int level) {
    if (level == 1) {  // Missile (its fire in flight is the game's own)
        const int phase = flame_phase;
        flame_phase = -1;
        auto out = placed(missile(700.0f), Place{}, 0.9f);
        flame_phase = phase;
        return out;
    }
    if (level == 2) return placed(missile(1300.0f, trim(2, red)), Place{}, 1.15f); // Big Missile
    // Missile Barrage: the launcher, loaded with three (the owner, 2026-11-22).
    return launcher(true);
}

std::vector<Part> dynamite_family(int level) {
    if (level < 3) return dynamite(level);
    // Cluster sticks: five sticks (a gold band), bomblets round them evenly,
    // inside and just outside.
    auto out = dynamite(2);
    for (auto& part : out)
        if (part.rgb == trim(2, gold)) part.rgb = trim(3, gold);
    Part bomblets{0xFF7A2E, {}}, glints{electron, {}};
    for (int k = 0; k < 6; ++k) {
        const float a = 6.2831853f * (float(k) + 0.5f) / 6.0f;
        const V3 p{2300.0f * std::cos(a), (k % 2 ? 900.0f : -900.0f), 1100.0f * std::sin(a)};
        ball(bomblets, place_at(p), 320.0f, 3, 6);
        ball(glints, place_at(p + V3{-120, 200, 0}), 110.0f, 2, 4);
    }
    out.push_back(bomblets);
    out.push_back(glints);
    return out;
}

// One stick of the bundle, as it flies on its own once the bundle splits:
// the stick, its band in the level's colour, a fuse and its spark.
std::vector<Part> dynamite_stick(int level) {
    Part stick{red, {}}, band{trim(level, gold), {}}, fuse{fuse_tan, {}}, spark{electron, {}};
    tube(stick, turned(-90, 0, 0), -1500.0f, 1500.0f, 310.0f, 310.0f);
    box(band, place_at(V3{0, 300, 0}), 360.0f, 260.0f, 360.0f);
    chain(fuse, {V3{0, 1500, 0}, V3{150, 1850, 0}, V3{400, 2100, 0}}, 70.0f);
    ball(spark, place_at(V3{430, 2160, 0}), 170.0f, 3, 6);
    return {stick, band, fuse, spark};
}

std::vector<Part> magnet_family(int level) {
    if (level < 3) return magnet(level);  // Magnet, Power Magnet
    // Ray Gun: harder, faster, deeper; pulse rings round its beam.
    auto out = raygun(3);
    Part pulses{0xFFC53D, {}}, inner{0xD8FFE9, {}};
    ring(pulses, place_at(V3{0, 0, -3000}), 900.0f, 80.0f, 14, 3);
    ring(inner, place_at(V3{0, 0, -2700}), 550.0f, 70.0f, 12, 3);
    out.push_back(pulses);
    out.push_back(inner);
    return out;
}

std::vector<Part> atom_family(int level) {
    if (level == 1) return atom(1);
    // (Level 2's crater is the icon's alone: on the turning model it read as a
    // dark disc beside the atom.)
    if (level == 2) return atom(2);
    // Splitting the sphere: inside a wide purple and gold ring.
    auto out = placed(atom(3), Place{}, 0.85f);
    Part outer{0xB84DFF, {}}, gold_ring{0xFFC53D, {}};
    ring(outer, turned(80, 0, 15), 2000.0f, 150.0f, 28, 4);
    ring(gold_ring, turned(80, 0, 15), 1750.0f, 80.0f, 28, 3);
    out.push_back(outer);
    out.push_back(gold_ring);
    return out;
}

}  // namespace
bool levels_on();
namespace {
std::vector<Part> model(int kind, int level) {
    // The game's six as in the original (the owner, 2026-11-23: the levels kept
    // as research for later): each its own model, as its icon.
    if (!levels_on()) switch (kind) {
        case 0: return firecracker(1);
        case 1: return dynamite(1);
        case 2: return magnet(1);
        case 3: return atom(1);
        case 4: return bomb(1);
        case 5: return raygun(1);
        case electron_kind: return atom_electron();
    case smoke_ring_kind: return smoke_ring();
    case magnet_sparks_kind: return magnet_sparks();
        default: break;
        }
    // With the levels (research): the game's six shown as the four, the
    // Firecracker and Bomb as the Missile, the Magnet and Ray Gun as the Magnet.
    switch (kind) {
    case 0: case 4: return missile_family(level);
    case 1: return dynamite_family(level);
    case 2: case 5: return magnet_family(level);
    case 3: return atom_family(level);
    case stick_kind: return dynamite_stick(level);
    case electron_kind: return atom_electron();
    case smoke_ring_kind: return smoke_ring();
    case magnet_sparks_kind: return magnet_sparks();
    case dive_kind: return placed(missile_family(level), turned(180, 0, 0), 1.0f);
    case barrage_missile_kind: return placed(missile(900.0f, trim(3, red)), Place{}, 1.0f);
    case empty_launcher_kind: return launcher(false);
    default: return {};
    }
}

// Writes the parts as F3D commands at `at` (lit vertices: position, then the
// normal in the colour bytes), the vertices in model memory; returns the end.
// Writes the model at `mem` onwards: its commands first (`dl_room` bytes kept
// for them), then its lights and vertices. Returns the end, or 0 if it does
// not fit before `limit`.
std::uint32_t write_parts(std::uint8_t* rdram, std::uint32_t at, std::uint32_t dl_room, std::uint32_t limit, const std::vector<Part>& parts,
                          std::uint32_t light_dir) {
    const std::uint32_t model_memory_end = limit;
    bool full = false;
    static const float outline = [] {  // research: BLOCKPLANET_MODEL_OUTLINE (model units)
        const char* v = std::getenv("BLOCKPLANET_MODEL_OUTLINE");
        return v ? std::strtof(v, nullptr) : 0.0f;
    }();
    std::uint32_t mem = at + dl_room;
    const std::uint32_t dl_end = mem - 8u;  // room for the end command
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
        if (at >= dl_end) { full = true; return; }
        store32(rdram, at, w0), store32(rdram, at + 4u, w1);
        at += 8u;
    };
    // Shade only (no texture), one cycle.
    cmd(0xE7000000u, 0);
    cmd(0xBB000000u, 0x00000000u);           // texture off
    cmd(0xBA001402u, 0x00000000u);           // one cycle
    cmd(0xFCFFFFFFu, 0xFFFE793Cu);           // G_CC_SHADE
    cmd(0xB7000000u, 0x00020000u);           // lighting on
    // One light and the ambient (G_MW_NUMLIGHT): the game sets two for some
    // of its effects (the Magnet in use), which turned our ambient into a
    // second light and left the game's dark ambient, so the model drew black.
    cmd(0xBC000002u, 0x80000040u);
    const auto lights = [&](std::uint32_t rgb, float ambient) {
        if (mem + 32u > model_memory_end) { full = true; return; }
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
            if (mem + 48u * std::uint32_t(n) > model_memory_end) { full = true; return; }
            const std::uint32_t base = mem;
            for (std::size_t t = 0; t < n; ++t) {
                // Each triangle wound to face away from its shape's centre (the
                // front faces; checked in the game: cross(q1 - q0, q2 - q0)
                // pointing out), the normal outwards.
                const Tri& tri = part.tris[i + t];
                V3 q[3] = {tri.p[0], tri.p[1], tri.p[2]};
                {
                    const V3 mid = (q[0] + q[1] + q[2]) * (1.0f / 3.0f) - tri.c;
                    const V3 n = cross(q[1] - q[0], q[2] - q[0]);
                    if (n.x * mid.x + n.y * mid.y + n.z * mid.z < 0.0f) std::swap(q[1], q[2]);
                }
                const V3 normal = unit(cross(q[1] - q[0], q[2] - q[0]));
                for (int k = 0; k < 3; ++k) {
                    // Grown outwards (the outline) from its shape's centre, by a set
                    // thickness.
                    V3 p = q[k];
                    if (grow > 0.0f) p = p + unit(p - tri.c) * grow;
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
    // The parts, drawn from both sides: the effect's draws can mirror the model
    // (the Magnet pulling bricks showed its dark outline shell instead of its
    // colours), which turns front faces round. The outline (each part a little
    // larger, its inside faces, dark) is off by default for the same reason;
    // BLOCKPLANET_MODEL_OUTLINE (model units) brings it back (research).
    if (outline > 0.0f) {
        cmd(0xB6000000u, 0x00002000u);
        cmd(0xB7000000u, 0x00001000u);
        lights(0x1A1033, 1.0f);
        for (const auto& part : parts) draw(part, outline);
    }
    cmd(0xB6000000u, 0x00003000u);  // no culling
    for (const auto& part : parts) {
        // (0.45 left the models dim in the effect.) A missile's flame glows:
        // its colour in full, whatever the light.
        const bool glowing = part.rgb == flame || part.rgb == flame_core || part.rgb == smoke || part.rgb == spark_blue;
        lights(part.rgb, glowing ? 1.0f : 0.65f);
        draw(part, 0.0f);
    }
    store32(rdram, at, 0xB8000000u), store32(rdram, at + 4u, 0);  // G_ENDDL
    return full ? 0u : mem;
}

}  // namespace

namespace {
struct Built { std::uint32_t at = 0, marker = 0, light_dir = 0; };
Built built[last_kind + 1][4][5];  // kind, level, flame phase + 1
std::uint32_t next_free = model_memory;
}  // namespace

std::uint32_t model_list(std::uint8_t* rdram, int draw_kind, int level, std::uint32_t light_dir, int phase) {
    if (rdram == nullptr || draw_kind < 0 || draw_kind > last_kind || level < 1 || level > 3) return 0;
    if (phase < -1 || phase > 3) phase = -1;
    const int kind = draw_kind;
    Built& b = built[draw_kind][level][phase + 1];
    const std::uint32_t marker = 0xB10C0000u | std::uint32_t(phase + 1) << 12 | std::uint32_t(draw_kind) << 8 | std::uint32_t(level);
    if (b.at == 0 || load32(rdram, b.at) != marker) {  // (the light's direction as first seen)
        flame_phase = phase;
        const auto parts = model(draw_kind, level);
        flame_phase = -1;
        if (parts.empty()) return 0;
        std::size_t tris = 0;
        for (const auto& part : parts) tris += part.tris.size();
        // Commands: two passes, a vertex load each five triangles, the lights.
        const std::uint32_t dl_room = std::uint32_t(tris * 2u * 8u + tris * 2u / 5u * 8u + 64u * parts.size() + 256u);
        const std::uint32_t need = 8u + dl_room + std::uint32_t(tris * 2u * 48u) + 32u * std::uint32_t(parts.size() + 1u) + 64u;
        if (need > model_memory_end - model_memory) return 0;
        if (next_free + need > model_memory_end) {
            // Full: start again (every model is built again when next drawn).
            next_free = model_memory;
            for (auto& k : built)
                for (auto& l : k)
                    for (auto& f : l) f = Built{};
        }
        const std::uint32_t at = next_free;
        store32(rdram, at, marker), store32(rdram, at + 4u, 0);
        const std::uint32_t used = write_parts(rdram, at + 8u, dl_room, at + need, parts, light_dir);
        if (used == 0) return 0;
        next_free = (used + 15u) & ~15u;
        b = Built{at, marker, light_dir};
        std::fprintf(stdout, "{\"event\":\"powerup_model\",\"kind\":%d,\"level\":%d,\"triangles\":%zu,\"bytes\":%u}\n", kind, level, tris, used - at);
        std::fflush(stdout);
    }
    return b.at + 8u;
}

bool levels_on() {
    static const bool on = std::getenv("BLOCKPLANET_POWERUP_LEVELS") != nullptr;
    return on;
}

int flame_frame() {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return int((ms / 70) % 4);
}

namespace {
std::uint32_t smoke_rings(std::uint8_t* rdram, std::uint32_t start, std::uint32_t call, std::uint32_t light_dir);
}  // namespace

bool replace(std::uint8_t* rdram, std::uint32_t start, int kind, int level, std::uint32_t only_source, std::uint32_t* source, bool whole,
             int draw_kind, double turn_degrees, double roll_degrees) {
    if (draw_kind < 0) draw_kind = kind;
    if (rdram == nullptr || start < 0x80000000u || kind < 0 || kind > last_kind || draw_kind > last_kind || level < 1 || level > 3) return false;
    const std::uint32_t end = load32(rdram, display_list_pointer);
    if (end <= start || end - start > 0x10000u) return false;
    // The game's body: its vertex loads. While held (only_source 0) every
    // load in the draw is the body, and their sources are recorded for the
    // kind; after use (the flight, only_source the held draw's first source)
    // only loads from those sources are the body, and everything else in the
    // draw (the flight's swirl, flame and smoke rings) is kept. The light's
    // direction is the game's (its white light, G_MV_L0).
    static std::array<std::vector<std::uint32_t>, last_kind + 1> body_sources;
    std::uint32_t first_vtx = 0, light_dir = 0x5A33C500u;
    std::vector<std::uint32_t> body_loads;
    // `whole`: the whole draw is the body, and the held model's sources are
    // left as they are (the Bomb's second model, a Dynamite stick).
    const bool held = only_source == 0 && !whole;
    if (held) body_sources[std::size_t(kind)].clear();
    for (std::uint32_t a = start; a < end; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if ((w0 >> 24) == 0x03u && ((w0 >> 16) & 0xFFu) == 0x86u && w1 >= 0x80000000u && first_vtx == 0) light_dir = load32(rdram, w1 + 8u);
        if ((w0 >> 24) != 0x04u) continue;
        auto& known = body_sources[std::size_t(kind)];
        const bool body = held || whole || std::find(known.begin(), known.end(), w1) != known.end();
        if (!body) continue;
        if (held && known.size() < 64) known.push_back(w1);
        if (first_vtx == 0) first_vtx = a;
        body_loads.push_back(a);
    }
    if (first_vtx == 0) return false;
    {   // Research (BLOCKPLANET_MODELS3D_LOG): the game's body's extent, once a kind.
        static const bool log = std::getenv("BLOCKPLANET_MODELS3D_LOG") != nullptr;
        static bool logged[last_kind + 1] = {};
        if (log && !logged[kind]) {
            logged[kind] = true;
            int lo[3] = {32767, 32767, 32767}, hi[3] = {-32768, -32768, -32768};
            double sum[3] = {}, n = 0;
            for (const std::uint32_t load : body_loads) {
                const std::uint32_t w0 = load32(rdram, load), src = 0x80000000u | (load32(rdram, load + 4u) & 0x7FFFFFu);
                const std::uint32_t count = ((w0 >> 20) & 0xFu) + 1u;
                for (std::uint32_t i = 0; i < count; ++i)
                    for (int ax = 0; ax < 3; ++ax) {
                        const int v = load16(rdram, src + 16u * i + 2u * std::uint32_t(ax));
                        lo[ax] = std::min(lo[ax], v), hi[ax] = std::max(hi[ax], v), sum[ax] += v;
                    }
                n += count;
            }
            std::fprintf(stdout, "{\"event\":\"model3d_extent\",\"kind\":%d,\"lo\":[%d,%d,%d],\"hi\":[%d,%d,%d],\"mean\":[%.0f,%.0f,%.0f]}\n", kind,
                         lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], sum[0] / n, sum[1] / n, sum[2] / n);
            std::fflush(stdout);
        }
    }
    const std::uint32_t vtx_source = load32(rdram, first_vtx + 4u);
    if (source != nullptr) *source = vtx_source;
    // In flight (not held), a missile's flame flickers: a new frame of it
    // every 70 ms.
    const bool missile_kind = draw_kind == 0 || draw_kind == 4 || draw_kind == dive_kind || draw_kind == barrage_missile_kind;
    const int phase = (!held && missile_kind) ? flame_frame() : -1;
    const std::uint32_t list = model_list(rdram, draw_kind, level, light_dir, phase);
    if (list == 0) return false;
    // The game's body blanked (each load and the triangles drawn from it, up
    // to the next load); a call to the model in place of the first load.
    for (const std::uint32_t load : body_loads) {
        store32(rdram, load, 0), store32(rdram, load + 4u, 0);
        for (std::uint32_t a = load + 8u; a < end; a += 8u) {
            const std::uint32_t op = load32(rdram, a) >> 24;
            if (op == 0x04u || op == 0x06u || op == 0xB8u || op == 0x01u) break;
            if (op == 0xBFu || op == 0xB1u || op == 0xB5u) store32(rdram, a, 0), store32(rdram, a + 4u, 0);
        }
    }
    // Turned about its own x axis: a short list that multiplies a rotation
    // onto the game's matrix, calls the model and pops it (one per draw, in
    // a ring of slots so a frame still being drawn keeps its own).
    std::uint32_t call = list;
    if (std::abs(turn_degrees) > 0.01 || std::abs(roll_degrees) > 0.01) {
        static std::uint32_t slot = 0;
        constexpr std::uint32_t turn_memory = 0x80770000u, slot_bytes = 0x60u, slots = 64u;
        const std::uint32_t at = turn_memory + (slot++ % slots) * slot_bytes;
        const double a = turn_degrees * 3.14159265358979 / 180.0, c = std::cos(a), sn = std::sin(a);
        const double b = roll_degrees * 3.14159265358979 / 180.0, cb = std::cos(b), sb = std::sin(b);
        // Row vectors: the roll about the model's z first (x' = (cb, sb, 0),
        // y' = (-sb, cb, 0)), then the turn about x (y' = (0, c, s), z' = (0, -s, c)).
        const double rz[3][3] = {{cb, sb, 0}, {-sb, cb, 0}, {0, 0, 1}}, rx[3][3] = {{1, 0, 0}, {0, c, sn}, {0, -sn, c}};
        double m[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) m[i][j] += rz[i][k] * rx[k][j];
        const std::uint32_t mtx = at + 0x20u;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                const std::int32_t fixed = std::int32_t(std::lround(m[i][j] * 65536.0));
                const std::uint32_t k = std::uint32_t(i * 4 + j);
                store16(rdram, mtx + 2u * k, std::uint16_t(fixed >> 16));
                store16(rdram, mtx + 32u + 2u * k, std::uint16_t(fixed & 0xFFFF));
            }
        store32(rdram, at, 0x01040040u), store32(rdram, at + 4u, mtx);      // G_MTX: modelview, multiply, push
        store32(rdram, at + 8u, 0x06000000u), store32(rdram, at + 12u, list);
        store32(rdram, at + 16u, 0xBD000000u), store32(rdram, at + 20u, 0);  // G_POPMTX
        store32(rdram, at + 24u, 0xB8000000u), store32(rdram, at + 28u, 0);  // end
        call = at;
    }
    // The Magnet's sparks, buzzing (a new zigzag every 70 ms), after it.
    if (draw_kind == 2 && (!levels_on() || level == 1)) {
        const std::uint32_t sparks = model_list(rdram, magnet_sparks_kind, 1, light_dir, flame_frame());
        if (sparks != 0) {
            static std::uint32_t slot = 0;
            constexpr std::uint32_t spark_memory = 0x80779000u, slot_bytes = 0x20u, slots = 32u;
            const std::uint32_t at = spark_memory + (slot++ % slots) * slot_bytes;
            store32(rdram, at, 0x06000000u), store32(rdram, at + 4u, call);
            store32(rdram, at + 8u, 0x06000000u), store32(rdram, at + 12u, sparks);
            store32(rdram, at + 16u, 0xB8000000u), store32(rdram, at + 20u, 0);
            call = at;
        }
    }
    // The Firecracker in flight: the game's smoke rings (flat pictures, each a
    // load of four vertices after the rocket, its flame fifteen) hidden, and
    // ours left behind it (the owner, 2026-11-26).
    if (draw_kind == 0 && !held && !whole) {
        for (std::uint32_t a = start; a < end; a += 8u) {
            const std::uint32_t w0 = load32(rdram, a);
            if ((w0 >> 24) != 0x04u || ((w0 >> 20) & 0xFu) + 1u != 4u) continue;
            if (std::find(body_loads.begin(), body_loads.end(), a) != body_loads.end()) continue;
            store32(rdram, a, 0), store32(rdram, a + 4u, 0);
            for (std::uint32_t b = a + 8u; b < end; b += 8u) {
                const std::uint32_t op = load32(rdram, b) >> 24;
                if (op == 0x04u || op == 0x06u || op == 0xB8u || op == 0x01u) break;
                if (op == 0xBFu || op == 0xB1u || op == 0xB5u) store32(rdram, b, 0), store32(rdram, b + 4u, 0);
            }
        }
        if (const std::uint32_t with_rings = smoke_rings(rdram, start, call, light_dir)) call = with_rings;
    }
    // The Atom: one electron on each ring, going round it (the owner,
    // 2026-11-26), each a call to the electron through a matrix that moves it
    // to its place this frame (in a ring of slots, as the turn above).
    if (draw_kind == 3) {
        const std::uint32_t electron_list = model_list(rdram, electron_kind, level, light_dir, -1);
        if (electron_list != 0) {
            static std::uint32_t slot = 0;
            constexpr std::uint32_t electron_memory = 0x80772000u, slot_bytes = 0x200u, slots = 32u;
            const std::uint32_t at = electron_memory + (slot++ % slots) * slot_bytes;
            const int atom_level = levels_on() ? level : 1;  // (the original Atom is level 1's)
            const int orbits = atom_orbits(atom_level);
            const float scale = atom_level == 3 ? 0.85f : 1.0f;  // (level 3's atom is drawn smaller in its ring)
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            constexpr double lap = 1.6;  // seconds round a ring
            std::uint32_t cmd = at, mtx = at + 0x80u;
            store32(rdram, cmd, 0x06000000u), store32(rdram, cmd + 4u, call), cmd += 8u;
            for (int o = 0; o < orbits; ++o) {
                // Spread round their rings, alternate rings the other way round.
                const double turn = std::fmod(seconds / lap + double(o) / double(orbits), 1.0) * (o % 2 ? -1.0 : 1.0);
                const float a = float(6.283185307179586 * turn);
                const V3 p = atom_ring(o, atom_level).apply(V3{atom_orbit * std::cos(a), atom_orbit * std::sin(a), 0}) * scale;
                const double m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {p.x, p.y, p.z, 1}};  // row vectors: a move
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j) {
                        const std::int32_t fixed = std::int32_t(std::lround(m[i][j] * 65536.0));
                        const std::uint32_t k = std::uint32_t(i * 4 + j);
                        store16(rdram, mtx + 2u * k, std::uint16_t(fixed >> 16));
                        store16(rdram, mtx + 32u + 2u * k, std::uint16_t(fixed & 0xFFFF));
                    }
                store32(rdram, cmd, 0x01040040u), store32(rdram, cmd + 4u, mtx);  // G_MTX: modelview, multiply, push
                store32(rdram, cmd + 8u, 0x06000000u), store32(rdram, cmd + 12u, electron_list);
                store32(rdram, cmd + 16u, 0xBD000000u), store32(rdram, cmd + 20u, 0);  // G_POPMTX
                cmd += 24u, mtx += 64u;
            }
            store32(rdram, cmd, 0xB8000000u), store32(rdram, cmd + 4u, 0);
            call = at;
        }
    }
    store32(rdram, first_vtx, 0x06000000u), store32(rdram, first_vtx + 4u, call);
    return true;
}


// Whether the power-up's nose (-z, our models' front) points back at the
// camera, from the draw's matrices (the last modelview load before the
// geometry, then the multiplies after it): the Bomb turns over late in its
// dive (measured 2026-11-22: the nose swings from up, to into the screen,
// to the camera), so ours would go into the sphere tail first. Kept as it
// was while side-on.
namespace {
using Mat = std::array<std::array<double, 4>, 4>;
Mat read_matrix(const std::uint8_t* rdram, std::uint32_t a) {
    a = 0x80000000u | (a & 0x7FFFFFu);
    Mat m{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const int k = i * 4 + j;
            const std::int32_t whole = load16(rdram, a + std::uint32_t(2 * k));
            const std::uint32_t fraction = static_cast<std::uint16_t>(load16(rdram, a + 32u + std::uint32_t(2 * k)));
            m[i][j] = double((whole << 16) | std::int32_t(fraction)) / 65536.0;
        }
    return m;
}
Mat multiply(const Mat& a, const Mat& b) {
    Mat r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r[i][j] += a[i][k] * b[k][j];
    return r;
}

// The Firecracker's smoke rings (the owner, 2026-11-26: the game's came a
// frame or two after the rocket had passed and always faced the camera). One
// is left at the rocket's tail as it moves on, square to the rocket, fixed in
// the world; each grows to 1.8 times its size and thins over 0.9 s.
// The draw's matrices give the camera (the last load) and the rocket's place
// (the multiplies after it); each ring is drawn through its own place times
// the camera, loaded and popped, after the rocket. Returns a list calling
// `call` and then the rings, or 0.
std::uint32_t smoke_rings(std::uint8_t* rdram, std::uint32_t start, std::uint32_t call, std::uint32_t light_dir) {
    const std::uint32_t end = load32(rdram, display_list_pointer);
    Mat view{}, place{};
    for (int i = 0; i < 4; ++i) view[i][i] = place[i][i] = 1;
    bool have_view = false;
    for (std::uint32_t a = start; a < end; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if ((w0 >> 24) == 0x04u || (w0 >> 24) == 0x06u) break;
        if ((w0 >> 24) != 0x01u) continue;
        const std::uint32_t params = (w0 >> 16) & 0xFFu;
        if (params & 1u) continue;  // projection
        const Mat t = read_matrix(rdram, w1);
        if (params & 2u) {
            view = t, have_view = true;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) place[i][j] = i == j;
        } else {
            place = multiply(t, place);
        }
    }
    if (!have_view) return 0;
    struct Ring { double at[3], n[3], size; std::chrono::steady_clock::time_point born; };
    static std::vector<Ring> rings;
    static std::chrono::steady_clock::time_point last_drawn{}, last_left{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_drawn > std::chrono::milliseconds(500)) rings.clear(), last_left = {};  // a new flight
    last_drawn = now;
    constexpr double life = 0.9;
    rings.erase(std::remove_if(rings.begin(), rings.end(),
                               [&](const Ring& r) { return std::chrono::duration<double>(now - r.born).count() > life; }),
                rings.end());
    // The rocket's tail (z +300, between its body and fuse) and its axis.
    const double tail[4] = {0, 0, 300, 1};
    double at[3] = {}, n[3] = {place[2][0], place[2][1], place[2][2]};
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 4; ++k) at[j] += tail[k] * place[k][j];
    const double scale = std::sqrt(place[0][0] * place[0][0] + place[0][1] * place[0][1] + place[0][2] * place[0][2]);
    const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl <= 0 || scale <= 0) return 0;
    for (double& c : n) c /= nl;
    // A new ring once the rocket has moved on a ring's width from the last
    // (as it sets off it hangs near the camera: rings piled up there, huge).
    static double last_at[3] = {};
    const double moved = std::sqrt((at[0] - last_at[0]) * (at[0] - last_at[0]) + (at[1] - last_at[1]) * (at[1] - last_at[1]) +
                                   (at[2] - last_at[2]) * (at[2] - last_at[2]));
    const bool first = last_left == std::chrono::steady_clock::time_point{};
    if (rings.size() < 16 && now - last_left >= std::chrono::milliseconds(60) && (first || moved >= scale * 1600.0)) {
        rings.push_back(Ring{{at[0], at[1], at[2]}, {n[0], n[1], n[2]}, scale * 0.75, now});
        last_left = now, last_at[0] = at[0], last_at[1] = at[1], last_at[2] = at[2];
    }
    static std::uint32_t slot = 0;
    constexpr std::uint32_t ring_memory_at = 0x80776000u, slot_bytes = 0x600u, slots = 8u;
    const std::uint32_t at_list = ring_memory_at + (slot++ % slots) * slot_bytes;
    std::uint32_t cmd = at_list, mtx = at_list + 0x200u;
    store32(rdram, cmd, 0x06000000u), store32(rdram, cmd + 4u, call), cmd += 8u;
    for (const Ring& r : rings) {
        const double age = std::chrono::duration<double>(now - r.born).count() / life;
        const std::uint32_t list = model_list(rdram, smoke_ring_kind, 1, light_dir, std::min(3, int(age * 4.0)));
        if (list == 0) continue;
        // Square to the rocket: the ring's z along its axis.
        const double* z = r.n;
        double x[3] = {z[1], -z[0], 0};
        if (std::abs(z[2]) > 0.9) x[0] = 0, x[1] = z[2], x[2] = -z[1];
        const double xl = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        for (double& c : x) c /= xl;
        const double y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
        const double s = r.size * (1.0 + 0.8 * age);
        Mat ring_place{};
        for (int j = 0; j < 3; ++j) ring_place[0][j] = x[j] * s, ring_place[1][j] = y[j] * s, ring_place[2][j] = z[j] * s, ring_place[3][j] = r.at[j];
        ring_place[3][3] = 1;
        const Mat m = multiply(ring_place, view);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                const std::int32_t fixed = std::int32_t(std::lround(std::clamp(m[i][j], -32767.0, 32767.0) * 65536.0));
                const std::uint32_t k = std::uint32_t(i * 4 + j);
                store16(rdram, mtx + 2u * k, std::uint16_t(fixed >> 16));
                store16(rdram, mtx + 32u + 2u * k, std::uint16_t(fixed & 0xFFFF));
            }
        store32(rdram, cmd, 0x01060040u), store32(rdram, cmd + 4u, mtx);  // G_MTX: modelview, load, push
        store32(rdram, cmd + 8u, 0x06000000u), store32(rdram, cmd + 12u, list);
        store32(rdram, cmd + 16u, 0xBD000000u), store32(rdram, cmd + 20u, 0);  // G_POPMTX
        cmd += 24u, mtx += 64u;
    }
    store32(rdram, cmd, 0xB8000000u), store32(rdram, cmd + 4u, 0);
    return at_list;
}
}  // namespace

double nose_towards_camera(const std::uint8_t* rdram, std::uint32_t start) {
    const std::uint32_t end = load32(rdram, display_list_pointer);
    if (end <= start || end - start > 0x10000u) return -2.0;
    Mat m{};
    for (int i = 0; i < 4; ++i) m[i][i] = 1;
    bool any = false;
    for (std::uint32_t a = start; a < end; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if ((w0 >> 24) == 0x04u) break;
        if ((w0 >> 24) != 0x01u) continue;
        const std::uint32_t params = (w0 >> 16) & 0xFFu;
        if (params & 1u) continue;
        const Mat t = read_matrix(rdram, w1);
        m = (params & 2u) ? t : multiply(t, m);
        any = true;
    }
    if (!any) return -2.0;
    const double nose[3] = {-m[2][0], -m[2][1], -m[2][2]};
    const double l = std::sqrt(nose[0] * nose[0] + nose[1] * nose[1] + nose[2] * nose[2]);
    return l > 0 ? nose[2] / l : -2.0;
}

double roll_handle_down(const std::uint8_t* rdram, std::uint32_t start) {
    const std::uint32_t end = load32(rdram, display_list_pointer);
    if (end <= start || end - start > 0x10000u) return 0.0;
    Mat m{};
    for (int i = 0; i < 4; ++i) m[i][i] = 1;
    bool any = false;
    for (std::uint32_t a = start; a < end; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if ((w0 >> 24) == 0x04u) break;
        if ((w0 >> 24) != 0x01u) continue;
        const std::uint32_t params = (w0 >> 16) & 0xFFu;
        if (params & 1u) continue;
        const Mat t = read_matrix(rdram, w1);
        m = (params & 2u) ? t : multiply(t, m);
        any = true;
    }
    if (!any) return 0.0;
    // The screen's down (eye -y) in the model's x and y (its rows are the
    // model's axes in eye space); the roll that turns the handle (model -y) to it.
    const double lx = std::sqrt(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]);
    const double ly = std::sqrt(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]);
    if (lx <= 0 || ly <= 0) return 0.0;
    const double dx = -m[0][1] / lx, dy = -m[1][1] / ly;
    if (std::abs(dx) + std::abs(dy) < 1e-3) return 0.0;  // looking straight along the barrel
    return std::atan2(dy, dx) * 180.0 / 3.14159265358979 + 90.0;
}

bool nose_backwards(const std::uint8_t* rdram, std::uint32_t start, bool restart) {
    static bool backwards = false, have = false;
    if (restart) backwards = false, have = false;
    const std::uint32_t end = load32(rdram, display_list_pointer);
    if (end <= start || end - start > 0x10000u) return backwards;
    Mat m{};
    for (int i = 0; i < 4; ++i) m[i][i] = 1;
    bool any = false;
    for (std::uint32_t a = start; a < end; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if ((w0 >> 24) == 0x04u) break;
        if ((w0 >> 24) != 0x01u) continue;
        const std::uint32_t params = (w0 >> 16) & 0xFFu;
        if (params & 1u) continue;  // projection
        const Mat t = read_matrix(rdram, w1);
        m = (params & 2u) ? t : multiply(t, m);  // a load, or a multiply (row vectors: new x current)
        any = true;
    }
    if (!any) return backwards;
    // The nose in eye space against its direction at the flight's start:
    // turned round once the game has turned it more than halfway.
    static double first[3] = {};
    double nose[3] = {-m[2][0], -m[2][1], -m[2][2]};
    const double nl = std::sqrt(nose[0] * nose[0] + nose[1] * nose[1] + nose[2] * nose[2]);
    if (nl <= 0) return backwards;
    for (double& c : nose) c /= nl;
    if (!have) {
        first[0] = nose[0], first[1] = nose[1], first[2] = nose[2], have = true;
        return backwards = false;
    }
    {   // Research (BLOCKPLANET_MODELS3D_LOG): the nose and the origin each draw.
        static const bool log = std::getenv("BLOCKPLANET_MODELS3D_LOG") != nullptr;
        if (log) {
            std::fprintf(stdout, "{\"event\":\"nose\",\"nose\":[%.2f,%.2f,%.2f],\"at\":[%.0f,%.0f,%.0f]}\n", nose[0], nose[1], nose[2], m[3][0], m[3][1], m[3][2]);
            std::fflush(stdout);
        }
    }
    // Facing the camera (eye +z) means flying tail first into the sphere.
    (void)first;
    if (nose[2] > 0.3) backwards = true;
    else if (nose[2] < -0.3) backwards = false;
    return backwards;
}

bool hide(std::uint8_t* rdram, std::uint32_t start) {
    const std::uint32_t end = load32(rdram, display_list_pointer);
    if (rdram == nullptr || end <= start || end - start > 0x10000u) return false;
    bool any = false;
    for (std::uint32_t a = start; a < end; a += 8u) {
        const std::uint32_t op = load32(rdram, a) >> 24;
        if (op == 0x04u || op == 0xBFu || op == 0xB1u || op == 0xB5u) store32(rdram, a, 0), store32(rdram, a + 4u, 0), any = true;
    }
    return any;
}

}  // namespace tetrisphere::powerup_models
