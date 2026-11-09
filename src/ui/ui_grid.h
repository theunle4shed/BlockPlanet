#pragma once

// The menu grid (docs/UI.md, Menu grid; the owner's design, 2026-11-09): the
// game's tiled grid as one background BlockPlanet draws itself, above the
// game's picture and HUD and below the menus. 8 x 8 tiles, patches of one
// sphere (not quite a half) between lines of equal angle across and down
// (great circles, rows and columns alike), so each row's and column's edges
// follow one smooth curve across the grid, bowing as much one way as the
// other, and the gaps between tiles read as the grid's lines. The sphere stays round and is
// sized by the screen's longer side, the outermost tiles there cut about in
// half by the edges. Like the original's, it breathes: the whole sphere swells and
// settles gently, with a soft wave of tiles coming forward and back across
// it. Between screens of the same colour nothing else happens; to a new
// colour the far background fades to it and the tiles flip over in a ripple,
// one after another, showing the new colour on their backs. The breathing
// carries on through flips.
//
// A screen picks its colour with data-grid on its body ("blue", "green", ...;
// see grid_colour), or a slow cycle of colours that fade smoothly into one
// another ("pause"; see grid_cycle). The
// grid is shown while the first screen open has one; its colour is that of
// the highest open screen that has one (a screen without one, such as a
// confirmation, keeps the colour under it).
//
// Colour changes never queue and never wait: each tile always heads for the
// newest colour. A tile not yet turned past edge-on carries on to it; one
// already showing an older colour finishes and flips again in the newest
// ripple.
//
// The layout and timing are plain functions, tested without RmlUi drawing
// anything; MenuGrid places the tiles as RmlUi elements (ui/grid.rml).

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blockplanet::ui {

struct GridRgb {
    float r = 0, g = 0, b = 0;  // 0 to 1
};
// A named colour's tile colour (the tiles' middle tone; the far background is
// a darker version). Names: blue, green, red, purple, gold, grey, cyan, teal,
// peach.
std::optional<GridRgb> grid_colour(std::string_view name);
GridRgb grid_far_colour(GridRgb tile);
// A named cycle of colours, fading smoothly from each to the next over
// grid_cycle_seconds ("pause": purple, blue, grey-blue, teal, green-grey,
// sampled from the original pause's background, which loops in about 22 to
// 25 seconds). Empty when `name` is not a cycle.
std::vector<std::string_view> grid_cycle(std::string_view name);
constexpr double grid_cycle_seconds = 5.0;
// A cycle's colour `time` seconds after it was first shown.
GridRgb grid_cycle_colour(const std::vector<std::string_view>& cycle, double time);

// One tile: its patch of the sphere (radians across, right, as lon, and
// down as lat; the gap to its neighbours already left out), its middle,
// its width across (pixels, about), its shading (1: lit face on), texture
// variant, ripple delay and place in the idle wave.
struct GridTile {
    float lon0 = 0, lon1 = 0, lat0 = 0, lat1 = 0;
    float lon = 0, lat = 0;
    float width = 0, shade = 1;
    int column = 0, row = 0, variant = 0;
    float delay = 0;  // seconds after a colour change that this tile starts to flip
    float phase = 0;  // the wave's phase here (radians)
};
struct GridLayout {
    int columns = 0, rows = 0;
    float centre_x = 0, centre_y = 0;  // the screen's middle (pixels)
    float perspective = 0;  // the viewer's distance (pixels), facing the screen's middle
    float radius = 0;       // the sphere's (pixels); its front touches the screen
    std::vector<GridTile> tiles;
};
constexpr int grid_columns = 8, grid_rows = 8;
GridLayout grid_layout(float width, float height);

// Where a tile's corners are seen (pixels; top left, top right, bottom right,
// bottom left): the whole sphere scaled by `breath` about its front, the
// tile lifted `lift` pixels outwards and turned `turn` degrees about its
// upright axis (positive: its right edge away).
struct GridPoint {
    float x = 0, y = 0;
};
std::array<GridPoint, 4> grid_tile_corners(const GridLayout& layout, const GridTile& tile, float breath, float lift,
                                           float turn);
// The CSS matrix3d (16 values, column by column) that draws a `size` x `size`
// element at (0, 0) onto the quad `corners` in perspective. Nothing when the
// quad has no area (a tile edge on).
std::optional<std::array<float, 16>> grid_quad_matrix(const std::array<GridPoint, 4>& corners, float size);

// Timing of a colour change (seconds).
constexpr float grid_flip_seconds = 0.34f;     // one tile's flip
constexpr float grid_ripple_seconds = 0.42f;   // from the first tile's start to the last's
constexpr float grid_fade_seconds = grid_flip_seconds + grid_ripple_seconds;  // the far background
// Breathing, measured from the original (1280 x 720, 30 fps): one breath
// about every 1.5 seconds, a smooth sine; the sphere's outline swells by
// about 2.4 % peak to peak. On top, a soft wave of tiles coming forward and
// back (the owner's impression), out of step across the grid.
constexpr float grid_wave_seconds = 1.5f;
constexpr float grid_breath_scale = 0.012f;  // either way
constexpr float grid_wave_depth = 0.05f;     // a tile's width, at most
// The whole sphere's scale at `time` seconds (about 1).
float grid_breath(double time);
// How far a tile comes forward (pixels) at `time` seconds.
float grid_wave_offset(const GridTile& tile, double time);

// The grid's colour for these open screens (first to top), as the rules
// above say: empty when the first has no data-grid.
std::string grid_colour_of(const std::vector<Rml::ElementDocument*>& screens);

class MenuGrid {
public:
    // `document` is ui/grid.rml.
    explicit MenuGrid(std::string document) : path_(std::move(document)) {}
    // Shows the grid in `colour` (a name for grid_colour or grid_cycle), or
    // hides it (empty or unknown). `now` in seconds. Call once a frame,
    // before Context::Update.
    void update(Rml::Context* context, const std::string& colour, double now);
    bool visible() const { return visible_; }
    // For tests: whether a colour change is still under way, the colour a
    // tile shows, and how far it is turned (degrees, 0 face on).
    bool changing() const { return changing_; }
    GridRgb tile_colour(std::size_t index) const;
    float tile_turn(std::size_t index) const;
    std::size_t tile_count() const { return tiles_.size(); }

private:
    struct Tile {
        Rml::Element* element = nullptr;
        GridRgb face;          // the colour shown when still
        GridRgb back;          // while flipping: the colour on its back
        bool flipping = false;
        double flip_start = 0, flip_end = -1e9;
        GridRgb shown;         // as last placed
        float turn = 0;        // as last placed (degrees)
        std::string transform, colour;  // last set, to set properties only on change
    };
    void build(Rml::Context* context);
    void place(double now);
    void retarget(GridRgb colour, double now);
    void drift(GridRgb colour);

    std::string path_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* far_ = nullptr;
    Rml::Element* layer_ = nullptr;
    Rml::Vector2i size_{0, 0};
    GridLayout layout_;
    std::vector<Tile> tiles_;
    std::string name_;          // as asked for (a colour or a cycle)
    double shown_at_ = 0;       // when the grid was shown in this name (for cycles)
    GridRgb target_;
    GridRgb far_from_, far_to_;
    std::string far_css_;
    double change_at_ = 0;
    bool changing_ = false;
    bool visible_ = false;
    bool failed_ = false;  // ui/grid.rml is missing: no grid
};

} // namespace blockplanet::ui
