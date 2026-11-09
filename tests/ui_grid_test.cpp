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
        const float side = std::max(w, h);
        const auto at = [&](int column, int row) -> const GridTile& { return layout.tiles[std::size_t(row * 8 + column)]; };
        const auto corners = [&](const GridTile& t) { return grid_tile_corners(layout, t, 1.0f, 0.0f, 0.0f); };
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
        // Along the longer side the middle of the outermost tiles is seen at
        // the screen's edge, so they are cut about in half; round, so the
        // same along the other side.
        const auto middle = [&](const GridTile& t) {
            const auto c = corners(t);
            return GridPoint{(c[0].x + c[1].x + c[2].x + c[3].x) / 4, (c[0].y + c[1].y + c[2].y + c[3].y) / 4};
        };
        const float reach_x = middle(at(7, 3)).x - layout.centre_x, reach_y = middle(at(3, 7)).y - layout.centre_y;
        assert(std::abs(reach_x - reach_y) < reach_x * 0.02f);  // latitude and longitude differ a little
        assert(reach_x > side / 2 * 0.95f && reach_x < side / 2 * 1.05f);
        assert(corners(at(7, 3))[2].x > layout.centre_x + side / 2 + 10);  // its outer edge, by the equator, past the screen
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
        // Breathing scales the picture about the middle; lifting moves a tile
        // outwards (away from the middle); turning narrows it.
        const auto& t = at(6, 2);
        const auto base = corners(t), big = grid_tile_corners(layout, t, 1.02f, 0, 0);
        assert(std::abs(big[0].x - layout.centre_x) > std::abs(base[0].x - layout.centre_x));
        const auto lifted = grid_tile_corners(layout, t, 1.0f, 20.0f, 0);
        assert(lifted[0].x > base[0].x && lifted[0].y < base[0].y);
        const auto turned = grid_tile_corners(layout, t, 1.0f, 0, 60.0f);
        assert(turned[1].x - turned[0].x < base[1].x - base[0].x);
        assert(grid_quad_matrix(grid_tile_corners(layout, t, 1.0f, 0, 90.0f), 128).has_value());  // edge on is held short of it
        return layout;
    };
    const auto square = check(1080, 1080);
    const auto wide = check(1920, 1080);
    check(2560, 1080);
    check(1440, 1080);
    const auto tall = check(1080, 1920);
    // The same sphere for the same longer side: a wide screen crops it top and
    // bottom, a tall one at the sides; never stretched.
    const auto big = check(1920, 1920);
    assert(near(wide.radius, big.radius, 0.01f) && near(tall.radius, big.radius, 0.01f) && square.radius < big.radius);

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
    assert(grid_layout(0, 0).tiles.empty());

    // Breathing: the whole sphere swells and settles about 1.2 % either way,
    // once every 1.5 seconds.
    float low = 2, high = 0;
    for (double time = 0; time < grid_wave_seconds; time += 0.01) {
        low = std::min(low, grid_breath(time));
        high = std::max(high, grid_breath(time));
    }
    assert(near(low, 1 - grid_breath_scale, 0.001f) && near(high, 1 + grid_breath_scale, 0.001f));
    assert(near(grid_breath(0.3), grid_breath(0.3 + grid_wave_seconds), 0.0001f));
    // The soft wave: tiles come forward and back, never behind their place,
    // and tiles further along it are out of step.
    const auto& t0 = wide.tiles[0];
    const auto& t1 = wide.tiles[9];
    float most = 0;
    bool apart = false;
    for (double time = 0; time < grid_wave_seconds; time += 0.05) {
        const float o = grid_wave_offset(t0, time);
        assert(o >= 0 && o <= grid_wave_depth * t0.width + 0.01f);
        most = std::max(most, o);
        if (std::abs(o - grid_wave_offset(t1, time)) > 0.5f) apart = true;
    }
    assert(most > grid_wave_depth * t0.width * 0.9f && apart);
    assert(near(grid_wave_offset(t0, 1.0), grid_wave_offset(t0, 1.0 + grid_wave_seconds), 0.01f));

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
        for (int round = 0; round < 150; ++round) {
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
        grid.update(context, "", 900.0);
        grid.update(context, "pause", 1000.0);
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
        grid.update(context, "", 1100.0);
        assert(!grid.visible());
        grid.update(context, "blue", 1101.0);
        assert(grid.visible() && !grid.changing() && same(grid.tile_colour(30), blue));

        // A wider window: the tiles are laid out again.
        context->SetDimensions(Rml::Vector2i(1920, 1080));
        grid.update(context, "blue", 1102.0);
        assert(grid.tile_count() == 64);
        context->Update();
        context->Render();

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
