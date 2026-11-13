#include "tetrisphere/minefield_sphere.h"
#include "tetrisphere/sphere_cell.h"
#include "tetrisphere/sphere_mesh.h"

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"
#include "../ui/ui_renderer.h"
#include "stb_image.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace tetrisphere::minefield_sphere {
namespace {

constexpr std::uint32_t player1 = 0x80110220u;
constexpr std::uint32_t menu_state = 0x800E121Cu;  // -1 in play
constexpr std::uint32_t game_mode = 0x800E44A8u;    // 1 Rescue
constexpr std::uint32_t values_pointer = 0x800E44D0u;
constexpr std::uint32_t masks_pointer = 0x800E44D4u;
constexpr std::uint32_t entries_pointer = 0x800E44D8u;
constexpr std::uint32_t columns_pointer = 0x800E44DCu;
constexpr int layer_cells = 1024;
constexpr std::int16_t inert = 423;      // type 47 x 9
constexpr std::uint8_t top_face = 16;
constexpr std::uint8_t open_core = 80;   // the core's face mask under an empty cell
constexpr int board = 32;

std::mutex mutex;
std::atomic<bool> armed_flag{false};
std::atomic<bool> active_flag{false};
std::atomic<int> requested_mines{130};
int field_width = 32, field_height = 32;
std::vector<bool> field_walls;  // the arena's walls inside the field (Arena::walls)
int field_arena = 0;
std::atomic<bool> exit_requested{false};
// The play time: from the first uncover to the end.
std::optional<std::chrono::steady_clock::time_point> play_started, play_ended;
int origin_x = 0, origin_y = 0;  // the board cell of the field's (0, 0)
std::atomic<int> pending_open{0}, pending_flag{0};
std::uint16_t last_buttons = 0;

std::optional<Minefield> field;
int frames_in_play = 0;
bool built = false;
std::vector<bool> shown_open;  // cells already cleared on the board
int over_frames = 0;           // frames since the field was won or lost
int cursor_cell = -1, cursor_field = -1;  // the board and field cell last under the cursor
// The directions the cursor may not move in (the field's edge), for the
// input filters; and its last place inside the field.
constexpr int edge_right = 1, edge_left = 2, edge_up = 4, edge_down = 8;
std::atomic<int> edges{0};
int inside_x = 0, inside_y = 0;
bool inside_known = false;

int cell_index(int x, int y) { return ((y % board + board) % board) * board + ((x % board + board) % board); }

// The field cell (index) at a board cell, or -1 outside the field.
int field_at(int bx, int by) {
    const int fx = ((bx - origin_x) % board + board) % board, fy = ((by - origin_y) % board + board) % board;
    if (fx >= field_width || fy >= field_height) return -1;
    const int index = fy * field_width + fx;
    if (!field_walls.empty() && field_walls[std::size_t(index)]) return -1;  // a wall inside the arena
    return index;
}

// The field cell (index) at a board cell, walls included, or -1 outside the
// field's bounds: where the cursor may go (the owner's call, 2026-11-13: it
// crosses an arena's walls, to reach Decompartmental's other compartments,
// but stops at a smaller field's edge).
int in_bounds(int bx, int by) {
    const int fx = ((bx - origin_x) % board + board) % board, fy = ((by - origin_y) % board + board) % board;
    if (fx >= field_width || fy >= field_height) return -1;
    return fy * field_width + fx;
}

// The board cell under the cursor: the cursor's values (player 1 +0x2B2A,
// +0x2B2E) count from the cell (8, 8) (measured by dropping pieces). Moving
// right lowers x; moving up raises y.
int cursor_x(const std::uint8_t* rdram) { return 8 + guest::load16(rdram, player1 + 0x2B2A); }
int cursor_y(const std::uint8_t* rdram) { return 8 + guest::load16(rdram, player1 + 0x2B2E); }

// Every cube's appearance: a cell's entry (0x800E44D8) points at a table of
// texture coordinates on its type's texture sheet, face by face. The game's
// piece cells cut theirs from a piece's end, middle or filler, so they read as
// 1 x 3s; the smooth single block (as the Puzzle and Training walls) cuts
// every face from the sheet's first tile (texels 1 to 14). BlockPlanet keeps
// such a table of its own in the free upper 4 MB and points every cube at it.
// The sheet holds 16 such 16-texel tiles: the first is flat (no outline);
// others are stripes, outlined cubes (81 is used) or bricks; 113 and 225
// sink each cube's centre (2026-11-12, stepping through them in play).
// Research: BLOCKPLANET_MINEFIELD_TILE=x picks another tile's left texel.
constexpr std::uint32_t smooth_table = 0x807F8000u;
constexpr int smooth_table_halves = 48;  // 0x60 bytes, like the game's

void write_smooth_table(std::uint8_t* rdram) {
    static const int left = [] {
        const char* v = std::getenv("BLOCKPLANET_MINEFIELD_TILE");
        const int x = v ? std::atoi(v) : 81;
        return x >= 0 && x <= 241 ? x : 81;
    }();
    const std::int16_t a = std::int16_t(left), b = std::int16_t(left + 13);
    const std::int16_t corners[8] = {a, 14, b, 14, a, 1, b, 1};
    for (int i = 0; i < smooth_table_halves; ++i)
        guest::store16(rdram, smooth_table + 2u * std::uint32_t(i), corners[i % 8]);
}

void build_board(std::uint8_t* rdram) {
    const std::uint32_t values = guest::load32(rdram, values_pointer);
    const std::uint32_t masks = guest::load32(rdram, masks_pointer);
    const std::uint32_t entries = guest::load32(rdram, entries_pointer);
    const std::uint32_t columns = guest::load32(rdram, columns_pointer);
    write_smooth_table(rdram);
    for (int c = 0; c < layer_cells; ++c) {
        guest::store32(rdram, entries + 4u * std::uint32_t(layer_cells + c), smooth_table);
        field_cells()[c].store(field_at(c % board, c / board) >= 0, std::memory_order_relaxed);
        guest::store16(rdram, values + 2u * std::uint32_t(layer_cells + c), inert);
        guest::store8(rdram, masks + std::uint32_t(layer_cells + c), top_face);
        guest::store8(rdram, masks + std::uint32_t(c), 0);  // the core, covered
        // The core: the game's plain core tiles (81 to 89, a 3 x 3 set, as
        // round Hide & Seek's pictures), not Story 1:1's glowing ones (90 to
        // 93). Its tiles set the core's look (the owner's hint, 2026-11-12:
        // Hide & Seek's core differs). Research: BLOCKPLANET_MINEFIELD_CORE=
        // base,width tries another set (72,3: the picture tiles; 90,2).
        static const auto core_set = [] {
            std::pair<int, int> set{81, 3};
            if (const char* v = std::getenv("BLOCKPLANET_MINEFIELD_CORE")) std::sscanf(v, "%d,%d", &set.first, &set.second);
            if (set.second < 1) set.second = 1;
            return set;
        }();
        {
            const int x = c % board, y = c / board, width = core_set.second;
            guest::store16(rdram, values + 2u * std::uint32_t(c),
                           static_cast<std::int16_t>(core_set.first + x % width + width * (y % width)));
        }
        guest::store8(rdram, columns + std::uint32_t(c), 0x11);
        for (int layer = 2; layer < 9; ++layer) {
            guest::store16(rdram, values + 2u * std::uint32_t(layer * layer_cells + c), 0);
            guest::store8(rdram, masks + std::uint32_t(layer * layer_cells + c), 0);
        }
    }
}

// Clears one layer-1 cell and opens the faces round it.
void clear_cell(std::uint8_t* rdram, int x, int y) {
    const std::uint32_t values = guest::load32(rdram, values_pointer);
    const std::uint32_t masks = guest::load32(rdram, masks_pointer);
    const std::uint32_t entries = guest::load32(rdram, entries_pointer);
    const std::uint32_t columns = guest::load32(rdram, columns_pointer);
    const int c = cell_index(x, y);
    guest::store16(rdram, values + 2u * std::uint32_t(layer_cells + c), 0);
    guest::store8(rdram, masks + std::uint32_t(layer_cells + c), 0);
    guest::store32(rdram, entries + 4u * std::uint32_t(layer_cells + c), 0);
    guest::store8(rdram, masks + std::uint32_t(c), open_core);
    guest::store8(rdram, columns + std::uint32_t(c), 0x00);
    struct Side { int dx, dy; std::uint8_t bit; };
    for (const Side s : {Side{-1, 0, 8}, Side{1, 0, 4}, Side{0, -1, 2}, Side{0, 1, 1}}) {
        const int n = cell_index(x + s.dx, y + s.dy);
        const std::uint32_t at = masks + std::uint32_t(layer_cells + n);
        if (guest::load16(rdram, values + 2u * std::uint32_t(layer_cells + n)) == 0) continue;
        guest::store8(rdram, at, static_cast<std::uint8_t>(guest::load8(rdram, at) | s.bit));
    }
}

void stop_locked() {
    tetrisphere::sphere::fill_training_gaps() = true;
    over_frames = 0;
    cursor_cell = cursor_field = -1;
    play_started.reset();
    play_ended.reset();
    exit_requested = false;
    edges = 0;
    inside_known = false;
    tetrisphere::sphere::set_force_replace(false);
    for (int c = 0; c < layer_cells; ++c) {
        field_cells()[c].store(false, std::memory_order_relaxed);
        cube_colours()[c].store(0, std::memory_order_relaxed);
    }
    active_flag = false;
    blue_field() = false;
    field.reset();
    built = false;
    frames_in_play = 0;
}

// The numbers on the uncovered core, drawn by the game as part of the core
// (the owner's idea, 2026-11-12: Hide & Seek's pictures stay on the core
// however the board turns). Each core tile is a 16 x 16 image of 256-colour
// indices drawn through a palette of RGBA 16-bit colours; the picture tiles
// (core values 72 to 80, as Hide & Seek's pictures use) are nine such images
// one after another, with one palette (seen in the display list, 2026-11-12;
// the plain core, 81 to 89, has its own image and palette). While the field
// is in play BlockPlanet writes its own nine tiles over them (assets/ui/
// minefield/core_digits.png, tools/art/minefield_marks.py: a plain tile, then
// the digits 1 to 8), and an uncovered cell's core value becomes 72 + the
// mines round it.
constexpr std::uint32_t picture_tiles = 0x80139F70u;    // nine 16 x 16 tiles, 0x100 bytes each
constexpr std::uint32_t picture_palette = 0x80130F28u;  // 256 colours, 2 bytes each
// The plain core (values 81 to 89) is one tile with its own palette; plain
// cells are all under cubes, so BlockPlanet draws the cursor's highlighted
// tile there and gives the uncovered cell under the cursor value 81.
constexpr std::uint32_t plain_tile = 0x80139E70u;
constexpr std::uint32_t plain_palette = 0x80130D28u;
constexpr std::int16_t plain_value = 81;
constexpr int core_tile = 16;
constexpr int core_tiles = 9;
constexpr std::int16_t first_picture_value = 72;

struct CoreDigits {
    std::vector<std::uint16_t> palette;  // RGBA 16-bit
    std::vector<std::uint8_t> indices;   // core_tiles x 16 x 16, tile after tile; then the highlighted ones
};

// Loads the sheet once: its colours become the palette (at most 256).
const std::optional<CoreDigits>& core_digits() {
    static const std::optional<CoreDigits> digits = []() -> std::optional<CoreDigits> {
        const auto path = blockplanet::ui::ui_asset_root() / "minefield" / "core_digits.png";
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (pixels == nullptr) return std::nullopt;
        CoreDigits out;
        bool fits = w >= core_tile * core_tiles && h >= 2 * core_tile;
        for (int k = 0; fits && k < 2 * core_tiles; ++k)
            for (int y = 0; fits && y < core_tile; ++y)
                for (int x = 0; x < core_tile; ++x) {
                    const int row = k / core_tiles, column = k % core_tiles;
                    const stbi_uc* p = pixels + 4 * ((row * core_tile + y) * w + column * core_tile + x);
                    const auto colour = std::uint16_t((p[0] >> 3) << 11 | (p[1] >> 3) << 6 | (p[2] >> 3) << 1 | 1);
                    auto at = std::find(out.palette.begin(), out.palette.end(), colour);
                    if (at == out.palette.end()) {
                        if (out.palette.size() == 256) {
                            fits = false;
                            break;
                        }
                        out.palette.push_back(colour);
                        at = out.palette.end() - 1;
                    }
                    out.indices.push_back(static_cast<std::uint8_t>(at - out.palette.begin()));
                }
        stbi_image_free(pixels);
        if (!fits) return std::nullopt;
        return out;
    }();
    return digits;
}

// Writes the tiles and the palette over the picture tiles (each frame while
// the field is in play, in case the stage reloads them).
// `cursor_count`: the mines round the uncovered cell under the cursor, whose
// highlighted tile goes in the plain core's tile (-1: none).
void write_core_digits(std::uint8_t* rdram, int cursor_count) {
    const auto& digits = core_digits();
    if (!digits) return;
    for (std::size_t i = 0; i < digits->palette.size(); ++i) {
        guest::store16(rdram, picture_palette + 2u * std::uint32_t(i), static_cast<std::int16_t>(digits->palette[i]));
        guest::store16(rdram, plain_palette + 2u * std::uint32_t(i), static_cast<std::int16_t>(digits->palette[i]));
    }
    // Core value 72 + k draws picture slot 3 x (k / 3) + 2 - k % 3 (each row of
    // three runs the other way; found by comparing the field with the core).
    constexpr std::size_t tile_bytes = core_tile * core_tile;
    for (int k = 0; k < core_tiles; ++k) {
        const int slot = 3 * (k / 3) + 2 - k % 3;
        for (std::size_t i = 0; i < tile_bytes; ++i)
            guest::store8(rdram, picture_tiles + std::uint32_t(slot * tile_bytes + i), digits->indices[std::size_t(k) * tile_bytes + i]);
    }
    if (cursor_count >= 0 && cursor_count < core_tiles)
        for (std::size_t i = 0; i < tile_bytes; ++i)
            guest::store8(rdram, plain_tile + std::uint32_t(i),
                          digits->indices[std::size_t(core_tiles + cursor_count) * tile_bytes + i]);
}

} // namespace

void arm(int arena, int mines) {
    std::lock_guard lock(mutex);
    const auto& list = arenas();
    const auto& a = list[std::size_t(std::clamp(arena, 0, int(list.size()) - 1))];
    requested_mines = mines >= 0 ? mines : a.mines;
    field_width = a.width;
    field_height = a.height;
    field_walls = a.walls;
    field_arena = std::clamp(arena, 0, int(list.size()) - 1);
    armed_flag = true;
    stop_locked();
}

bool armed() { return armed_flag.load(); }
bool active() { return active_flag.load(); }

std::uint16_t filter_input(std::uint16_t buttons) {
    if (!active_flag.load()) {
        last_buttons = buttons;
        return buttons;
    }
    constexpr std::uint16_t a = 0x8000, b = 0x4000;
    if ((buttons & a) && !(last_buttons & a)) ++pending_open;
    if ((buttons & b) && !(last_buttons & b)) ++pending_flag;
    last_buttons = buttons;
    // The D-pad stops at the field's edge (the owner's request): right,
    // left, up, down (N64 bits 0x0100, 0x0200, 0x0800, 0x0400).
    const int blocked = edges.load();
    std::uint16_t drop = a | b;
    if (blocked & edge_right) drop |= 0x0100;
    if (blocked & edge_left) drop |= 0x0200;
    if (blocked & edge_up) drop |= 0x0800;
    if (blocked & edge_down) drop |= 0x0400;
    return static_cast<std::uint16_t>(buttons & ~drop);
}

void filter_stick(float& x, float& y) {
    if (!active_flag.load()) return;
    const int blocked = edges.load();
    if ((blocked & edge_right) && x > 0) x = 0;
    if ((blocked & edge_left) && x < 0) x = 0;
    if ((blocked & edge_up) && y > 0) y = 0;
    if ((blocked & edge_down) && y < 0) y = 0;
}

// The field cell the player starts on: the field's middle, or where that is a
// wall, the middle of the open area nearest it (its cells' average, in
// unwrapped steps from where the search starts, so an area across the board's
// edge works too), moved to its nearest open cell.
std::pair<int, int> starting_cell() {
    const int w = field_width, h = field_height, mx = w / 2, my = h / 2;
    const auto wall = [&](int x, int y) {
        x = (x % w + w) % w, y = (y % h + h) % h;
        return !field_walls.empty() && field_walls[std::size_t(y * w + x)];
    };
    if (!wall(mx, my)) return {mx, my};
    // The open cell nearest the middle.
    int sx = mx, sy = my;
    for (int r = 1, found = 0; r < board && !found; ++r)
        for (int dy = -r; dy <= r && !found; ++dy)
            for (int dx = -r; dx <= r && !found; ++dx)
                if (!wall(mx + dx, my + dy)) sx = mx + dx, sy = my + dy, found = 1;
    // Its area (four-way steps), each cell with the unwrapped place it was reached at.
    std::vector<int> seen(std::size_t(w * h), 0);
    std::vector<std::pair<int, int>> todo{{sx, sy}}, cells;
    seen[std::size_t(((sy % h + h) % h) * w + ((sx % w + w) % w))] = 1;
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        cells.push_back({x, y});
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx, ny = y + dy;
            const std::size_t key = std::size_t(((ny % h + h) % h) * w + ((nx % w + w) % w));
            if (wall(nx, ny) || seen[key]) continue;
            seen[key] = 1;
            todo.push_back({nx, ny});
        }
    }
    double ax = 0, ay = 0;
    for (const auto& [x, y] : cells) ax += x, ay += y;
    ax /= double(cells.size()), ay /= double(cells.size());
    // The area's cell nearest that average.
    auto best = cells.front();
    double best_d = 1e18;
    for (const auto& [x, y] : cells) {
        const double d = (x - ax) * (x - ax) + (y - ay) * (y - ay);
        if (d < best_d) best_d = d, best = {x, y};
    }
    return {(best.first % w + w) % w, (best.second % h + h) % h};
}

void poll(std::uint8_t* rdram) {
    if (rdram == nullptr || !armed_flag.load()) return;
    std::lock_guard lock(mutex);
    const bool in_play = guest::load16(rdram, menu_state) == -1 && guest::load16(rdram, game_mode) == 1 &&
                         guest::load32(rdram, values_pointer) >= 0x80000000u;
    if (!in_play) {
        // Left play (Exit, or the game's own screens): the test ends.
        if (active_flag.load()) {
            stop_locked();
            armed_flag = false;
        }
        return;
    }
    // Let the stage set itself up first (about a second in play).
    if (!built && ++frames_in_play < 60) return;
    if (!built) {
        // The field's middle on the cursor's starting cell (8, 8); the whole
        // board wraps. Where an arena's middle is a wall (Decompartmental,
        // Horizon), the middle of the area nearest it is used instead, so the
        // player starts in the middle of a compartment (the owner's call).
        {
            const auto [cx, cy] = starting_cell();
            origin_x = 8 - cx;
            origin_y = 8 - cy;
        }
        build_board(rdram);
        field.emplace(field_width, field_height, requested_mines.load(), std::random_device{}(),
                      field_width == board && field_height == board, field_walls);
        shown_open.assign(layer_cells, false);
        built = true;
        active_flag = true;
        blue_field() = true;
        tetrisphere::sphere::fill_training_gaps() = false;
        tetrisphere::sphere::set_force_replace(true);
        std::fprintf(stdout, "{\"event\":\"minefield_sphere\",\"state\":\"ready\",\"mines\":%d}\n", field->mines());
        std::fflush(stdout);
    }
    // The stage may lay its own board after the field was built (seen on a
    // later stage, 2026-11-12): any cell above layer 1, or a layer-1 cell that
    // is neither inert nor empty, means the board is the game's again, so the
    // field is laid once more and its uncovered cells cleared again.
    {
        const std::uint32_t values = guest::load32(rdram, values_pointer);
        bool replaced = false;
        int why_cell = -1, why_value = 0, why_layer = 0;
        for (int c = 0; c < layer_cells && !replaced; ++c) {
            const std::int16_t v = guest::load16(rdram, values + 2u * std::uint32_t(layer_cells + c));
            const std::int16_t v2 = guest::load16(rdram, values + 2u * std::uint32_t(2 * layer_cells + c));
            replaced = (v != 0 && v != inert) || v2 != 0;
            if (replaced) why_cell = c, why_value = v2 != 0 ? v2 : v, why_layer = v2 != 0 ? 2 : 1;
        }
        if (replaced) {
            build_board(rdram);
            shown_open.assign(layer_cells, false);
            std::fprintf(stdout, "{\"event\":\"minefield_sphere\",\"state\":\"rebuilt\",\"cell\":[%d,%d],\"layer\":%d,\"value\":%d}\n",
                         why_cell % board, why_cell / board, why_layer, why_value);
            std::fflush(stdout);
        }
    }
    // Research: BLOCKPLANET_MINEFIELD_CORE_VALUES steps the core's tile values
    // through 0 to 71, a 3 x 3 set of nine every 2 seconds (logged), to find
    // the game's core looks (81 to 89 plain, 90 to 93 Story 1:1's, 72 to 80
    // the picture tiles).
    static const bool core_values = std::getenv("BLOCKPLANET_MINEFIELD_CORE_VALUES") != nullptr;
    if (core_values) {
        static int frame = 0, shown = -1;
        const int set = (frame++ / 120) % 8;
        if (set != shown) {
            shown = set;
            const std::uint32_t values = guest::load32(rdram, values_pointer);
            for (int c = 0; c < layer_cells; ++c)
                guest::store16(rdram, values + 2u * std::uint32_t(c),
                               static_cast<std::int16_t>(set * 9 + (c % board) % 3 + 3 * ((c / board) % 3)));
            std::fprintf(stdout, "{\"event\":\"core_values\",\"base\":%d}\n", set * 9);
            std::fflush(stdout);
        }
    }
    // The cursor's shadow marks one cell (the owner's idea, 2026-11-12): the
    // piece in hand (+0x2386) holds shape-table
    // entry 24, a fixed 1 x 1 piece (23 is an L of three; holding the
    // Random Block, 22, crashed the game). BLOCKPLANET_MINEFIELD_PIECE=n
    // tries another entry (-1: the stage's own pieces).
    static const int piece = [] { const char* v = std::getenv("BLOCKPLANET_MINEFIELD_PIECE"); return v ? std::atoi(v) : 24; }();
    if (piece >= 0) {
        guest::store16(rdram, player1 + 0x2386, static_cast<std::int16_t>(piece));
        // (The queue is left alone: holding 24 there as well crashed the game.)
    }
    // The drop timer never runs out (below), and the
    // stage is never won: it ends when an S x S square of the core is
    // uncovered (S at +0x23E4, 4 to 8 in Rescue), so S is the whole board,
    // which the mines always keep covered. (Turning the goal off instead,
    // 0x800E44A4 = 0, stopped the holes being drawn.)
    // The drop timer is a float (+0x2408, counting up to the limit at
    // 0x800F116C, 6000) that +0x2B10 (up to 600) is worked out from: both
    // are held at 0, so the game never drops a piece on its own.
    guest::store32(rdram, player1 + 0x2408, 0);
    guest::store16(rdram, player1 + 0x2B10, 0);
    guest::store16(rdram, player1 + 0x23E4, board);
    // The cursor stays within the field's bounds (it may cross walls): if it
    // got out (the stick, or a press the filter missed), it goes back to its
    // last place inside; then the directions that would leave are blocked.
    // After the end the sphere turns freely, to look the result over (the
    // owner's call), and drop goes back to the menu.
    const bool ended = field->state() == Minefield::State::Won || field->state() == Minefield::State::Lost;
    if (!ended && in_bounds(cursor_x(rdram), cursor_y(rdram)) < 0 && inside_known) {
        guest::store16(rdram, player1 + 0x2B2A, static_cast<std::int16_t>(inside_x - 8));
        guest::store16(rdram, player1 + 0x2B2E, static_cast<std::int16_t>(inside_y - 8));
    }
    const int x = cursor_x(rdram), y = cursor_y(rdram);
    if (in_bounds(x, y) >= 0) inside_x = x, inside_y = y, inside_known = true;
    // Moving right lowers x; up raises y.
    edges = ended ? 0
                  : (in_bounds(x - 1, y) < 0 ? edge_right : 0) | (in_bounds(x + 1, y) < 0 ? edge_left : 0) |
                        (in_bounds(x, y + 1) < 0 ? edge_up : 0) | (in_bounds(x, y - 1) < 0 ? edge_down : 0);
    const int under_cursor = in_bounds(x, y);  // walls included, for the cursor's colour
    const int here = field_at(x, y);  // -1 on the wall: nothing happens
    if (ended) {
        pending_flag = 0;
        // A second after the end, drop asks to leave (handled by the host).
        if (pending_open.exchange(0) > 0 && over_frames > 60) exit_requested = true;
    }
    for (int n = pending_flag.exchange(0); n > 0; --n) field->toggle_flag(here);
    for (int n = pending_open.exchange(0); n > 0; --n) {
        if (here >= 0 && field->open(here)) {
            if (!play_started) play_started = std::chrono::steady_clock::now();
            std::fprintf(stdout, "{\"event\":\"minefield_sphere\",\"open\":[%d,%d],\"state\":%d}\n", x, y,
                         static_cast<int>(field->state()));
            std::fflush(stdout);
        }
    }
    // Every uncovered cell is cleared on the board. At the end (the owner's
    // design): won, every mine is marked green; lost, red, and half a second
    // later every other cube goes. The mines stay as cubes under their marks:
    // a board with no cube left counts as the core opened, and the stage
    // ends. (The two turns of the sphere that followed are gone: a smaller
    // field stops the cursor at its edge; the endings are to be designed.)
    const bool over = field->state() == Minefield::State::Lost || field->state() == Minefield::State::Won;
    if (over) ++over_frames;
    if (over && !play_ended) play_ended = std::chrono::steady_clock::now();
    const bool clear_rest = field->state() == Minefield::State::Lost && over_frames > 30;
    for (int c = 0; c < field->size(); ++c) {
        const auto& cell = field->cell(c);
        const bool clear = !field->wall(c) && (cell.open && !cell.mine ? true : clear_rest && !cell.mine);
        if (clear && !shown_open[std::size_t(c)]) {
            const int bx = origin_x + c % field_width, by = origin_y + c / field_width;
            clear_cell(rdram, bx, by);
            // The core beneath shows the mines round it.
            guest::store16(rdram, guest::load32(rdram, values_pointer) + 2u * std::uint32_t(cell_index(bx, by)),
                           static_cast<std::int16_t>(first_picture_value + cell.count));
            shown_open[std::size_t(c)] = true;
        }
    }
    // The cursor (the owner's report, 2026-11-12: the game's shadow follows
    // the stage's pieces, so it is hidden and the cell itself shows it): an
    // uncovered cell under it shows its highlighted tile, a cube turns
    // lavender.
    {
        const std::uint32_t values = guest::load32(rdram, values_pointer);
        const int here_cell = cell_index(x, y);
        const int under = here;
        const bool open_here = under >= 0 && field->cell(under).open && !field->cell(under).mine;
        if (cursor_cell >= 0 && cursor_cell != here_cell && cursor_field >= 0 && field->cell(cursor_field).open)
            guest::store16(rdram, values + 2u * std::uint32_t(cursor_cell),
                           static_cast<std::int16_t>(first_picture_value + field->cell(cursor_field).count));
        cursor_cell = here_cell;
        cursor_field = under;
        if (open_here) guest::store16(rdram, values + 2u * std::uint32_t(here_cell), plain_value);
        write_core_digits(rdram, open_here && !over ? field->cell(under).count : -1);
        if (open_here && over)
            guest::store16(rdram, values + 2u * std::uint32_t(here_cell),
                           static_cast<std::int16_t>(first_picture_value + field->cell(under).count));
    }
    // The cubes' colours: flags yellow (under the cursor too, so a flag shows
    // at once: the owner's call), the cursor's lavender (red over a wall); at the end the
    // mines green (won) or red (lost).
    {
        constexpr int yellow = 32, green = 34, lavender = 36, red = 37;
        const bool won = field->state() == Minefield::State::Won;
        for (int c = 0; c < field->size(); ++c) {
            const auto& cell = field->cell(c);
            const bool cursor = c == under_cursor && !over;
            // Over a wall the cursor shows red: nothing can be done there.
            const int colour = over && cell.mine ? (won ? green : red)
                               : cell.flag ? yellow
                               : cursor ? (field->wall(c) ? red : lavender) : 0;
            cube_colours()[cell_index(origin_x + c % field_width, origin_y + c / field_width)].store(colour, std::memory_order_relaxed);
        }
    }
}

bool hide_cursor_marker() { return active_flag.load(); }

std::vector<Mark> marks(const std::uint8_t* rdram) {
    std::vector<Mark> out;
    // The marks are drawn on the cells themselves now (blockplanet_minefield_
    // marks); BLOCKPLANET_MINEFIELD_FLOATING=1 shows the HUD's floating ones
    // as well (the first version, kept for comparison).
    static const bool floating = [] { const char* v = std::getenv("BLOCKPLANET_MINEFIELD_FLOATING"); return v && v[0] == '1'; }();
    if (!floating) return out;
    if (rdram == nullptr || !active_flag.load()) return out;
    std::lock_guard lock(mutex);
    if (!field) return out;
    const int x0 = cursor_x(rdram), y0 = cursor_y(rdram);
    // Where the sphere is turned to, in cells, following its smooth turn:
    // its angles (+0x2B42, +0x2B46; 8,192 a turn, so 256 a cell) ease
    // towards 128 + 256 x the cursor's values (measured 2026-11-12).
    const auto view = [&](std::uint32_t angle, int cursor) {
        float v = (float(guest::load16(rdram, player1 + angle)) - 128.0f) / 256.0f;
        // The angle wraps every 32 cells: keep it next to the cursor.
        while (v - float(cursor) > 16.0f) v -= 32.0f;
        while (v - float(cursor) < -16.0f) v += 32.0f;
        return v;
    };
    const float vx = view(0x2B42, x0 - 8), vy = view(0x2B46, y0 - 8);
    const bool over = field->state() == Minefield::State::Lost || field->state() == Minefield::State::Won;
    // Only the side facing the camera: up to 6 cells from the cursor.
    for (int dy = -7; dy <= 7; ++dy)
        for (int dx = -7; dx <= 7; ++dx) {
            const int c = field_at(x0 + dx, y0 + dy);
            if (c < 0) continue;
            const auto& cell = field->cell(c);
            Mark m;
            // The board's x runs right to left on screen (moving right lowers it).
            m.dx = -(float(x0 - 8 + dx) - vx);
            m.dy = float(y0 - 8 + dy) - vy;
            // Only cells on the side facing the camera (and not at its very edge).
            constexpr float cell_angle = 6.2831853f / 32.0f;
            if (std::cos(m.dx * cell_angle) * std::cos(m.dy * cell_angle) < 0.25f) continue;
            if (cell.mine && (over || cell.open)) {
                m.text = "\uE2FF";  // the icon font's bomb
                m.kind = c == field->exploded() ? "hit" : (field->state() == Minefield::State::Won ? "found" : "mine");
            } else if (cell.flag) {
                m.text = "\uE0D6";  // the icon font's flag
                m.kind = "flag";
            } else if (cell.open && cell.count > 0) {
                m.text = std::to_string(cell.count);
                m.kind = "n" + std::to_string(cell.count);
            } else {
                continue;
            }
            out.push_back(std::move(m));
        }
    return out;
}

bool take_exit_request() { return exit_requested.exchange(false); }

std::optional<Progress> progress() {
    if (!active_flag.load()) return std::nullopt;
    std::lock_guard lock(mutex);
    if (!field) return std::nullopt;
    Progress p;
    p.arena = field_arena;
    p.started = play_started.has_value();
    if (play_started)
        p.seconds = std::chrono::duration<double>(play_ended.value_or(std::chrono::steady_clock::now()) - *play_started).count();
    p.won = field->state() == Minefield::State::Won;
    p.lost = field->state() == Minefield::State::Lost;
    return p;
}

std::string counter() {
    if (!active_flag.load()) return {};
    std::lock_guard lock(mutex);
    if (!field) return {};
    switch (field->state()) {
    case Minefield::State::Won: return "Cleared";
    case Minefield::State::Lost: return "Boom";
    default: return std::to_string(field->flags()) + " / " + std::to_string(field->mines());
    }
}

} // namespace tetrisphere::minefield_sphere

extern "C" bool blockplanet_minefield_hide_marker(std::uint8_t*, recomp_context*) {
    return tetrisphere::minefield_sphere::hide_cursor_marker();
}

// The view centred on the cursor's cell (the owner's report, 2026-11-13): the
// game eases its angles (+0x2B40, +0x2B44; 256 a cell) to 128 + 256 x the
// cursor's cell, which puts the corner where four cells meet in the middle of
// the screen. The copy the drawing reads (+0x2B48, +0x2B4C), made just before
// this hook in func_800CD3B8, is moved on half a cell (+128: checked against
// -128 and 0 in play, 2026-11-13). Player 1 only.
// BLOCKPLANET_MINEFIELD_CENTRE=n tries another offset.
extern "C" void blockplanet_minefield_centre_view(std::uint8_t* rdram, recomp_context* ctx) {
    if (!tetrisphere::minefield_sphere::active()) return;
    const std::uint32_t context = static_cast<std::uint32_t>(tetrisphere::guest::load32(rdram, static_cast<std::uint32_t>(ctx->r19)));
    if (context != 0x80110220u) return;
    static const int offset = [] { const char* v = std::getenv("BLOCKPLANET_MINEFIELD_CENTRE"); return v ? std::atoi(v) : 128; }();
    for (std::uint32_t field : {0x2B48u, 0x2B4Cu})
        tetrisphere::guest::store32(rdram, context + field,
                                    tetrisphere::guest::load32(rdram, context + field) + static_cast<std::uint32_t>(offset));
}
