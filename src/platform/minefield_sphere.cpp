#include "tetrisphere/minefield_sphere.h"
#include "tetrisphere/sphere_cell.h"

#include "tetrisphere/guest_memory.h"
#include "../ui/ui_renderer.h"
#include "stb_image.h"

#include <algorithm>
#include <atomic>
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
int origin_x = 0, origin_y = 0;  // the board cell of the field's (0, 0)
std::atomic<int> pending_open{0}, pending_flag{0};
std::uint16_t last_buttons = 0;

std::optional<Minefield> field;
int frames_in_play = 0;
bool built = false;
std::vector<bool> shown_open;  // cells already cleared on the board
int over_frames = 0;           // frames since the field was won or lost

int cell_index(int x, int y) { return ((y % board + board) % board) * board + ((x % board + board) % board); }

// The field cell (index) at a board cell, or -1 outside the field.
int field_at(int bx, int by) {
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
constexpr int core_tile = 16;
constexpr int core_tiles = 9;
constexpr std::int16_t first_picture_value = 72;

struct CoreDigits {
    std::vector<std::uint16_t> palette;  // RGBA 16-bit
    std::vector<std::uint8_t> indices;   // core_tiles x 16 x 16, tile after tile
};

// Loads the sheet once: its colours become the palette (at most 256).
const std::optional<CoreDigits>& core_digits() {
    static const std::optional<CoreDigits> digits = []() -> std::optional<CoreDigits> {
        const auto path = blockplanet::ui::ui_asset_root() / "minefield" / "core_digits.png";
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (pixels == nullptr) return std::nullopt;
        CoreDigits out;
        bool fits = w >= core_tile * core_tiles && h >= core_tile;
        for (int k = 0; fits && k < core_tiles; ++k)
            for (int y = 0; fits && y < core_tile; ++y)
                for (int x = 0; x < core_tile; ++x) {
                    const stbi_uc* p = pixels + 4 * (y * w + k * core_tile + x);
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
void write_core_digits(std::uint8_t* rdram) {
    const auto& digits = core_digits();
    if (!digits) return;
    for (std::size_t i = 0; i < digits->palette.size(); ++i)
        guest::store16(rdram, picture_palette + 2u * std::uint32_t(i), static_cast<std::int16_t>(digits->palette[i]));
    // Core value 72 + k draws picture slot 3 x (k / 3) + 2 - k % 3 (each row of
    // three runs the other way; found by comparing the field with the core).
    constexpr std::size_t tile_bytes = core_tile * core_tile;
    for (int k = 0; k < core_tiles; ++k) {
        const int slot = 3 * (k / 3) + 2 - k % 3;
        for (std::size_t i = 0; i < tile_bytes; ++i)
            guest::store8(rdram, picture_tiles + std::uint32_t(slot * tile_bytes + i), digits->indices[std::size_t(k) * tile_bytes + i]);
    }
}

} // namespace

void arm(int width, int height, int mines) {
    std::lock_guard lock(mutex);
    requested_mines = mines;
    field_width = std::clamp(width, 4, board);
    field_height = std::clamp(height, 4, board);
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
    return static_cast<std::uint16_t>(buttons & ~(a | b));
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
        // Centred on the cursor's starting cell (8, 8); the whole board wraps.
        origin_x = 8 - field_width / 2;
        origin_y = 8 - field_height / 2;
        build_board(rdram);
        field.emplace(field_width, field_height, requested_mines.load(), std::random_device{}(),
                      field_width == board && field_height == board);
        shown_open.assign(layer_cells, false);
        built = true;
        active_flag = true;
        blue_field() = true;
        tetrisphere::sphere::fill_training_gaps() = false;
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
        for (int c = 0; c < layer_cells && !replaced; ++c) {
            const std::int16_t v = guest::load16(rdram, values + 2u * std::uint32_t(layer_cells + c));
            replaced = (v != 0 && v != inert) || guest::load16(rdram, values + 2u * std::uint32_t(2 * layer_cells + c)) != 0;
        }
        if (replaced) {
            build_board(rdram);
            shown_open.assign(layer_cells, false);
            std::fprintf(stdout, "{\"event\":\"minefield_sphere\",\"state\":\"rebuilt\"}\n");
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
    // The drop timer never runs out (+0x2B10 counts up to 600), and the
    // stage is never won: it ends when an S x S square of the core is
    // uncovered (S at +0x23E4, 4 to 8 in Rescue), so S is the whole board,
    // which the mines always keep covered. (Turning the goal off instead,
    // 0x800E44A4 = 0, stopped the holes being drawn.)
    guest::store16(rdram, player1 + 0x2B10, 0);
    guest::store16(rdram, player1 + 0x23E4, board);
    const int x = cursor_x(rdram), y = cursor_y(rdram);
    const int here = field_at(x, y);  // -1 on the wall: nothing happens
    for (int n = pending_flag.exchange(0); n > 0; --n) field->toggle_flag(here);
    for (int n = pending_open.exchange(0); n > 0; --n) {
        if (here >= 0 && field->open(here)) {
            std::fprintf(stdout, "{\"event\":\"minefield_sphere\",\"open\":[%d,%d],\"state\":%d}\n", x, y,
                         static_cast<int>(field->state()));
            std::fflush(stdout);
        }
    }
    // Every uncovered cell is cleared on the board. At the end (the owner's
    // design): won, every mine is marked green; lost, red, and half a second
    // later every other cube goes. The mines stay as cubes under their marks:
    // a board with no cube left counts as the core opened, and the stage
    // ends. Then the sphere turns round twice to show where they all were.
    const bool over = field->state() == Minefield::State::Lost || field->state() == Minefield::State::Won;
    if (over) ++over_frames;
    const bool clear_rest = field->state() == Minefield::State::Lost && over_frames > 30;
    for (int c = 0; c < field->size(); ++c) {
        const auto& cell = field->cell(c);
        const bool clear = cell.open && !cell.mine ? true : clear_rest && !cell.mine;
        if (clear && !shown_open[std::size_t(c)]) {
            const int bx = origin_x + c % field_width, by = origin_y + c / field_width;
            clear_cell(rdram, bx, by);
            // The core beneath shows the mines round it.
            guest::store16(rdram, guest::load32(rdram, values_pointer) + 2u * std::uint32_t(cell_index(bx, by)),
                           static_cast<std::int16_t>(first_picture_value + cell.count));
            shown_open[std::size_t(c)] = true;
        }
    }
    write_core_digits(rdram);
    // The cubes' colours: flags yellow; at the end the mines green (won) or
    // red (lost).
    {
        constexpr int yellow = 32, green = 34, red = 37;
        const bool won = field->state() == Minefield::State::Won;
        for (int c = 0; c < field->size(); ++c) {
            const auto& cell = field->cell(c);
            const int colour = over && cell.mine ? (won ? green : red) : cell.flag ? yellow : 0;
            cube_colours()[cell_index(origin_x + c % field_width, origin_y + c / field_width)].store(colour, std::memory_order_relaxed);
        }
    }
    // The two turns: the cursor moves a cell every third frame from a second
    // after the end, 64 cells (two turns of the 32-cell board); the game eases
    // the sphere after it.
    if (over && over_frames > 60 && over_frames <= 60 + 64 * 3 && over_frames % 3 == 0)
        guest::store16(rdram, player1 + 0x2B2A, static_cast<std::int16_t>(guest::load16(rdram, player1 + 0x2B2A) - 1));
}

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

std::string status() {
    if (!active_flag.load()) return {};
    std::lock_guard lock(mutex);
    return field ? minefield_status(*field) : std::string{};
}

std::string counter() {
    if (!active_flag.load()) return {};
    std::lock_guard lock(mutex);
    if (!field) return {};
    switch (field->state()) {
    case Minefield::State::Won: return "Cleared";
    case Minefield::State::Lost: return "Boom";
    default: return std::to_string(field->mines() - field->flags());
    }
}

} // namespace tetrisphere::minefield_sphere
