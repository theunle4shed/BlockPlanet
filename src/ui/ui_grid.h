#pragma once

// The menu grid (docs/UI.md, Menu grid; the owner's design, 2026-11-09): the
// game's tiled grid as one background BlockPlanet draws itself, above the
// game's picture and HUD and below the menus. Each tile is its own piece,
// placed and tilted so the grid bulges outward like a sphere. Between screens
// of the same colour it stays still; to a new colour the far background
// fades to it and the tiles flip over in a ripple, one after another, showing
// the new colour on their backs.
//
// A screen picks its colour with data-grid on its body ("blue", "green", ...;
// see grid_colour). The grid is shown while the first screen open has one;
// its colour is that of the highest open screen that has one (a screen
// without one, such as a confirmation, keeps the colour under it). The menus
// over a game (pause) have none, so no grid shows there.
//
// The layout and timing are plain functions, tested without RmlUi drawing
// anything; MenuGrid places the tiles as RmlUi elements (ui/grid.rml).

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blockplanet::ui {

struct GridRgb {
    float r = 0, g = 0, b = 0;  // 0 to 1
};
// A named colour's tile colour (the tiles' middle tone; the far background is
// a darker version). Names: blue, green, red, purple, gold, grey.
std::optional<GridRgb> grid_colour(std::string_view name);
GridRgb grid_far_colour(GridRgb tile);

// One tile's place: its rectangle before tilting (pixels), how far it comes
// towards the viewer (pixels), its tilt (degrees, CSS rotateX and rotateY),
// its shading (1: as lit face on) and its texture variant.
struct GridTile {
    float x = 0, y = 0, width = 0, height = 0;
    float z = 0, tilt_x = 0, tilt_y = 0, shade = 1;
    int column = 0, row = 0, variant = 0;
    float delay = 0;  // seconds after a colour change that this tile starts to flip
};
struct GridLayout {
    int columns = 0, rows = 0;
    float perspective = 0;  // pixels, from the screen's centre
    std::vector<GridTile> tiles;
};
// 9 x 7 tiles on a 4:3 screen. Wider screens get more columns and taller
// ones more rows (odd counts, so a tile sits in the middle), keeping the
// tiles about as square as the original's.
GridLayout grid_layout(float width, float height);

// Timing of a colour change (seconds).
constexpr float grid_flip_seconds = 0.34f;     // one tile's flip
constexpr float grid_ripple_seconds = 0.42f;   // from the first tile's start to the last's
constexpr float grid_fade_seconds = grid_flip_seconds + grid_ripple_seconds;  // the far background
// A tile's flip, 0 (not started) to 1 (done), `elapsed` seconds after the
// change began.
float grid_flip_progress(const GridTile& tile, float elapsed);

// The grid's colour for these open screens (first to top), as the rules
// above say: empty when the first has no data-grid.
std::string grid_colour_of(const std::vector<Rml::ElementDocument*>& screens);

class MenuGrid {
public:
    // `document` is ui/grid.rml.
    explicit MenuGrid(std::string document) : path_(std::move(document)) {}
    // Shows the grid in `colour` (a name for grid_colour), or hides it
    // (empty or unknown). `now` in seconds. Call once a frame, before
    // Context::Update.
    void update(Rml::Context* context, const std::string& colour, double now);
    bool visible() const { return visible_; }
    // For tests: whether a colour change is still under way, and the colour
    // a tile shows.
    bool changing() const { return changing_; }
    GridRgb tile_colour(std::size_t index) const;
    std::size_t tile_count() const { return tiles_.size(); }

private:
    struct Tile {
        Rml::Element* element = nullptr;
        GridRgb from, to;
        bool flipping = false;
        float start = 0;  // seconds after change_at_ that its flip starts
        std::string transform, colour;  // last set, to set properties only on change
    };
    void build(Rml::Context* context);
    void place(float elapsed);

    std::string path_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* far_ = nullptr;
    Rml::Element* layer_ = nullptr;
    Rml::Vector2i size_{0, 0};
    GridLayout layout_;
    std::vector<Tile> tiles_;
    std::string colour_;
    GridRgb far_from_, far_to_;
    double change_at_ = 0;
    float last_elapsed_ = 0;
    bool changing_ = false;
    bool visible_ = false;
    bool failed_ = false;  // ui/grid.rml is missing: no grid
};

} // namespace blockplanet::ui
