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
    const auto check = [](float w, float h, int columns, int rows) {
        const auto layout = grid_layout(w, h);
        assert(layout.columns == columns && layout.rows == rows);
        assert(layout.tiles.size() == std::size_t(columns * rows));
        for (const auto& t : layout.tiles) {
            assert(t.x >= 0 && t.y >= 0 && t.x + t.width <= w + 0.01f && t.y + t.height <= h + 0.01f);
            assert(t.z >= 0 && t.shade > 0.5f && t.shade < 1.2f && t.variant >= 0 && t.variant < 4);
            assert(std::abs(t.tilt_x) < 60 && std::abs(t.tilt_y) < 60);
        }
        // The middle tile faces the viewer and comes nearest; the sides
        // turn outwards, mirrored.
        const auto& middle = layout.tiles[std::size_t((rows / 2) * columns + columns / 2)];
        assert(near(middle.tilt_x, 0) && near(middle.tilt_y, 0));
        for (const auto& t : layout.tiles) assert(t.z <= middle.z + 0.01f);
        const auto& left = layout.tiles[std::size_t((rows / 2) * columns)];
        const auto& right = layout.tiles[std::size_t((rows / 2) * columns + columns - 1)];
        assert(right.tilt_y > 10 && near(left.tilt_y, -right.tilt_y));
        const auto& top = layout.tiles[std::size_t(columns / 2)];
        const auto& bottom = layout.tiles[std::size_t((rows - 1) * columns + columns / 2)];
        assert(bottom.tilt_x < -5 && near(top.tilt_x, -bottom.tilt_x));
        // The ripple: top left first, bottom right last.
        assert(layout.tiles.front().delay == 0 && near(layout.tiles.back().delay, grid_ripple_seconds));
    };
    check(1440, 1080, 9, 7);   // 4:3, the original's grid
    check(640, 480, 9, 7);
    check(1920, 1080, 13, 7);  // 16:9
    check(2560, 1080, 17, 7);  // 21:9
    check(1080, 1080, 9, 9);   // square
    check(1080, 1920, 9, 17);  // portrait
    assert(grid_layout(0, 0).tiles.empty());

    assert(grid_colour("blue") && grid_colour("green") && grid_colour("red") && grid_colour("purple") && grid_colour("gold") &&
           grid_colour("grey"));
    assert(!grid_colour("") && !grid_colour("pink"));
    const auto blue = *grid_colour("blue");
    assert(near(blue.r, 0x0d / 255.0f) && near(blue.g, 0x15 / 255.0f) && near(blue.b, 0x1f / 255.0f));
    const auto far = grid_far_colour(blue);
    assert(far.r < blue.r && far.g < blue.g && far.b < blue.b);

    GridTile tile;
    tile.delay = 0.1f;
    assert(grid_flip_progress(tile, 0.0f) == 0.0f && grid_flip_progress(tile, 0.1f) == 0.0f);
    assert(near(grid_flip_progress(tile, 0.1f + grid_flip_seconds / 2), 0.5f) && grid_flip_progress(tile, 5.0f) == 1.0f);
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
        auto* pause = context->LoadDocumentFromMemory("<rml><body/></rml>");
        assert(grid_colour_of({}).empty());
        assert(grid_colour_of({main}) == "blue");
        assert(grid_colour_of({main, solo}) == "green");
        assert(grid_colour_of({main, solo, confirm}) == "green");  // keeps the colour under it
        assert(grid_colour_of({pause}).empty());                  // over a game: none
        assert(grid_colour_of({pause, main}).empty());            // Options from the pause menu

        MenuGrid grid(document);
        grid.update(context, "", 0.0);
        assert(!grid.visible());
        grid.update(context, "blue", 0.0);
        context->Update();
        const auto blue = *grid_colour("blue"), green = *grid_colour("green");
        assert(grid.visible() && grid.tile_count() == 63 && !grid.changing());
        assert(same(grid.tile_colour(0), blue) && same(grid.tile_colour(62), blue));
        grid.update(context, "blue", 0.5);  // the same colour: still
        assert(!grid.changing());

        // A new colour: the tiles flip one after another.
        grid.update(context, "green", 1.0);
        assert(grid.changing());
        grid.update(context, "green", 1.0 + 0.01);
        assert(same(grid.tile_colour(0), blue));
        grid.update(context, "green", 1.0 + grid_flip_seconds * 0.6);
        context->Update();
        assert(same(grid.tile_colour(0), green) && same(grid.tile_colour(62), blue));
        grid.update(context, "green", 1.0 + grid_fade_seconds + 0.01);
        assert(!grid.changing() && same(grid.tile_colour(0), green) && same(grid.tile_colour(62), green));

        // Back to blue, then green again part way through: no tile is left behind.
        grid.update(context, "blue", 3.0);
        grid.update(context, "blue", 3.0 + grid_flip_seconds * 0.6);
        assert(same(grid.tile_colour(0), blue) && same(grid.tile_colour(62), green));
        grid.update(context, "green", 3.0 + grid_flip_seconds * 0.7);
        grid.update(context, "green", 10.0);
        assert(!grid.changing());
        for (std::size_t i = 0; i < grid.tile_count(); ++i) assert(same(grid.tile_colour(i), green));

        // Hidden, then shown again: straight in its colour, no flip.
        grid.update(context, "", 11.0);
        assert(!grid.visible());
        grid.update(context, "blue", 12.0);
        assert(grid.visible() && !grid.changing() && same(grid.tile_colour(30), blue));

        // A wider window: the tiles are laid out again.
        context->SetDimensions(Rml::Vector2i(1920, 1080));
        grid.update(context, "blue", 13.0);
        assert(grid.tile_count() == 13 * 7);
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
