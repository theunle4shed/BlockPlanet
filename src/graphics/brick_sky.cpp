// The brick sky: an experiment for the owner's design (2026-11-10). See
// include/tetrisphere/brick_sky.h and
// docs/research/blockplanet-play-backgrounds.md (Brick skybox).
//
// Drawn in the game's play background slot (the background scene's draw
// through 0x8002D93C from the main loop), so it lands in the game's
// background layer, and the sphere, pieces and magic are drawn over it as
// before. The sky's own commands live in BlockPlanet's memory and are called
// from the game's display list with one G_DL. F3D commands as the title's
// ring uses them (src/graphics/title_hooks.cpp).

#include "tetrisphere/brick_sky.h"

#include "tetrisphere/guest_memory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace tetrisphere::brick_sky {
namespace {

using namespace tetrisphere::guest;

constexpr float pi = 3.14159265358979f;
constexpr std::uint32_t display_list_pointer = 0x800F22B4u;  // the game's next free command
constexpr std::uint32_t background_scene = 0x801033BCu;
constexpr std::uint32_t player_context = 0x80110220u;
constexpr std::uint32_t sphere_angle_x = player_context + 0x2B42u;  // 8192 a turn, left and right
constexpr std::uint32_t sphere_angle_y = player_context + 0x2B46u;  // up and down

constexpr std::uint32_t projection_matrix = memory, sky_matrix = memory + 0x40u;
constexpr std::uint32_t flat_matrix = memory + 0x80u, identity_matrix = memory + 0xC0u;
constexpr std::uint32_t commands_start = memory + 0x100u, commands_end = memory + 0xB000u;
constexpr std::uint32_t vertices_start = commands_end;

bool apply(Settings& s, const std::string& name, const std::string& value);

// BLOCKPLANET_BRICK_SKY=1 turns the sky on; BLOCKPLANET_BRICK_SKY_SET=
// "sky_spin=wild,sky_comfort=none" sets others, for recordings.
Settings from_environment() {
    Settings s;
    const char* on = std::getenv("BLOCKPLANET_BRICK_SKY");
    s.on = on != nullptr && std::string(on) != "0";
    if (const char* list = std::getenv("BLOCKPLANET_BRICK_SKY_SET")) {
        const std::string text(list);
        std::size_t at = 0;
        while (at < text.size()) {
            const auto comma = std::min(text.find(',', at), text.size());
            const auto item = text.substr(at, comma - at);
            const auto equals = item.find('=');
            if (equals != std::string::npos) apply(s, item.substr(0, equals), item.substr(equals + 1));
            at = comma + 1;
        }
    }
    return s;
}

std::mutex mutex;
Settings current = from_environment();
std::atomic<float> picture_aspect{4.0f / 3.0f};

using Colour = std::array<float, 3>;
std::vector<Colour> palette(const std::string& name, const std::vector<int>& stage_types) {
    if (name == "stage") return stage_contrast(stage_types);
    if (name == "teal") return {{0.15f, 0.62f, 0.60f}, {0.18f, 0.44f, 0.72f}, {0.10f, 0.50f, 0.45f}};
    if (name == "amber") return {{0.88f, 0.56f, 0.16f}, {0.80f, 0.34f, 0.14f}, {0.70f, 0.48f, 0.20f}};
    if (name == "rose") return {{0.82f, 0.30f, 0.52f}, {0.60f, 0.26f, 0.72f}, {0.75f, 0.38f, 0.40f}};
    if (name == "grey") return {{0.56f, 0.59f, 0.63f}, {0.40f, 0.43f, 0.48f}, {0.66f, 0.66f, 0.64f}};
    return {{0.46f, 0.26f, 0.78f}, {0.24f, 0.34f, 0.82f}, {0.36f, 0.20f, 0.60f}};  // violet
}

std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

std::uint8_t channel(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// An N64 fixed-point matrix from a row-vector 4 x 4 (v * M).
void put_matrix(std::uint8_t* rdram, std::uint32_t address, const float (&m)[4][4]) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const auto fixed = static_cast<std::int32_t>(std::lround(m[i][j] * 65536.0f));
            store16(rdram, address + 2u * std::uint32_t(4 * i + j), static_cast<std::int16_t>((fixed >> 16) & 0xFFFF));
            store16(rdram, address + 32u + 2u * std::uint32_t(4 * i + j), static_cast<std::int16_t>(fixed & 0xFFFF));
        }
}

void put_vertex(std::uint8_t* rdram, std::uint32_t at, const Vertex& v) {
    store16(rdram, at + 0, v.x);
    store16(rdram, at + 2, v.y);
    store16(rdram, at + 4, v.z);
    store16(rdram, at + 6, 0);
    store16(rdram, at + 8, 0);
    store16(rdram, at + 10, 0);
    store8(rdram, at + 12, v.r);
    store8(rdram, at + 13, v.g);
    store8(rdram, at + 14, v.b);
    store8(rdram, at + 15, v.a);
}

float wrap(float a) { return a - 2.0f * pi * std::floor((a + pi) / (2.0f * pi)); }

} // namespace

Settings settings() {
    std::lock_guard lock(mutex);
    return current;
}

float strength(const std::string& spin) {
    if (spin == "gentle") return 0.5f;
    if (spin == "normal") return 1.5f;
    if (spin == "wild") return 3.0f;
    return 0.0f;  // still, drift
}

bool set(const std::string& name, const std::string& value) {
    std::lock_guard lock(mutex);
    return apply(current, name, value);
}

namespace {
bool apply(Settings& s, const std::string& name, const std::string& value) {
    if (name == "sky_on") s.on = value == "on";
    else if (name == "sky_layout") {
        if (value == "sphere" || value == "cube") s.layout = value;
    }
    else if (name == "sky_spin") {
        if (value == "still" || value == "drift" || value == "gentle" || value == "normal" || value == "wild") s.spin = value;
    } else if (name == "sky_direction") s.against = value != "with";
    else if (name == "sky_fov") s.fov_degrees = std::clamp(float(std::atof(value.c_str())), 30.0f, 130.0f);
    else if (name == "sky_comfort") {
        if (value == "none" || value == "frame" || value == "horizon" || value == "both") s.comfort = value;
    } else if (name == "sky_colours") {
        if (value == "stage" || value == "violet" || value == "teal" || value == "amber" || value == "rose" || value == "grey") s.colours = value;
    }
    else if (name == "sky_brightness") s.brightness = std::clamp(float(std::atof(value.c_str())) / 100.0f, 0.3f, 1.0f);
    else return false;
    return true;
}
} // namespace

std::vector<std::pair<std::string, std::string>> values() {
    const Settings s = settings();
    return {{"sky_on", s.on ? "on" : "off"},
            {"sky_layout", s.layout},
            {"sky_spin", s.spin},
            {"sky_direction", s.against ? "against" : "with"},
            {"sky_fov", std::to_string(int(std::lround(s.fov_degrees)))},
            {"sky_comfort", s.comfort},
            {"sky_colours", s.colours},
            {"sky_brightness", std::to_string(int(std::lround(s.brightness * 100.0f)))}};
}

void Motion::step(std::int16_t sphere_x, std::int16_t sphere_y, float seconds, const Settings& s) {
    if (!started) {
        started = true;
        last_x = sphere_x;
        last_y = sphere_y;
        return;
    }
    const float dt = std::clamp(seconds, 0.0f, 0.1f);
    const int dx = angle_step(last_x, sphere_x), dy = angle_step(last_y, sphere_y);
    last_x = sphere_x;
    last_y = sphere_y;
    if (dt <= 0.0f) return;
    const float turn = 2.0f * pi / 8192.0f;
    const float k = strength(s.spin) * (s.against ? -1.0f : 1.0f);
    const bool still = s.spin == "still";
    const float target_yaw = k * float(dx) * turn / dt + (still ? 0.0f : 0.05f);
    const float target_pitch = k * float(dy) * turn / dt + (still ? 0.0f : 0.015f);
    // Ease towards the target (about a quarter of a second), so it never snaps.
    const float a = 1.0f - std::exp(-dt / 0.25f);
    yaw_speed += (target_yaw - yaw_speed) * a;
    pitch_speed += (target_pitch - pitch_speed) * a;
    const float speed = std::hypot(yaw_speed, pitch_speed);
    if (speed > max_speed) {
        yaw_speed *= max_speed / speed;
        pitch_speed *= max_speed / speed;
    }
    yaw = wrap(yaw + yaw_speed * dt);
    pitch = wrap(pitch + pitch_speed * dt);
}

std::size_t Mesh::triangles() const {
    std::size_t n = 0;
    for (const auto bits : bevels) n += 2 + 2 * std::size_t(((bits >> 0) & 1) + ((bits >> 1) & 1) + ((bits >> 2) & 1) + ((bits >> 3) & 1));
    return n;
}

namespace {
constexpr float inset = 0.12f;  // the bevel's width, in a cell

// Adds one cell: its outer corners (mortar), then its face's corners, inset
// on its bevelled sides. `at(u, v)` places a point given in cell units;
// `light(v)` is the face's light at a height in the cell (0 bottom, 1 top).
template <typename At, typename Light>
void add_cell(Mesh& mesh, const At& at, const Light& light, const Colour& base, float shade, std::uint8_t bevels) {
    static const std::array<std::array<float, 2>, 4> corners{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (int ring = 0; ring < 2; ++ring)
        for (int c = 0; c < 4; ++c) {
            const float cu = corners[std::size_t(c)][0], cv = corners[std::size_t(c)][1];
            float u = cu, v = cv;
            if (ring == 1) {
                if (cu == 0 && (bevels & 8)) u = inset;
                if (cu == 1 && (bevels & 2)) u = 1 - inset;
                if (cv == 0 && (bevels & 1)) v = inset;
                if (cv == 1 && (bevels & 4)) v = 1 - inset;
            }
            const auto p = at(u, v);
            // The outer corners are mortar where a side is a brick's edge;
            // inside a brick they carry the face's light.
            const float l = ring == 0 && bevels != 0 ? 0.28f : light(v);
            mesh.vertices.push_back({static_cast<std::int16_t>(std::lround(p[0])), static_cast<std::int16_t>(std::lround(p[1])),
                                     static_cast<std::int16_t>(std::lround(p[2])), channel(base[0] * shade * l),
                                     channel(base[1] * shade * l), channel(base[2] * shade * l), 0xFF});
        }
    mesh.bevels.push_back(bevels);
}

std::uint32_t next_random(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return hash(state);
}
} // namespace

Mesh cube_mesh(const Settings& s, int per_face, float radius) {
    struct Face { std::array<float, 3> n, u, v; };
    static const std::array<Face, 6> faces{{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};
    const auto colours = palette(s.colours, s.stage_types);
    const float step = (pi / 2.0f) / float(per_face);
    Mesh out;
    for (int f = 0; f < 6; ++f) {
        const Face& face = faces[std::size_t(f)];
        for (int i = 0; i < per_face; ++i)
            for (int j = 0; j < per_face; ++j) {
                const auto at = [&](float u, float v) {
                    // Equal angles across the face (as the menu grid's layout).
                    const float ta = std::tan(-pi / 4.0f + (float(i) + u) * step), tb = std::tan(-pi / 4.0f + (float(j) + v) * step);
                    std::array<float, 3> d{};
                    for (int c = 0; c < 3; ++c) d[std::size_t(c)] = face.n[std::size_t(c)] + ta * face.u[std::size_t(c)] + tb * face.v[std::size_t(c)];
                    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    return std::array<float, 3>{d[0] / l * radius, d[1] / l * radius, d[2] / l * radius};
                };
                const Colour& base = colours[hash(std::uint32_t(f * 4099 + i * 131 + j)) % colours.size()];
                const float shade = s.brightness * (0.85f + 0.3f * float(hash(std::uint32_t(f * 7919 + i * 31 + j * 17)) % 100) / 100.0f);
                add_cell(out, at, [](float v) { return v > 0.5f ? 1.12f : 0.82f; }, base, shade, 0xF);
            }
    }
    return out;
}

std::vector<int> sphere_bricks() {
    // The game's pieces are one to four cells; these shapes (each with its
    // turns) fill the grid at random, as a board's layers look. Columns
    // wrap; rows do not.
    using Shape = std::vector<std::array<int, 2>>;
    static const std::vector<Shape> shapes = [] {
        const std::vector<Shape> base{
            {{0, 0}, {1, 0}, {2, 0}, {3, 0}},  // I
            {{0, 0}, {1, 0}, {0, 1}, {1, 1}},  // O
            {{0, 0}, {1, 0}, {2, 0}, {1, 1}},  // T
            {{0, 0}, {1, 0}, {2, 0}, {2, 1}},  // L
            {{0, 0}, {1, 0}, {1, 1}, {2, 1}},  // S
            {{0, 0}, {1, 0}, {2, 0}},          // three in a line
            {{0, 0}, {1, 0}, {1, 1}},          // a corner
            {{0, 0}, {1, 0}},                  // two
        };
        std::vector<Shape> all;
        for (const auto& shape : base)
            for (int turn = 0; turn < 4; ++turn) {
                Shape t;
                for (const auto& [x, y] : shape) {
                    int a = x, b = y;
                    for (int k = 0; k < turn; ++k) {
                        const int c = a;
                        a = -b;
                        b = c;
                    }
                    t.push_back({a, b});
                }
                // Anchored at its first cell in reading order (the fill's next free cell).
                std::sort(t.begin(), t.end(), [](const auto& p, const auto& q) { return p[1] != q[1] ? p[1] < q[1] : p[0] < q[0]; });
                const auto origin = t.front();
                for (auto& p : t) p = {p[0] - origin[0], p[1] - origin[1]};
                if (std::find(all.begin(), all.end(), t) == all.end()) all.push_back(t);
            }
        return all;
    }();
    const auto cell = [](int row, int col) { return std::size_t(row * sphere_columns + ((col % sphere_columns) + sphere_columns) % sphere_columns); };
    std::vector<int> brick(std::size_t(sphere_columns * sphere_rows), -1);
    std::uint32_t random = 0x5EEDu;
    int next = 0;
    for (int row = 0; row < sphere_rows; ++row)
        for (int col = 0; col < sphere_columns; ++col) {
            if (brick[cell(row, col)] >= 0) continue;
            bool placed = false;
            for (int attempt = 0; attempt < 8 && !placed; ++attempt) {
                const auto& shape = shapes[next_random(random) % shapes.size()];
                bool fits = true;
                for (const auto& [dx, dy] : shape) {
                    const int r = row + dy;
                    if (r < 0 || r >= sphere_rows || brick[cell(r, col + dx)] >= 0) {
                        fits = false;
                        break;
                    }
                }
                if (!fits) continue;
                for (const auto& [dx, dy] : shape) brick[cell(row + dy, col + dx)] = next;
                placed = true;
            }
            if (!placed) brick[cell(row, col)] = next;
            ++next;
        }
    return brick;
}

Mesh sphere_mesh(const Settings& s, float radius) {
    const auto colours = palette(s.colours, s.stage_types);
    const float step = 2.0f * pi / float(sphere_columns);   // 11.25 degrees, as the board's cells
    const float south = -float(sphere_rows) / 2.0f * step;  // -78.75 degrees
    // A point at a column and row in cell units; rows below 0 and above the
    // last are the poles.
    const auto point = [&](float col, float row) {
        const float lon = col * step;
        const float lat = row < 0.0f ? -pi / 2 : row > float(sphere_rows) ? pi / 2 : south + row * step;
        return std::array<float, 3>{radius * std::cos(lat) * std::sin(lon), radius * std::sin(lat), radius * std::cos(lat) * std::cos(lon)};
    };
    static const std::vector<int> brick = sphere_bricks();
    const auto id = [&](int row, int col) {
        if (row < 0 || row >= sphere_rows) return -1;
        return brick[std::size_t(row * sphere_columns + ((col % sphere_columns) + sphere_columns) % sphere_columns)];
    };
    // Each brick's rows, so its light runs across the whole brick (lighter towards its top).
    std::vector<std::array<int, 2>> rows(std::size_t(*std::max_element(brick.begin(), brick.end()) + 1), {sphere_rows, -1});
    for (int r = 0; r < sphere_rows; ++r)
        for (int c = 0; c < sphere_columns; ++c) {
            auto& span = rows[std::size_t(id(r, c))];
            span = {std::min(span[0], r), std::max(span[1], r)};
        }
    Mesh out;
    for (int row = 0; row < sphere_rows; ++row)
        for (int col = 0; col < sphere_columns; ++col) {
            const int me = id(row, col);
            const std::uint8_t bevels = std::uint8_t((id(row - 1, col) != me ? 1 : 0) | (id(row, col + 1) != me ? 2 : 0) |
                                                     (id(row + 1, col) != me ? 4 : 0) | (id(row, col - 1) != me ? 8 : 0));
            const Colour& base = colours[hash(std::uint32_t(me) * 2654435761u) % colours.size()];
            const float shade = s.brightness * (0.85f + 0.3f * float(hash(std::uint32_t(me) + 977u) % 100) / 100.0f);
            const auto span = rows[std::size_t(me)];
            const float height = float(span[1] - span[0] + 1);
            add_cell(out, [&](float u, float v) { return point(float(col) + u, float(row) + v); },
                     [&](float v) { return 0.82f + 0.30f * (float(row - span[0]) + v) / height; }, base, shade, bevels);
        }
    // The poles: a dark disc each, 32 thin cells from the last row to the pole.
    const Colour cap = colours.front();
    for (int pole = 0; pole < 2; ++pole)
        for (int col = 0; col < sphere_columns; ++col) {
            const float ring = pole == 0 ? 0.0f : float(sphere_rows);
            const float tip = pole == 0 ? -1.0f : float(sphere_rows) + 1.0f;
            add_cell(out,
                     [&](float u, float v) {
                         const float r = pole == 0 ? (v < 0.5f ? tip : ring) : (v < 0.5f ? ring : tip);
                         return point(float(col) + u, r);
                     },
                     [](float) { return 0.45f; }, cap, s.brightness, 0);
        }
    return out;
}

Mesh mesh(const Settings& s) { return s.layout == "cube" ? cube_mesh(s) : sphere_mesh(s); }

std::uint32_t build(std::uint8_t* rdram, const Settings& s, const Motion& m, std::uint32_t projection, int /*frame*/,
                    float picture_aspect) {
    // Matrices: the sky's perspective (4:3, as the game's frame), its turn,
    // and a flat projection for the comfort layer (the frame's centred units).
    const float f = 1.0f / std::tan(s.fov_degrees * pi / 360.0f), aspect = 4.0f / 3.0f;
    const float near = 50.0f, far = 3000.0f;
    const float p[4][4] = {{f / aspect, 0, 0, 0},
                           {0, f, 0, 0},
                           {0, 0, (far + near) / (near - far), -1},
                           {0, 0, 2.0f * far * near / (near - far), 0}};
    const float cy = std::cos(m.yaw), sy = std::sin(m.yaw), cp = std::cos(m.pitch), sp = std::sin(m.pitch);
    // Row vectors: v * Ry(yaw) * Rx(pitch).
    const float r[4][4] = {{cy, sy * sp, -sy * cp, 0}, {0, cp, sp, 0}, {sy, -cy * sp, cy * cp, 0}, {0, 0, 0, 1}};
    const float flat[4][4] = {{1.0f / 160.0f, 0, 0, 0}, {0, 1.0f / 120.0f, 0, 0}, {0, 0, 1.0f / 512.0f, 0}, {0, 0, 0, 1}};
    const float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    put_matrix(rdram, projection_matrix, p);
    put_matrix(rdram, sky_matrix, r);
    put_matrix(rdram, flat_matrix, flat);
    put_matrix(rdram, identity_matrix, id);

    const Mesh sky = mesh(s);
    std::vector<Vertex> comfort;
    const bool frame = s.comfort == "frame" || s.comfort == "both", horizon = s.comfort == "horizon" || s.comfort == "both";
    // The flat projection is the 4:3 frame's units (x 160 at its edge); an
    // expanded picture shows more on each side, so the comfort layer reaches
    // the picture's own edges (seen as seams at the 4:3 edges otherwise).
    const int half_w = int(std::lround(120.0f * std::clamp(picture_aspect, 4.0f / 3.0f, 4.0f)));
    const int inner_w = half_w - 42;
    if (frame) {
        // A still, dark frame at the edges: opaque-ish at the edge, clear inside.
        for (const auto [x, y, a] : std::array<std::array<int, 3>, 8>{{{-half_w, 120, 210}, {half_w, 120, 210}, {half_w, -120, 210}, {-half_w, -120, 210},
                                                                     {-inner_w, 84, 0}, {inner_w, 84, 0}, {inner_w, -84, 0}, {-inner_w, -84, 0}}})
            comfort.push_back({std::int16_t(x), std::int16_t(y), 0, 0, 0, 0, std::uint8_t(a)});
    }
    if (horizon) {
        // A still, faint line across the middle (it shows either side of the sphere).
        for (const auto [x, y, a] : std::array<std::array<int, 3>, 4>{{{-half_w, 2, 60}, {half_w, 2, 60}, {half_w, -2, 60}, {-half_w, -2, 60}}})
            comfort.push_back({std::int16_t(x), std::int16_t(y), 0, 210, 220, 240, std::uint8_t(a)});
    }
    const std::uint32_t vertex_bytes = std::uint32_t((sky.vertices.size() + comfort.size()) * 16u);
    if (vertices_start + vertex_bytes > memory_end) return 0;
    for (std::size_t k = 0; k < sky.vertices.size(); ++k) put_vertex(rdram, vertices_start + 16u * std::uint32_t(k), sky.vertices[k]);
    const std::uint32_t comfort_start = vertices_start + 16u * std::uint32_t(sky.vertices.size());
    for (std::size_t k = 0; k < comfort.size(); ++k) put_vertex(rdram, comfort_start + 16u * std::uint32_t(k), comfort[k]);

    std::uint32_t dl = commands_start;
    bool full = false;
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
        if (dl + 8u > commands_end - 8u) { full = true; return; }
        store32(rdram, dl, w0);
        store32(rdram, dl + 4u, w1);
        dl += 8u;
    };
    const auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        cmd(0xBF000000u, (a * 10u) << 16 | (b * 10u) << 8 | (c * 10u));
    };
    cmd(0xE7000000u, 0);              // pipe sync
    cmd(0xBA001402u, 0x00000000u);    // one-cycle
    cmd(0xB900031Du, 0x0F0A4000u);    // opaque surface, no z
    cmd(0xFCFFFFFFu, 0xFFFE793Cu);    // combine: shade colour and alpha
    cmd(0xBB000000u, 0x00000000u);    // texture off
    cmd(0xB6000000u, 0xFFFFFFFFu);    // clear geometry modes (no lighting, no culling)
    cmd(0xB7000000u, 0x00000204u);    // shade, smooth
    cmd(0x01030040u, projection_matrix);  // projection: load
    cmd(0x01060040u, sky_matrix);         // modelview: load, push
    // Two bricks a vertex load: 16 vertices, 20 triangles. From the centre
    // of a sphere every direction meets it once, so no depth is needed.
    const std::uint32_t cell_count = std::uint32_t(sky.cells());
    for (std::uint32_t b = 0; b < cell_count; b += 2) {
        const std::uint32_t n = std::min<std::uint32_t>(2, cell_count - b);
        cmd(0x04000000u | ((n * 8u - 1u) << 20) | (n * 8u * 16u), vertices_start + b * 8u * 16u);
        for (std::uint32_t k = 0; k < n; ++k) {
            const std::uint32_t o = k * 8u, in = o + 4u;
            const std::uint8_t bevels = sky.bevels[b + k];
            tri(in + 0, in + 1, in + 2);
            tri(in + 0, in + 2, in + 3);
            for (std::uint32_t c = 0; c < 4; ++c) {
                if (!(bevels & (1u << c))) continue;
                const std::uint32_t c1 = (c + 1) % 4;
                tri(o + c, o + c1, in + c1);
                tri(o + c, in + c1, in + c);
            }
        }
    }
    if (!comfort.empty()) {
        cmd(0xE7000000u, 0);
        cmd(0xB900031Du, 0x00504A40u);    // translucent surface, no z
        cmd(0x01030040u, flat_matrix);    // projection: load
        cmd(0x01020040u, identity_matrix);  // modelview: load
        std::uint32_t at = comfort_start;
        if (frame) {
            cmd(0x04000000u | (7u << 20) | (8u * 16u), at);
            for (std::uint32_t c = 0; c < 4; ++c) {
                const std::uint32_t c1 = (c + 1) % 4;
                tri(c, c1, 4 + c1);
                tri(c, 4 + c1, 4 + c);
            }
            at += 8u * 16u;
        }
        if (horizon) {
            cmd(0x04000000u | (3u << 20) | (4u * 16u), at);
            tri(0, 1, 2);
            tri(0, 2, 3);
        }
    }
    cmd(0xBD000000u, 0x00000000u);    // pop the modelview
    if (projection != 0) cmd(0x01030040u, projection);  // the game's projection again
    cmd(0xE7000000u, 0);
    if (full) return 0;
    store32(rdram, dl, 0xB8000000u);  // G_ENDDL
    store32(rdram, dl + 4u, 0);
    return commands_start;
}

void set_picture_aspect(float aspect) {
    if (aspect > 0.5f && aspect < 10.0f) picture_aspect = aspect;
}

std::array<float, 3> type_colour(int type) {
    switch (type) {
    // Seen in play (2026-11-10): Story 1:1, 2:1, 4:1, 6:1, Lines 1:1, Puzzle 12.
    case 0x20: return {0.92f, 0.80f, 0.18f};  // yellow
    case 0x21: return {0.20f, 0.36f, 0.86f};  // blue
    case 0x22: return {0.22f, 0.72f, 0.30f};  // green
    case 0x23: return {0.80f, 0.26f, 0.70f};  // magenta
    case 0x24: return {0.86f, 0.22f, 0.18f};  // red (0x24 and 0x25 are red and lavender, which is which to confirm)
    case 0x25: return {0.70f, 0.62f, 0.90f};  // pale lavender
    default: return {0.6f, 0.6f, 0.6f};
    }
}

std::vector<std::array<float, 3>> stage_contrast(const std::vector<int>& used) {
    std::vector<std::array<float, 3>> out;
    for (int type = 0x20; type <= 0x25; ++type)
        if (std::find(used.begin(), used.end(), type) == used.end()) out.push_back(type_colour(type));
    if (out.empty()) return palette("amber", {});  // a stage with all six: a fixed set
    return out;
}

std::vector<int> stage_brick_types(const std::uint8_t* rdram) {
    std::vector<int> types;
    const int count = std::clamp<int>(load16(rdram, 0x8013D186u), 0, 6);
    for (int k = 0; k < count; ++k) {
        const std::uint32_t record = load32(rdram, 0x8013D188u + 4u * std::uint32_t(k));
        if (record < 0x80000000u || record >= 0x80400000u) continue;
        const int type = load16(rdram, record + 2u);
        if (type >= 0x20 && type <= 0x25) types.push_back(type);
    }
    return types;
}

bool draw(std::uint8_t* rdram) {
    Settings s = settings();
    if (s.colours == "stage") {
        s.stage_types = stage_brick_types(rdram);
        static std::vector<int> logged;
        if (s.stage_types != logged) {
            logged = s.stage_types;
            std::string list;
            for (const int t : logged) list += (list.empty() ? "" : ",") + std::to_string(t);
            std::fprintf(stdout, "{\"event\":\"brick_sky_stage_types\",\"types\":[%s]}\n", list.c_str());
            std::fflush(stdout);
        }
    }
    if (!s.on) return false;
    if (load32(rdram, background_scene) == 0) return false;
    std::uint32_t dl = load32(rdram, display_list_pointer);
    if (dl < 0x80000000u || dl >= 0x80400000u) return false;
    static Motion motion;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    motion.step(load16(rdram, sphere_angle_x), load16(rdram, sphere_angle_y),
                std::chrono::duration<float>(now - last).count(), s);
    last = now;
    // The projection in use before, to load again afterwards (the last
    // projection load in this frame's list, looked for a little way back).
    std::uint32_t projection = 0;
    for (std::uint32_t at = dl - 8u, n = 0; n < 4096 && at >= 0x80000000u; at -= 8u, ++n) {
        const std::uint32_t w0 = load32(rdram, at);
        if ((w0 >> 24) == 0x01u && (w0 >> 16 & 0x01u) != 0) {
            projection = load32(rdram, at + 4u);
            break;
        }
    }
    static int frame = 0;
    const std::uint32_t list = build(rdram, s, motion, projection, frame++, picture_aspect.load());
    if (list == 0) return false;
    store32(rdram, dl, 0x06000000u);  // G_DL: call ours, then come back
    store32(rdram, dl + 4u, list);
    store32(rdram, display_list_pointer, dl + 8u);
    static const bool log = std::getenv("BLOCKPLANET_BRICK_SKY_LOG") != nullptr;
    if (log && frame % 30 == 1) {
        std::fprintf(stdout, "{\"event\":\"brick_sky\",\"yaw\":%.3f,\"pitch\":%.3f,\"yaw_speed\":%.3f,\"pitch_speed\":%.3f,\"sphere_x\":%d,\"sphere_y\":%d}\n",
                     motion.yaw, motion.pitch, motion.yaw_speed, motion.pitch_speed, motion.last_x, motion.last_y);
        std::fflush(stdout);
    }
    return true;
}

} // namespace tetrisphere::brick_sky
