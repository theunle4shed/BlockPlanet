// The game over shell (the owner's design, 2026-11-11): when the last heart
// is lost, black blocks of 4 x 4 cells settle onto the sphere's visible side,
// each sliding straight down from a few layers above its place onto the top
// of the board there, one after another from the edge of the view inwards.
// It is drawn by BlockPlanet with the sphere (after the extended patch's last
// quarter, sphere_hooks.cpp), from the same mesh and normals, as quads made
// with the sphere's own face code; the game's board is not changed.
// BLOCKPLANET_GAMEOVER_SHELL=0 leaves the game's own game over alone.

#include "tetrisphere/game_over_shell.h"

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/sphere_cell.h"
#include "tetrisphere/sphere_extended.h"
#include "tetrisphere/sphere_faces.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace tetrisphere::sphere {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t display_list_pointer = 0x800F22B4u;
constexpr std::uint32_t active_context = 0x8013DD00u;
constexpr std::uint32_t board_column_offset = 0x2B20u;  // context word, added to the mesh row
constexpr std::uint32_t board_row_offset = 0x2B24u;     // context word, added to the mesh column
constexpr std::uint32_t board_pointer = 0x800E44DCu;    // one byte per cell, height << 4 | kind
constexpr std::uint32_t p1_hearts = 0x80110220u + 0x2D7Eu;
constexpr std::uint32_t main_row = 0x800E121Cu;

constexpr int block_cells = 4;          // 32 cells round the sphere: 8 blocks a row, no gap
constexpr double start_layers = 1.5;    // how far above its place a block starts (more comes at the camera)
constexpr double fall_seconds = 0.5;    // each block's slide
constexpr double spread_seconds = 0.7;  // the last block starts this long after the first

// The face work area the sphere's face code reads (sphere_cell.cpp's table),
// by mesh axis: corner order (row, column) = (0, 0), (1, 0), (1, 1), (0, 1).
struct Corner {
    int row, column;
    std::array<std::uint32_t, 3> top, bottom;
};
constexpr std::array<Corner, 4> corners{{
    {0, 0, {0x8015EDA4u, 0x8015ED9Cu, 0x8015EDACu}, {0x8015F4E8u, 0x8015F4E4u, 0x8015F4EEu}},
    {1, 0, {0x8015EDB4u, 0x8015EDB0u, 0x8015EDB8u}, {0x8015F4FEu, 0x8015F4F6u, 0x8015F506u}},
    {1, 1, {0x8015EE1Au, 0x8015EDBCu, 0x8015EEC8u}, {0x8015F566u, 0x8015F536u, 0x8015F594u}},
    {0, 1, {0x8015F4DCu, 0x8015F4D2u, 0x8015F4E0u}, {0x8015F5A0u, 0x8015F59Au, 0x8015F5A6u}},
}};
// Normals: per corner, then the row and column sides (sphere_cell.cpp).
constexpr std::array<std::array<std::uint32_t, 3>, 4> corner_normal{{
    {0x8015F7ECu, 0x8015F7FCu, 0x8015F80Cu},
    {0x8015F7F4u, 0x8015F804u, 0x8015F814u},
    {0x8015F7F8u, 0x8015F808u, 0x8015F818u},
    {0x8015F7F0u, 0x8015F800u, 0x8015F810u},
}};
constexpr std::array<std::uint32_t, 6> row_normal_out{0x8015F4D4u, 0x8015F4DEu, 0x8015F4E2u, 0x8015F4E6u, 0x8015F4EAu, 0x8015F4F2u};
constexpr std::array<std::uint32_t, 6> column_normal_out{0x8015F4F8u, 0x8015F500u, 0x8015F508u, 0x8015F538u, 0x8015F568u, 0x8015F596u};

class Live final : public GuestMemory {
public:
    explicit Live(std::uint8_t* rdram) : rdram_(rdram) {}
    std::uint8_t read8(std::uint32_t a) const override { return load8(rdram_, a); }
    std::int16_t read16(std::uint32_t a) const override { return load16(rdram_, a); }
    std::uint32_t read32(std::uint32_t a) const override { return load32(rdram_, a); }
    void write8(std::uint32_t a, std::uint8_t v) override { store8(rdram_, a, v); }
    void write16(std::uint32_t a, std::int16_t v) override { store16(rdram_, a, v); }
    void write32(std::uint32_t a, std::uint32_t v) override { store32(rdram_, a, v); }

private:
    std::uint8_t* rdram_;
};

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_GAMEOVER_SHELL");
        return v == nullptr || v[0] != '0';
    }();
    return on;
}

void append(std::uint8_t* rdram, std::uint32_t w0, std::uint32_t w1) {
    const std::uint32_t at = load32(rdram, display_list_pointer);
    store32(rdram, at, w0);
    store32(rdram, at + 4u, w1);
    store32(rdram, display_list_pointer, at + 8u);
}

double ease_out(double u) {
    u = std::clamp(u, 0.0, 1.0);
    const double v = 1.0 - u;
    return 1.0 - v * v * v;
}

} // namespace

void draw_game_over_shell(std::uint8_t* rdram, const ExtendedSphere& sphere) {
    if (!enabled()) return;
    if (std::getenv("BLOCKPLANET_GAMEOVER_SHELL_LOG") != nullptr) {  // research
        static int n = 0;
        if (n++ % 20 == 0)
            std::fprintf(stdout, "{\"event\":\"shell_probe\",\"hearts\":%d,\"row\":%d,\"ring\":%d}\n", int(load16(rdram, p1_hearts)),
                         int(load16(rdram, main_row)), sphere.ring());
    }
    static std::chrono::steady_clock::time_point started{};
    static bool running = false;
    const bool game_over = load16(rdram, p1_hearts) == -1 && load16(rdram, main_row) == -1;
    if (!game_over) {
        running = false;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!running) running = true, started = now;
    const double seconds = std::chrono::duration<double>(now - started).count();

    const std::uint32_t context = load32(rdram, active_context);
    const std::uint32_t off_row = load32(rdram, context + board_column_offset);
    const std::uint32_t off_column = load32(rdram, context + board_row_offset);
    const std::uint32_t map = load32(rdram, board_pointer);
    const auto height_at = [&](int row, int column) {
        const std::uint32_t bx = (static_cast<std::uint32_t>(row) + off_row) & 0x1Fu;
        const std::uint32_t by = (static_cast<std::uint32_t>(column) + off_column) & 0x1Fu;
        // The column runs from its first layer (high nibble) to its end (low
        // nibble + 1, at most 7): the drawing code lifts its faces a layer
        // per block from the first to the end (sphere_quadrants.cpp).
        const int end = (load8(rdram, map + (by << 5) + bx) & 0xF) + 1;
        return std::min(end, 7);
    };
    // Block origins (mesh rows and columns) on the board's 4 x 4 grid, all
    // four corners inside the extended patch (it runs from -ring to 14 + ring).
    const auto origins = [&](std::uint32_t offset) {
        std::vector<int> out;
        // Only over the game's own 15 x 15 patch (cells 0 to 14), the side
        // facing the camera: blocks beyond it stand edge-on at the silhouette.
        for (int r = -1; r + block_cells <= 16; ++r)
            if (((static_cast<std::uint32_t>(r) + offset) & (block_cells - 1)) == 0) out.push_back(r);
        return out;
    };
    const auto rows = origins(off_row), columns = origins(off_column);
    if (rows.empty() || columns.empty()) return;

    // Dark blocks: the block texture (its bevelled edges, stretched over the
    // 4 x 4 block) times a dark primitive colour, put back to the sphere's
    // own combine after.
    std::uint32_t previous_w0 = 0, previous_w1 = 0;
    for (std::uint32_t at = load32(rdram, display_list_pointer) - 8u, n = 0; n < 20000 && at >= 0x80000000u; at -= 8u, ++n)
        if ((load32(rdram, at) >> 24) == 0xFCu) {
            previous_w0 = load32(rdram, at), previous_w1 = load32(rdram, at + 4u);
            break;
        }
    // gsDPSetCombineLERP(TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, 1) in both cycles.
    const auto combine = [](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t aa, std::uint32_t ab,
                            std::uint32_t ac, std::uint32_t ad) {
        const std::uint32_t w0 = 0xFC000000u | (a & 0xF) << 20 | (c & 0x1F) << 15 | (aa & 7) << 12 | (ac & 7) << 9 | (a & 0xF) << 5 | (c & 0x1F);
        const std::uint32_t w1 = (b & 0xF) << 28 | (b & 0xF) << 24 | (aa & 7) << 21 | (ac & 7) << 18 | (d & 7) << 15 | (ab & 7) << 12 |
                                 (ad & 7) << 9 | (d & 7) << 6 | (ab & 7) << 3 | (ad & 7);
        return std::pair{w0, w1};
    };
    const auto [cw0, cw1] = combine(1 /*TEXEL0*/, 15 /*0*/, 3 /*PRIMITIVE*/, 7 /*0*/, 7 /*0*/, 7 /*0*/, 7 /*0*/, 6 /*1*/);
    static const bool plain = std::getenv("BLOCKPLANET_GAMEOVER_SHELL_PLAIN") != nullptr;  // research
    append(rdram, 0xE7000000u, 0);           // pipe sync
    if (!plain) append(rdram, cw0, cw1);
    append(rdram, 0xFA000000u, 0x4A4C58FFu); // primitive colour: near black, a little blue

    Live memory(rdram);
    const double centre = (sphere.first_cell() + sphere.last_cell() + 1) / 2.0;
    double far = 1.0;
    for (int r : rows)
        for (int c : columns)
            far = std::max(far, std::hypot(r + 2.0 - centre, c + 2.0 - centre));
    for (int r : rows)
        for (int c : columns) {
            // From the edge of the view inwards, so the nearest blocks (which
            // loom largest as they slide) come last and close it.
            const double delay = spread_seconds * (1.0 - std::hypot(r + 2.0 - centre, c + 2.0 - centre) / far);
            const double u = (seconds - delay) / fall_seconds;
            if (u <= 0.0) continue;  // not yet
            int top = 1;
            for (int dr = 0; dr < block_cells; ++dr)
                for (int dc = 0; dc < block_cells; ++dc) top = std::max(top, height_at(r + dr, c + dc));
            // Its inner face on the top of the board there (the outer face of
            // the column's top block is base + (height - 1) steps), lifted
            // while it slides down.
            const double lift = start_layers * (1.0 - ease_out(u));
            const double inner = top - 1 + lift, outer = inner + 1.0;
            for (std::size_t k = 0; k < corners.size(); ++k) {
                const MeshEntry& e = sphere.point(r + corners[k].row * block_cells, c + corners[k].column * block_cells);
                for (int axis = 0; axis < 3; ++axis) {
                    memory.write16(corners[k].top[axis], static_cast<std::int16_t>(std::lround(e.outer[axis] + outer * e.inner[axis])));
                    memory.write16(corners[k].bottom[axis], static_cast<std::int16_t>(std::lround(e.outer[axis] + inner * e.inner[axis])));
                }
                const Normal& n = sphere.point_normal(r + corners[k].row * block_cells, c + corners[k].column * block_cells);
                for (int axis = 0; axis < 3; ++axis) memory.write16(corner_normal[k][axis], n[axis]);
            }
            for (int axis = 0; axis < 3; ++axis) {
                memory.write16(row_normal_out[axis], sphere.row_normal(r)[axis]);
                memory.write16(row_normal_out[axis + 3], static_cast<std::int16_t>(-sphere.row_normal(r + block_cells - 1)[axis]));
                memory.write16(column_normal_out[axis], sphere.column_normal(c)[axis]);
                memory.write16(column_normal_out[axis + 3], static_cast<std::int16_t>(-sphere.column_normal(c + block_cells - 1)[axis]));
            }
            for (Face face : {Face::RowStart, Face::RowEnd, Face::ColumnStart, Face::ColumnEnd, Face::Outer}) draw_face(memory, face);
        }
    if (std::getenv("BLOCKPLANET_GAMEOVER_SHELL_LOG") != nullptr) {  // research
        static int n = 0;
        if (n++ % 20 == 0) {
            const MeshEntry& e = sphere.point(rows.front(), columns.front());
            std::fprintf(stdout, "{\"event\":\"shell_draw\",\"t\":%.2f,\"rows\":%zu,\"columns\":%zu,\"combine\":\"%08X %08X\",\"p\":[%d,%d,%d,%d,%d,%d],\"h\":%d}\n",
                         seconds, rows.size(), columns.size(), previous_w0, previous_w1, e.outer[0], e.outer[1], e.outer[2], e.inner[0], e.inner[1],
                         e.inner[2], height_at(rows.front(), columns.front()));
            std::string hs;
            for (int r = sphere.first_cell(); r <= sphere.last_cell(); ++r) {
                for (int c = sphere.first_cell(); c <= sphere.last_cell(); ++c) hs += char('0' + std::min(9, height_at(r, c)));
                hs += ' ';
            }
            std::fprintf(stdout, "HEIGHTS %s\n", hs.c_str());
        }
    }
    append(rdram, 0xE7000000u, 0);
    if (previous_w0 != 0) append(rdram, previous_w0, previous_w1);
}

} // namespace tetrisphere::sphere
