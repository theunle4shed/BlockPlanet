// The menu grid (ui_grid.h, grid_texture.h): the texture reader with made-up
// archive data (no ROM is needed), the tile layout, and colour changes with
// RmlUi and a renderer that draws nothing.
#include "grid_texture.h"
#include "ui_grid.h"

#include <RmlUi/Core.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace blockplanet::ui;

namespace {

class NullRenderer final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& size, const Rml::String&) override { size = {384, 96}; return 1; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

class QuietSystem final : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type, const Rml::String&) override { return true; }
};

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) { b[at] = std::uint8_t(v >> 8); b[at + 1] = std::uint8_t(v); }
void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) { put16(b, at, v >> 16); put16(b, at + 2, v & 0xFFFF); }

// An archive at `at` with one entry: pass J, the given SQSH blocks.
void archive(std::vector<std::uint8_t>& rom, std::size_t at, const std::vector<std::uint8_t>& blocks) {
    std::vector<std::uint8_t> entry = {1, 'J', 0, 0, 0, 0};
    put32(entry, 2, std::uint32_t(blocks.size()));
    entry.insert(entry.end(), blocks.begin(), blocks.end());
    const std::size_t table = 2 + 2 * 4;
    if (rom.size() < at + table + entry.size()) rom.resize(at + table + entry.size());
    put16(rom, at, 1);
    put32(rom, at + 2, std::uint32_t(table));
    put32(rom, at + 6, std::uint32_t(table + entry.size()));
    std::copy(entry.begin(), entry.end(), rom.begin() + std::ptrdiff_t(at + table));
}

std::vector<std::uint8_t> block(std::uint32_t unpacked, const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> b = {'S', 'Q', 'S', 'H', 0, 0, 0, 0, 0, 0, 0, 0};
    put32(b, 4, unpacked);
    put32(b, 8, std::uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

bool near(float a, float b, float tolerance = 0.01f) { return std::abs(a - b) <= tolerance; }
bool same(GridRgb a, GridRgb b) { return near(a.r, b.r, 1e-6f) && near(a.g, b.g, 1e-6f) && near(a.b, b.b, 1e-6f); }

void texture_tests() {
    // Pass J: three literal halfwords, then three copied from index 0.
    std::vector<std::uint8_t> rom;
    archive(rom, 0, block(12, {0x02, 1, 2, 3, 4, 5, 6, 0x82, 0, 0}));
    auto out = unpack_archive_entry(rom, 0, 0);
    assert(out && (*out == std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6}));
    // Stored blocks (equal sizes) are copied, and blocks follow one another.
    auto two = block(4, {9, 8, 7, 6});
    auto j = block(4, {0x00, 1, 2, 0x80, 0, 0});
    two.insert(two.end(), j.begin(), j.end());
    rom.clear();
    archive(rom, 0, two);
    out = unpack_archive_entry(rom, 0, 0);
    assert(out && (*out == std::vector<std::uint8_t>{9, 8, 7, 6, 1, 2, 1, 2}));
    // A copy from beyond what is unpacked, a missing entry, a cut-off ROM.
    rom.clear();
    archive(rom, 0, block(8, {0x00, 1, 2, 0x81, 0, 5}));
    assert(!unpack_archive_entry(rom, 0, 0));
    assert(!unpack_archive_entry(rom, 0, 1));
    assert(!unpack_archive_entry(std::vector<std::uint8_t>(rom.begin(), rom.begin() + 12), 0, 0));
    assert(!unpack_archive_entry({}, 0, 0));

    // The marble: from the main archive when it holds 160 x 120 bytes.
    assert(!marble_from_rom({}));
    std::vector<std::uint8_t> pixels(160 * 120);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = std::uint8_t(i * 7);
    std::vector<std::uint8_t> big(0x736C50);
    archive(big, 0x736C50, block(std::uint32_t(pixels.size()), pixels));
    const auto marble = marble_from_rom(big);
    assert(marble && marble->width == 160 && marble->height == 120 && marble->pixels == pixels);
    // The wrong size is not the marble.
    std::vector<std::uint8_t> other(0x736C50);
    archive(other, 0x736C50, block(100, std::vector<std::uint8_t>(100, 1)));
    assert(!marble_from_rom(other));

    // The procedural marble varies; the atlas has a bevel and a middle near 128.
    const auto plain = procedural_marble();
    assert(plain.width == 160 && plain.height == 120 && plain.pixels.size() == 160u * 120u);
    assert(*std::min_element(plain.pixels.begin(), plain.pixels.end()) + 40 < *std::max_element(plain.pixels.begin(), plain.pixels.end()));
    const auto atlas = make_tile_atlas(plain, 96, 4);
    assert(atlas.width == 384 && atlas.height == 96 && atlas.rgba.size() == 384u * 96u * 4u);
    const auto grey = [&](int x, int y) { return int(atlas.rgba[(std::size_t(y) * atlas.width + x) * 4]); };
    assert(grey(1, 48) > grey(48, 48) && grey(48, 1) > grey(48, 48));    // light top and left
    assert(grey(94, 48) < grey(48, 48) && grey(48, 94) < grey(48, 48));  // dark bottom and right
    long sum = 0;
    for (int y = 10; y < 86; ++y)
        for (int x = 10; x < 86; ++x) sum += grey(x, y);
    const double middle = double(sum) / (76.0 * 76.0);
    assert(middle > 105 && middle < 150);
    assert(make_tile_atlas(GreyImage{}, 96, 4).rgba.empty());

    // Without a ROM the grid's texture is the procedural one.
    assert(!grid_tile_atlas().rgba.empty() && !grid_texture_from_rom());
}

void layout_tests() {
    const auto check = [](float w, float h) {
        const auto layout = grid_layout(w, h);
        assert(layout.columns == 8 && layout.rows == 8 && layout.tiles.size() == 64u);
        const float side = std::min(w, h);
        const auto at = [&](int column, int row) -> const GridTile& { return layout.tiles[std::size_t(row * 8 + column)]; };
        const auto corners = [&](const GridTile& t) { return *grid_tile_corners(layout, t, 0.0f, 0.0f); };
        for (const auto& t : layout.tiles) {
            assert(t.lon0 < t.lon && t.lon < t.lon1 && t.lat0 < t.lat && t.lat < t.lat1 && t.width > 0);
            assert(t.shade > 0.4f && t.shade < 1.2f && t.variant >= 0 && t.variant < 4);
            // Symmetric: mirrored left to right and top to bottom.
            const auto c = corners(t), m = corners(at(7 - t.column, t.row)), v = corners(at(t.column, 7 - t.row));
            assert(near(c[0].x - layout.centre_x, layout.centre_x - m[1].x, 0.05f) && near(c[0].y, m[1].y, 0.05f));
            assert(near(c[0].y - layout.centre_y, layout.centre_y - v[3].y, 0.05f) && near(c[0].x, v[3].x, 0.05f));
            // Corners in order around the tile (top left, top right, bottom right, bottom left).
            assert(c[0].x < c[1].x && c[3].x < c[2].x && c[0].y < c[3].y && c[1].y < c[2].y);
        }
        // Sized by the shorter side (the owner's note): there the middle of
        // the outermost tiles is at the screen's edge, so they are cut in
        // half; the same reach across and down (round); along the longer side
        // the far background shows beyond the grid.
        const auto middle = [&](const GridTile& t) {
            const auto c = corners(t);
            return GridPoint{(c[0].x + c[1].x + c[2].x + c[3].x) / 4, (c[0].y + c[1].y + c[2].y + c[3].y) / 4};
        };
        const float mid_x = middle(at(7, 3)).x - layout.centre_x, mid_y = middle(at(3, 7)).y - layout.centre_y;
        assert(std::abs(mid_x - mid_y) < 0.5f);
        assert(mid_x > side / 2 * 0.97f && mid_x < side / 2 * 1.03f);
        float reach = 0;
        for (const auto& t : layout.tiles)
            for (const auto& p : corners(t)) reach = std::max(reach, std::abs(p.x - layout.centre_x));
        assert(reach > side / 2 + 5);  // the outermost tiles run past the shorter side's edges
        if (w > h) assert(reach < w / 2 - 10);
        if (h > w) assert(reach < h / 2 - 10);
        // One surface: along a row, each tile's top edge carries on into the
        // next one's across the gap, with no step (the owner's note).
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 7; ++column) {
                const auto l = corners(at(column, row)), r = corners(at(column + 1, row));
                for (const auto& [i0, i1] : {std::pair{0, 1}, std::pair{3, 2}}) {  // top edges, bottom edges
                    const float edge = std::atan2(l[i1].y - l[i0].y, l[i1].x - l[i0].x);
                    const float across = std::atan2(r[i0].y - l[i1].y, r[i0].x - l[i1].x);
                    const float next = std::atan2(r[i1].y - r[i0].y, r[i1].x - r[i0].x);
                    // Bending steadily, with no step: the gap's direction lies
                    // between the two edges' (as along one smooth curve).
                    assert(std::abs(next - edge) < 0.5f);
                    assert(std::abs(across - (edge + next) / 2) <= std::abs(next - edge) / 2 + 0.005f);
                }
            }
        }
        // The same down each column.
        for (int column = 0; column < 8; ++column) {
            for (int row = 0; row < 7; ++row) {
                const auto u = corners(at(column, row)), d = corners(at(column, row + 1));
                const float edge = std::atan2(u[3].x - u[0].x, u[3].y - u[0].y);
                const float across = std::atan2(d[0].x - u[3].x, d[0].y - u[3].y);
                const float next = std::atan2(d[3].x - d[0].x, d[3].y - d[0].y);
                assert(std::abs(next - edge) < 0.5f);
                assert(std::abs(across - (edge + next) / 2) <= std::abs(next - edge) / 2 + 0.005f);
            }
        }
        // Rows bow as much as columns (the owner's note): on a square screen
        // the grid is the same turned on its side (tile c, r is tile r, c
        // with across and down swapped).
        if (w == h) {
            for (const auto& t : layout.tiles) {
                const auto c = corners(t), u = corners(at(t.row, t.column));
                assert(near(c[0].x - layout.centre_x, u[0].y - layout.centre_y, 0.05f));
                assert(near(c[1].x - layout.centre_x, u[3].y - layout.centre_y, 0.05f));
            }
            // And a row away from the middle bows: its ends lower than its middle.
            const auto end = corners(at(0, 1))[0], mid = corners(at(3, 1))[1];
            assert(end.y > mid.y + 5);
        }
        // Lifting moves a tile outwards (away from the middle); turning narrows it.
        const auto& t = at(6, 2);
        const auto base = corners(t);
        const auto lifted = *grid_tile_corners(layout, t, 20.0f, 0);
        assert(lifted[0].x > base[0].x && lifted[0].y < base[0].y);
        const auto turned = *grid_tile_corners(layout, t, 0, 60.0f);
        assert(turned[1].x - turned[0].x < base[1].x - base[0].x);
        assert(grid_quad_matrix(*grid_tile_corners(layout, t, 0, 90.0f), 128).has_value());  // edge on is held short of it
        // Flying out: towards the viewer (larger, further from the middle),
        // then past them (nothing to draw).
        const auto half_away = *grid_tile_corners(layout, t, 0, 0, 0.5f);
        assert(half_away[1].x - half_away[0].x > base[1].x - base[0].x);
        assert(std::abs(half_away[0].x - layout.centre_x) > std::abs(base[0].x - layout.centre_x));
        assert(!grid_tile_corners(layout, t, 0, 0, 1.0f));
        // Flying, no tile grows to cover much of the screen (straight at the
        // viewer, several together read as a flat dark screen): at most a
        // tenth of it, counting only what is on screen.
        const auto on_screen = [&](const std::array<GridPoint, 4>& q) {
            int hit = 0, n = 0;
            for (float y = 6; y < h; y += 12) {
                for (float x = 6; x < w; x += 12) {
                    ++n;
                    bool in = false;
                    for (std::size_t i = 0, j = 3; i < 4; j = i++)
                        if ((q[i].y > y) != (q[j].y > y) && x < (q[j].x - q[i].x) * (y - q[i].y) / (q[j].y - q[i].y) + q[i].x) in = !in;
                    hit += in;
                }
            }
            return float(hit) / float(n);
        };
        for (std::size_t i = 0; i < layout.tiles.size(); i += 9)
            for (float away = 0; away < 1.0f; away += 0.1f)
                if (const auto q = grid_tile_corners(layout, layout.tiles[i], 0, 0, away)) assert(on_screen(*q) < 0.1f);
        for (const auto& u : layout.tiles) assert(u.fly >= 0 && u.fly <= 1.0001f);
        assert(at(3, 3).fly > at(0, 0).fly);  // the middle arrives last (and leaves first)
        return layout;
    };
    const auto square = check(1080, 1080);
    const auto wide = check(1920, 1080);
    check(2560, 1080);
    check(1440, 1080);
    const auto tall = check(1080, 1920);
    // The same sphere for the same shorter side, whatever the longer; never stretched.
    const auto big = check(1920, 1920);
    assert(near(wide.radius, square.radius, 0.01f) && near(tall.radius, square.radius, 0.01f) && square.radius < big.radius);

    // The matrix carries the element's square onto the quad, in perspective.
    const std::array<GridPoint, 4> quad = {GridPoint{100, 50}, GridPoint{300, 70}, GridPoint{280, 260}, GridPoint{90, 240}};
    const auto m = grid_quad_matrix(quad, 128);
    assert(m);
    const auto project = [&](float x, float y) {
        const float X = (*m)[0] * x + (*m)[4] * y + (*m)[12], Y = (*m)[1] * x + (*m)[5] * y + (*m)[13];
        const float W = (*m)[3] * x + (*m)[7] * y + (*m)[15];
        return GridPoint{X / W, Y / W};
    };
    const GridPoint unit[4] = {{0, 0}, {128, 0}, {128, 128}, {0, 128}};
    for (int k = 0; k < 4; ++k) {
        const auto p = project(unit[k].x, unit[k].y);
        assert(near(p.x, quad[std::size_t(k)].x, 0.05f) && near(p.y, quad[std::size_t(k)].y, 0.05f));
    }
    assert(!grid_quad_matrix({GridPoint{0, 0}, GridPoint{0, 0}, GridPoint{0, 0}, GridPoint{0, 0}}, 128));
    // A quad folded by perspective (depth w crossing zero inside it, which a
    // GPU stretches across the screen) gets no matrix: every matrix keeps w
    // positive at the element's corners. On the water, every tile at rest
    // still gets one, folded or not (flat when the water folds it).
    {
        const auto layout = grid_layout(1280, 720);
        const auto positive = [](const std::array<float, 16>& m) {
            for (float x : {0.0f, 128.0f})
                for (float y : {0.0f, 128.0f})
                    if (m[3] * x + m[7] * y + m[15] <= 0.0f) return false;
            return true;
        };
        for (double time = 812345.0; time < 812348.0; time += 0.1) {
            for (const auto& t : layout.tiles) {
                const float u = float(t.column), v = float(t.row);
                const std::array<float, 4> lifts = {grid_water(u, v, time, t.width), grid_water(u + 1, v, time, t.width),
                                                    grid_water(u + 1, v + 1, time, t.width), grid_water(u, v + 1, time, t.width)};
                const auto water = grid_quad_matrix(*grid_tile_corners(layout, t, lifts, 0.0f), 128);
                if (water) assert(positive(*water));
                const auto flat = grid_quad_matrix(*grid_tile_corners(layout, t, 0.0f, 0.0f), 128);
                assert(flat && positive(*flat));
            }
        }
        for (float turn = 0; turn < 90; turn += 3)
            for (const auto& t : layout.tiles)
                if (const auto m = grid_quad_matrix(*grid_tile_corners(layout, t, 0.0f, turn), 128)) assert(positive(*m));
    }
    assert(grid_layout(0, 0).tiles.empty());

    // The water: tiles rise and sink (both ways) by up to the wave's depth;
    // further along the wave they are out of step; a tile's corners differ
    // (it tilts with the water); with no cross ripple it repeats every wave.
    const float width = wide.tiles[0].width;
    float high_water = 0, low_water = 0;
    bool apart = false, tilts = false;
    for (double time = 0; time < 3 * grid_wave_seconds; time += 0.02) {
        const float h0 = grid_water(0, 0, time, width);
        assert(std::abs(h0) <= grid_wave_depth * width + 0.01f);
        high_water = std::max(high_water, h0);
        low_water = std::min(low_water, h0);
        if (std::abs(h0 - grid_water(3, 3, time, width)) > 1.0f) apart = true;
        if (std::abs(h0 - grid_water(1, 0, time, width)) > 0.5f) tilts = true;
    }
    assert(high_water > grid_wave_depth * width * 0.6f && low_water < -grid_wave_depth * width * 0.6f && apart && tilts);
    grid_tuning().ripple = 0;
    assert(near(grid_water(2, 5, 1.0, width), grid_water(2, 5, 1.0 + grid_wave_seconds, width), 0.01f));
    grid_tuning() = GridTuning{};

    // Independence: none, a tile's corners follow the water exactly; all,
    // tiles on the same diagonal (in step on the water) bob apart, each
    // keeping to the wave's depth.
    {
        const auto& a = wide.tiles[std::size_t(2 * 8 + 3)];  // column 3, row 2
        const auto& b = wide.tiles[std::size_t(3 * 8 + 2)];  // column 2, row 3: the same diagonal
        grid_tuning().independence = 0.0f;
        const auto plain = grid_tile_lifts(a, 3.3);
        assert(near(plain[0], grid_water(3, 2, 3.3, a.width), 0.001f) && near(plain[2], grid_water(4, 3, 3.3, a.width), 0.001f));
        grid_tuning().independence = 1.0f;
        bool differ = false;
        for (double time = 0; time < 6; time += 0.1) {
            const auto la = grid_tile_lifts(a, time), lb = grid_tile_lifts(b, time);
            for (std::size_t k = 0; k < 4; ++k) assert(std::abs(la[k]) <= grid_wave_depth * a.width * 2.0f + 0.01f);
            if (std::abs(la[0] - lb[0]) > 2.0f) differ = true;
        }
        assert(differ);
        grid_tuning() = GridTuning{};
    }

    // By default every tile keeps one clock (as the original's do): its
    // corners repeat exactly every wave period, whatever its own phase.
    for (const auto& t : wide.tiles) {
        const auto now = grid_tile_lifts(t, 7.3), later = grid_tile_lifts(t, 7.3 + double(grid_wave_seconds));
        for (std::size_t k = 0; k < 4; ++k) assert(near(now[k], later[k], 0.05f));
    }

    // The test screen's settings: each slider's value, in its own units,
    // matching the defaults, and setting one changes the tuning.
    {
        const auto values = grid_lab_values();
        assert(values.size() == 9);  // the water's and the flight's settings
        const auto value_of = [&](const char* name) {
            for (const auto& [n, v] : values) if (n == name) return v;
            return std::string("?");
        };
        assert(value_of("grid_wave_every") == "1.43" && value_of("grid_wave_step") == "350");
        assert(value_of("grid_wave_depth") == "6" && value_of("grid_ripple") == "0" && value_of("grid_speed_variation") == "0");
        assert(grid_lab_set("grid_ripple", "50") && near(grid_tuning().ripple, 0.5f, 1e-6f));
        assert(grid_lab_set("grid_wave_step", "100") && near(grid_tuning().wave_step, 0.1f, 1e-6f));
        assert(!grid_lab_set("grid_nothing", "1"));
        grid_tuning() = GridTuning{};
    }
    assert(grid_colour("blue") && grid_colour("green") && grid_colour("red") && grid_colour("purple") && grid_colour("gold") &&
           grid_colour("grey") && grid_colour("cyan") && grid_colour("teal") && grid_colour("peach"));
    assert(!grid_colour("") && !grid_colour("pink") && !grid_colour("pause"));
    const auto blue = *grid_colour("blue");
    assert(near(blue.r, 0x0d / 255.0f) && near(blue.g, 0x15 / 255.0f) && near(blue.b, 0x1f / 255.0f));
    const auto far = grid_far_colour(blue);
    assert(far.r < blue.r && far.g < blue.g && far.b < blue.b);
    const auto cycle = grid_cycle("pause");
    assert(cycle.size() == 5 && cycle[0] == "pause-purple");
    for (auto name : cycle) assert(grid_colour(name));
    assert(grid_cycle("blue").empty());
    // Smooth: each colour in turn, blended in between, back to the first.
    const auto purple = *grid_colour("pause-purple"), pblue = *grid_colour("pause-blue");
    assert(same(grid_cycle_colour(cycle, 0), purple) && same(grid_cycle_colour(cycle, grid_cycle_seconds), pblue));
    assert(same(grid_cycle_colour(cycle, 5 * grid_cycle_seconds), purple));
    const auto half = grid_cycle_colour(cycle, grid_cycle_seconds / 2);
    assert(near(half.r, (purple.r + pblue.r) / 2, 0.002f) && near(half.b, (purple.b + pblue.b) / 2, 0.002f));
    for (double time = 0; time < 5 * grid_cycle_seconds; time += 0.1) {
        const auto a = grid_cycle_colour(cycle, time), b = grid_cycle_colour(cycle, time + 0.1);
        assert(std::abs(a.r - b.r) < 0.003f && std::abs(a.g - b.g) < 0.003f && std::abs(a.b - b.b) < 0.003f);
    }
}

// Turns, as seen: the same flip modulo half a turn (edge-on either way looks the same).
float turn_step(float before, float after) {
    float d = std::fmod(after - before + 270.0f, 180.0f);
    if (d < 0) d += 180.0f;
    return d - 90.0f;
}

void grid_tests(const std::string& document) {
    NullRenderer renderer;
    QuietSystem system;
    Rml::SetRenderInterface(&renderer);
    Rml::SetSystemInterface(&system);
    assert(Rml::Initialise());
    Rml::Context* context = Rml::CreateContext("test", Rml::Vector2i(1440, 1080));
    {
        // Which colour the open screens ask for.
        auto* main = context->LoadDocumentFromMemory("<rml><body data-grid='blue'/></rml>");
        auto* solo = context->LoadDocumentFromMemory("<rml><body data-grid='green'/></rml>");
        auto* confirm = context->LoadDocumentFromMemory("<rml><body/></rml>");
        auto* game = context->LoadDocumentFromMemory("<rml><body/></rml>");
        auto* pause = context->LoadDocumentFromMemory("<rml><body data-grid='pause'/></rml>");
        assert(grid_colour_of({}).empty());
        assert(grid_colour_of({main}) == "blue");
        assert(grid_colour_of({main, solo}) == "green");
        assert(grid_colour_of({main, solo, confirm}) == "green");  // keeps the colour under it
        assert(grid_colour_of({game}).empty());                   // a screen without one first: none
        assert(grid_colour_of({game, main}).empty());
        assert(grid_colour_of({pause, confirm}) == "pause");
        assert(grid_colour_of({pause, main}) == "blue");

        MenuGrid grid(document);
        // Frames at 60 a second from `from` to `to`.
        const auto run = [&](MenuGrid& g, const char* name, double from, double to, bool cover = false) {
            for (double v = from; v <= to + 1e-9; v += 1.0 / 60.0) g.update(context, name, v, cover);
        };
        grid.update(context, "", 0.0);
        assert(!grid.visible());
        grid.update(context, "blue", 0.0);
        context->Update();
        const auto blue = *grid_colour("blue"), green = *grid_colour("green");
        assert(grid.visible() && grid.tile_count() == 64 && !grid.changing());
        assert(same(grid.tile_colour(0), blue) && same(grid.tile_colour(63), blue));
        grid.update(context, "blue", 0.5);  // the same colour: no flips
        assert(!grid.changing() && grid.tile_turn(0) == 0);

        // A new colour: the tiles flip one after another.
        grid.update(context, "green", 1.0);
        assert(grid.changing());
        grid.update(context, "green", 1.0 + 0.01);
        assert(same(grid.tile_colour(0), blue));
        grid.update(context, "green", 1.0 + grid_flip_seconds * 0.6);
        context->Update();
        assert(same(grid.tile_colour(0), green) && same(grid.tile_colour(63), blue));
        grid.update(context, "green", 1.0 + grid_fade_seconds + 0.01);
        assert(!grid.changing() && same(grid.tile_colour(0), green) && same(grid.tile_colour(63), green));

        // Back and forth quickly (the owner's report): every tile ends on the
        // newest colour, nothing waits for a ripple to finish, and no tile
        // jumps between frames.
        std::uint32_t seed = 7;
        double t = 10.0;
        int c = 1;  // green
        const GridRgb colours[2] = {blue, green};
        for (int round = 0; round < 60; ++round) {
            std::vector<float> turns(grid.tile_count());
            for (std::size_t i = 0; i < turns.size(); ++i) turns[i] = grid.tile_turn(i);
            const int presses = 1 + int((seed >> 8) % 6);
            for (int k = 0; k < presses; ++k) {
                seed = seed * 1103515245u + 12345u;
                c ^= 1;
                const int frames = 1 + int((seed >> 16) % 25);  // 1 to 25 frames apart
                for (int f = 0; f < frames; ++f) {
                    t += 1.0 / 60.0;
                    grid.update(context, c ? "green" : "blue", t);
                    for (std::size_t i = 0; i < turns.size(); ++i) {
                        assert(std::abs(turn_step(turns[i], grid.tile_turn(i))) < 30.0f);
                        turns[i] = grid.tile_turn(i);
                    }
                }
            }
            // Settled within two ripples of the last press, on the newest colour.
            for (int f = 0; f < 60 * 2; ++f) {
                t += 1.0 / 60.0;
                grid.update(context, c ? "green" : "blue", t);
            }
            assert(!grid.changing());
            for (std::size_t i = 0; i < grid.tile_count(); ++i)
                assert(same(grid.tile_colour(i), colours[c]) && grid.tile_turn(i) == 0);
        }

        // The pause's cycle: purple at first, fading with every tile in step
        // (no flips), and a flip ripple when it comes from another colour.
        run(grid, "", 900.0, 901.0);
        assert(!grid.visible());
        run(grid, "pause", 1000.0, 1000.0);
        const auto cycle = grid_cycle("pause");
        assert(same(grid.tile_colour(20), *grid_colour("pause-purple")) && !grid.changing());
        for (double u = 1000.0; u < 1000.0 + grid_cycle_seconds * 1.5; u += 1.0 / 60.0) {
            grid.update(context, "pause", u);
            assert(!grid.changing() && grid.tile_turn(20) == 0);
            assert(same(grid.tile_colour(0), grid_cycle_colour(cycle, u - 1000.0)) && same(grid.tile_colour(63), grid.tile_colour(0)));
        }
        grid.update(context, "blue", 1020.0);  // Options from the pause
        grid.update(context, "blue", 1022.0);
        grid.update(context, "pause", 1030.0);  // back: a ripple into the cycle
        assert(grid.changing());
        double u = 1030.0;
        for (; u < 1032.0; u += 1.0 / 60.0) grid.update(context, "pause", u);
        assert(!grid.changing());
        for (std::size_t i = 0; i < grid.tile_count(); ++i) assert(same(grid.tile_colour(i), grid_cycle_colour(cycle, u - 1.0 / 60.0 - 1030.0)));

        // Hidden, then shown again: straight in its colour, no flip.
        run(grid, "", 1100.0, 1101.0);
        assert(!grid.visible());
        grid.update(context, "blue", 1101.0);
        assert(grid.visible() && !grid.changing() && same(grid.tile_colour(30), blue));

        // Flying in: from nothing to every tile in place over grid_fly_total;
        // out again, the same way back, then hidden.
        assert(grid.presence() == 0.0f);
        run(grid, "blue", 1101.0, 1101.0 + grid_fly_total / 2);
        assert(grid.presence() > 0.3f && grid.presence() < 0.7f);
        run(grid, "blue", 1101.0 + grid_fly_total / 2, 1102.5);
        assert(grid.presence() == 1.0f && grid.cover_allowance() == 1.0f);
        run(grid, "", 1102.5, 1102.5 + grid_fly_total / 4);
        assert(grid.visible() && grid.presence() < 1.0f && grid.presence() > 0.5f);
        // A change of mind part way: back in from where the tiles are.
        const float part = grid.presence();
        grid.update(context, "blue", 1102.5 + grid_fly_total / 4 + 1.0 / 60.0);
        assert(grid.presence() > part && grid.presence() < part + 0.1f);
        run(grid, "blue", 1102.6, 1104.0);
        // Into the loading cover: the cover waits until the tiles have mostly passed.
        grid.update(context, "", 1104.0 + 1.0 / 60.0, true);
        assert(grid.cover_allowance() == 0.0f);
        float allowance = 0;
        for (double v = 1104.0 + 2.0 / 60.0; grid.visible(); v += 1.0 / 60.0) {
            grid.update(context, "", v, true);
            assert(grid.cover_allowance() >= allowance);
            allowance = grid.cover_allowance();
            assert(v < 1106.0);
        }
        // Held (as ui_system does while the game's own pause tiles animate):
        // the same colour keeps it up, then a screen's colour changes it.
        run(grid, "pause", 1110.0, 1112.0);
        run(grid, "pause", 1112.0, 1113.0);  // held: still asked for
        run(grid, "blue", 1113.0, 1115.0);
        assert(grid.visible() && grid.presence() == 1.0f && same(grid.tile_colour(5), blue));
        run(grid, "blue", 1115.0, 1115.0);

        // A wider window: the tiles are laid out again.
        context->SetDimensions(Rml::Vector2i(1920, 1080));
        grid.update(context, "blue", 1102.0);
        assert(grid.tile_count() == 64);
        context->Update();
        context->Render();

        // The test screen's backdrop: flown out, the document stays (black
        // behind), the grid is not visible, and it flies back in.
        grid.set_backdrop(true);
        run(grid, "", 1103.0, 1104.0);
        assert(!grid.visible());
        run(grid, "blue", 1104.0, 1105.0);
        assert(grid.visible() && grid.presence() == 1.0f);
        grid.set_backdrop(false);
        run(grid, "", 1105.0, 1106.0);
        assert(!grid.visible());

        // A missing document: no grid, and no crash.
        MenuGrid missing("no-such-grid.rml");
        missing.update(context, "blue", 0.0);
        assert(!missing.visible());
    }
    Rml::Shutdown();
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    texture_tests();
    layout_tests();
    grid_tests(argv[1]);
    return 0;
}
