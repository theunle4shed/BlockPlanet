#include "ui_grid.h"

#include "grid_texture.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace blockplanet::ui {
namespace {

constexpr float pi = 3.14159265358979f;

// The sphere: the grid spans this angle either side of its middle (degrees;
// 90 would be a half sphere), in equal steps across and down.
constexpr float half_span_degrees = 62.0f;
// The viewer's distance, in the screen's longer side.
constexpr float viewer_distance = 1.6f;
// Where the middle of the outermost tiles is seen along the longer side
// (1: at the screen's edge, so they are cut about in half; the owner's note).
constexpr float outer_reach = 1.0f;
// How much of its step of latitude and longitude a tile covers (the rest is the gap).
constexpr float tile_fill = 0.9f;
// The element each tile is drawn from (pixels square), before its matrix.
constexpr float tile_element = 128.0f;
// Turned closer to edge on than this, a tile is drawn at this angle (its
// matrix would have no area).
constexpr float most_turn = 87.0f;
// The light, from the upper left and front (CSS axes: y down, z towards the viewer).
constexpr float light_x = -0.35f, light_y = -0.45f, light_z = 0.82f;

struct Vec3 {
    float x, y, z;
};
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalised(Vec3 v) { return v * (1.0f / std::sqrt(dot(v, v))); }
// The outward direction at an angle across (right) and down from the middle.
// Rows and columns are treated alike: every line of equal angle across is a
// great circle through the sphere's top and bottom, every line of equal
// angle down one through its left and right sides, so rows bow as much as
// columns (a symmetric bulge, like the original's tile sphere seen head on).
Vec3 direction(float across, float down) { return normalised({std::tan(across), std::tan(down), 1.0f}); }

GridRgb rgb(int hex) { return {float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f}; }

GridRgb mix(GridRgb a, GridRgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
bool same(GridRgb a, GridRgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

std::string css(GridRgb c, float scale) {
    const auto channel = [scale](float v) { return std::clamp(int(std::lround(v * scale * 255.0f)), 0, 255); };
    char text[48];
    std::snprintf(text, sizeof(text), "rgb(%d,%d,%d)", channel(c.r), channel(c.g), channel(c.b));
    return text;
}

std::string px(float v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1fpx", v);
    return text;
}

float ease_in_out(float t) { return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t); }

} // namespace

std::optional<GridRgb> grid_colour(std::string_view name) {
    // Tile colours sampled from the original game's grid (dark and of low
    // saturation): blue #0d151f (highlights #141f2d, shadows #080d14) and red
    // #2b0510; the others made to match them. Purple, cyan, teal and peach
    // are the pause's cycle (after the original pause's sphere grid).
    if (name == "blue") return rgb(0x0d151f);
    if (name == "green") return rgb(0x0b1d18);  // dark teal-green
    if (name == "red") return rgb(0x2b0510);
    if (name == "purple") return rgb(0x1c1030);
    if (name == "gold") return rgb(0x261d08);
    if (name == "grey") return rgb(0x151719);
    if (name == "cyan") return rgb(0x0a2029);
    if (name == "teal") return rgb(0x0a2420);
    if (name == "peach") return rgb(0x2e1713);
    // The pause's, sampled from the original's pause background (its
    // average colour, once a second for 40 seconds).
    if (name == "pause-purple") return rgb(0x26152f);
    if (name == "pause-blue") return rgb(0x0f1b2c);
    if (name == "pause-grey") return rgb(0x262b30);
    if (name == "pause-teal") return rgb(0x0f2131);
    if (name == "pause-green") return rgb(0x242c25);
    return std::nullopt;
}

GridRgb grid_far_colour(GridRgb tile) { return {tile.r * 0.45f, tile.g * 0.45f, tile.b * 0.45f}; }

std::vector<std::string_view> grid_cycle(std::string_view name) {
    if (name == "pause") return {"pause-purple", "pause-blue", "pause-grey", "pause-teal", "pause-green"};
    return {};
}

GridRgb grid_cycle_colour(const std::vector<std::string_view>& cycle, double time) {
    if (cycle.empty()) return {};
    const double position = std::max(0.0, time) / grid_cycle_seconds;
    const auto step = std::size_t(position);
    const float t = float(position - double(step));
    const auto from = grid_colour(cycle[step % cycle.size()]).value_or(GridRgb{});
    const auto to = grid_colour(cycle[(step + 1) % cycle.size()]).value_or(GridRgb{});
    return mix(from, to, t * t * (3.0f - 2.0f * t));
}

GridLayout grid_layout(float width, float height) {
    GridLayout layout;
    if (width <= 0.0f || height <= 0.0f) return layout;
    layout.columns = grid_columns;
    layout.rows = grid_rows;
    layout.centre_x = width / 2.0f;
    layout.centre_y = height / 2.0f;
    // Round, never stretched: sized by the longer side.
    const float side = std::max(width, height);
    const float span = half_span_degrees * pi / 180.0f;
    const float step = 2.0f * span / float(grid_columns);
    layout.perspective = viewer_distance * side;
    // The radius that shows the middle of the outermost tiles at the screen's
    // edge (seen in perspective from the viewer; the sphere's front at the
    // screen, its centre behind it).
    const float outer = span - step / 2.0f, half = side / 2.0f * outer_reach, p = layout.perspective;
    layout.radius = half * p / (p * std::sin(outer) - half * (1.0f - std::cos(outer)));
    const float inset = step * (1.0f - tile_fill) / 2.0f;
    for (int row = 0; row < layout.rows; ++row) {
        for (int column = 0; column < layout.columns; ++column) {
            GridTile tile;
            tile.column = column;
            tile.row = row;
            // A patch between lines of equal angle: every tile in a row shares
            // its top and bottom lines, every tile in a column its sides.
            tile.lon0 = -span + float(column) * step + inset;
            tile.lon1 = -span + float(column + 1) * step - inset;
            tile.lat0 = -span + float(row) * step + inset;
            tile.lat1 = -span + float(row + 1) * step - inset;
            tile.lon = (tile.lon0 + tile.lon1) / 2.0f;
            tile.lat = (tile.lat0 + tile.lat1) / 2.0f;
            tile.width = layout.radius * std::sqrt(dot(direction(tile.lon1, tile.lat) - direction(tile.lon0, tile.lat),
                                                      direction(tile.lon1, tile.lat) - direction(tile.lon0, tile.lat)));
            const Vec3 d = direction(tile.lon, tile.lat);
            const float facing = d.x * light_x + d.y * light_y + d.z * light_z;
            tile.shade = std::clamp(0.5f + 0.6f * facing, 0.45f, 1.15f);
            tile.variant = (column * 7 + row * 3 + (column * row) % 5) % 4;
            // The ripple runs from the top left corner to the bottom right.
            tile.delay = grid_ripple_seconds * float(column + row) / float(layout.columns + layout.rows - 2);
            tile.step = column + row;
            // Flying in, the outermost set off first and the middle last.
            const float dx = float(column) - 3.5f, dy = float(row) - 3.5f;
            tile.fly = 1.0f - std::sqrt(dx * dx + dy * dy) / std::sqrt(2.0f * 3.5f * 3.5f);
            layout.tiles.push_back(tile);
        }
    }
    return layout;
}

std::optional<std::array<GridPoint, 4>> grid_tile_corners(const GridLayout& layout, const GridTile& tile, float breath,
                                                          float lift, float turn, float away) {
    const float r = layout.radius;
    const Vec3 centre_of_sphere{0.0f, 0.0f, -r};
    const Vec3 normal = direction(tile.lon, tile.lat);
    const Vec3 middle = centre_of_sphere + normal * r;
    // The upright axis: along the tile's column, downwards.
    const Vec3 axis = normalised(direction(tile.lon, tile.lat + 0.01f) - direction(tile.lon, tile.lat - 0.01f));
    const float angle = std::clamp(turn, -most_turn, most_turn) * pi / 180.0f;
    const float c = std::cos(angle), s = std::sin(angle);
    const float lons[4] = {tile.lon0, tile.lon1, tile.lon1, tile.lon0};
    const float lats[4] = {tile.lat0, tile.lat0, tile.lat1, tile.lat1};
    // Flying out: speeding up towards the viewer (past them at 1) and a
    // little outwards, so the tiles part around the middle as they come.
    const float rush = away * away;
    const float distance = grid_tuning().fly_distance;
    const Vec3 flight = Vec3{0.0f, 0.0f, layout.perspective * 1.3f * distance} * rush + normal * (r * 0.6f * distance * rush);
    std::array<GridPoint, 4> out{};
    for (int k = 0; k < 4; ++k) {
        // The corner on the sphere, turned about the axis through the tile's
        // middle (Rodrigues), lifted outwards, then the whole sphere breathes
        // about its front (the screen's middle).
        const Vec3 v = centre_of_sphere + direction(lons[k], lats[k]) * r - middle;
        const Vec3 turned = v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
        const Vec3 at = (middle + turned + normal * lift) * breath + flight;
        if (at.z > layout.perspective * 0.8f) return std::nullopt;  // flown past the viewer
        const float scale = layout.perspective / (layout.perspective - at.z);
        out[std::size_t(k)] = {layout.centre_x + at.x * scale, layout.centre_y + at.y * scale};
    }
    return out;
}

std::optional<std::array<float, 16>> grid_quad_matrix(const std::array<GridPoint, 4>& q, float size) {
    // The unit square onto the quad (Heckbert's square-to-quad mapping):
    // x = (a u + b v + c) / (g u + h v + 1), y = (d u + e v + f) / (g u + h v + 1).
    const float dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x, dx3 = q[0].x - q[1].x + q[2].x - q[3].x;
    const float dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y, dy3 = q[0].y - q[1].y + q[2].y - q[3].y;
    const float det = dx1 * dy2 - dx2 * dy1;
    if (std::abs(det) < 1e-3f || size <= 0.0f) return std::nullopt;
    const float g = (dx3 * dy2 - dx2 * dy3) / det, h = (dx1 * dy3 - dx3 * dy1) / det;
    const float a = q[1].x - q[0].x + g * q[1].x, b = q[3].x - q[0].x + h * q[3].x, c = q[0].x;
    const float d = q[1].y - q[0].y + g * q[1].y, e = q[3].y - q[0].y + h * q[3].y, f = q[0].y;
    if (!std::isfinite(g) || !std::isfinite(h)) return std::nullopt;
    // u and v are the element's pixels over its size; z is kept as it is.
    return std::array<float, 16>{a / size, d / size, 0, g / size, b / size, e / size, 0, h / size, 0, 0, 1, 0, c, f, 0, 1};
}

GridTuning& grid_tuning() {
    static GridTuning tuning;
    return tuning;
}

namespace {

// The test screen's settings, with how a slider's value maps onto
// grid_tuning (the value over `unit`).
struct LabSetting {
    const char* name;
    float GridTuning::*field;
    float unit;
};
constexpr LabSetting lab_settings[] = {
    {"grid_breath_in", &GridTuning::breath_in, 1.0f},
    {"grid_breath_hold", &GridTuning::breath_hold, 1.0f},
    {"grid_breath_out", &GridTuning::breath_out, 1.0f},
    {"grid_breath_size", &GridTuning::breath_scale, 200.0f},  // percent, peak to peak
    {"grid_wave_every", &GridTuning::wave_every, 1.0f},
    {"grid_wave_step", &GridTuning::wave_step, 1000.0f},      // milliseconds
    {"grid_wave_rise", &GridTuning::wave_rise, 1.0f},
    {"grid_wave_depth", &GridTuning::wave_depth, 100.0f},     // percent of a tile's width
    {"grid_fly_seconds", &GridTuning::fly_seconds, 1.0f},
    {"grid_fly_spread", &GridTuning::fly_spread, 1.0f},
    {"grid_fly_distance", &GridTuning::fly_distance, 1.0f},
};

} // namespace

std::vector<std::pair<std::string, std::string>> grid_lab_values() {
    std::vector<std::pair<std::string, std::string>> values;
    for (const auto& s : lab_settings) {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", double(std::round(grid_tuning().*s.field * s.unit * 1000.0f) / 1000.0f));
        values.emplace_back(s.name, text);
    }
    return values;
}

bool grid_lab_set(const std::string& name, const std::string& value) {
    for (const auto& s : lab_settings) {
        if (name != s.name) continue;
        grid_tuning().*s.field = float(std::atof(value.c_str())) / s.unit;
        return true;
    }
    return false;
}

float grid_breath(double time) {
    // In (rising smoothly), held, out (falling smoothly), then again.
    const auto& t = grid_tuning();
    const double in = std::max(0.05f, t.breath_in), hold = std::max(0.0f, t.breath_hold), out = std::max(0.05f, t.breath_out);
    const double at = std::fmod(std::max(0.0, time), in + hold + out);
    float level;
    if (at < in) level = 0.5f - 0.5f * std::cos(float(at / in) * pi);
    else if (at < in + hold) level = 1.0f;
    else level = 0.5f + 0.5f * std::cos(float((at - in - hold) / out) * pi);
    return 1.0f + t.breath_scale * (2.0f * level - 1.0f);
}

float grid_wave_offset(const GridTile& tile, double time) {
    // Waves set off from the top left every wave_every seconds; this tile
    // rises and falls (a raised cosine over wave_rise) wave_step seconds
    // after the one before it along the wave. Overlapping waves add up, to at most one.
    const auto& t = grid_tuning();
    const double every = std::max(0.05f, t.wave_every), rise = std::max(0.05f, t.wave_rise);
    const double local = std::fmod(std::fmod(std::max(0.0, time) - double(tile.step) * t.wave_step, every) + every, every);
    float sum = 0.0f;
    for (double since = local; since < rise; since += every) sum += 0.5f - 0.5f * std::cos(float(since / rise) * 2.0f * pi);
    return t.wave_depth * tile.width * std::min(1.0f, sum);
}

std::string grid_colour_of(const std::vector<Rml::ElementDocument*>& screens) {
    if (screens.empty() || screens.front() == nullptr || screens.front()->GetAttribute<Rml::String>("data-grid", "").empty())
        return {};
    for (auto it = screens.rbegin(); it != screens.rend(); ++it) {
        if (*it == nullptr) continue;
        auto colour = (*it)->GetAttribute<Rml::String>("data-grid", "");
        if (!colour.empty()) return colour;
    }
    return {};
}

GridRgb MenuGrid::tile_colour(std::size_t index) const { return index < tiles_.size() ? tiles_[index].shown : GridRgb{}; }
float MenuGrid::tile_turn(std::size_t index) const { return index < tiles_.size() ? tiles_[index].turn : 0.0f; }

void MenuGrid::build(Rml::Context* context) {
    size_ = context->GetDimensions();
    layout_ = grid_layout(float(size_.x), float(size_.y));
    const auto& atlas = grid_tile_atlas();
    std::string rml;
    for (const auto& tile : layout_.tiles) {
        rml += "<img class=\"grid-tile\" src=\"/blockplanet-grid-tile\" rect=\"" + std::to_string(tile.variant * atlas.tile) +
               " 0 " + std::to_string(atlas.tile) + " " + std::to_string(atlas.tile) + "\"/>";
    }
    layer_->SetInnerRML(rml);
    tiles_.assign(layout_.tiles.size(), Tile{});
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        Tile& tile = tiles_[i];
        tile.element = layer_->GetChild(int(i));
        tile.face = tile.shown = target_;
        if (tile.element == nullptr) continue;
        // Each tile drawn from a square at the top left, which its matrix
        // carries onto its patch of the sphere, in perspective.
        tile.element->SetProperty("width", px(tile_element));
        tile.element->SetProperty("height", px(tile_element));
    }
}

void MenuGrid::retarget(GridRgb colour, double now) {
    if (same(colour, target_)) return;
    // The far background fades on from wherever it is.
    const float fade = std::clamp(float(now - change_at_) / grid_fade_seconds, 0.0f, 1.0f);
    far_from_ = mix(far_from_, far_to_, ease_in_out(fade));
    far_to_ = grid_far_colour(colour);
    target_ = colour;
    // A new ripple. Tiles that have not yet turned past edge-on head for this
    // colour at once; the others finish and flip again (place).
    change_at_ = now;
}

void MenuGrid::drift(GridRgb colour) {
    // A cycle's slow fade: every tile showing (or turning to) the colour
    // follows it without flipping, and so does the far background.
    const GridRgb old = target_;
    for (auto& tile : tiles_) {
        if (same(tile.face, old)) tile.face = colour;
        if (same(tile.back, old)) tile.back = colour;
    }
    const bool settled = same(far_from_, far_to_);
    far_to_ = grid_far_colour(colour);
    if (settled) far_from_ = far_to_;
    target_ = colour;
}

void MenuGrid::place(double now) {
    bool active = false;
    const float breath = grid_breath(now);
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        Tile& tile = tiles_[i];
        const GridTile& at = layout_.tiles[i];
        // A tile still showing another colour flips when the newest ripple
        // reaches it (or as soon as its previous flip ends, if later).
        const double ready = change_at_ + double(at.delay);
        if (!tile.flipping && !same(tile.face, target_) && now >= ready) {
            tile.flipping = true;
            tile.flip_start = std::max(ready, tile.flip_end);
            tile.back = target_;
        }
        float turn = 0.0f;
        GridRgb shown = tile.face;
        if (tile.flipping) {
            const float p = std::clamp(float(now - tile.flip_start) / grid_flip_seconds, 0.0f, 1.0f);
            const float angle = 180.0f * ease_in_out(p);
            // Not yet past edge-on: still heading for the newest colour.
            if (angle < 90.0f) tile.back = target_;
            if (p >= 1.0f) {
                tile.flipping = false;
                tile.face = tile.back;
                tile.flip_end = tile.flip_start + double(grid_flip_seconds);
                shown = tile.face;
                // An older colour reached: flip again at once for the newest.
                if (!same(tile.face, target_)) {
                    tile.flipping = true;
                    tile.flip_start = std::max(ready, tile.flip_end);
                    tile.back = target_;
                }
            } else if (angle > 90.0f) {
                // Past edge-on the tile shows its back (drawn as the front
                // turned the other way, which looks the same).
                turn = angle - 180.0f;
                shown = tile.back;
            } else {
                turn = angle;
            }
        }
        if (tile.flipping || !same(tile.face, target_)) active = true;
        tile.shown = shown;
        tile.turn = turn;
        // Turned towards edge on, a tile catches less light (in steps, so its
        // colour, which rebuilds its shape, changes only a few times a flip).
        const float edge = std::round(std::abs(std::cos(turn * pi / 180.0f)) * 6.0f) / 6.0f;
        const float shade = at.shade * (0.35f + 0.65f * edge);
        // Layers: the whole sphere's breath, the tile's place on it, its part
        // in the wave (lifted outwards), its flip. Worked out here and given
        // to RmlUi as one matrix per tile.
        std::string transform = tile.transform;
        const auto& tuning = grid_tuning();
        const float in_place = std::clamp((fly_ - at.fly * tuning.fly_spread) / std::max(0.01f, tuning.fly_seconds), 0.0f, 1.0f);
        const float away = 1.0f - in_place * in_place * (3.0f - 2.0f * in_place);
        const auto corners = grid_tile_corners(layout_, at, breath, grid_wave_offset(at, now), turn, away);
        const bool hidden = !corners || in_place <= 0.0f;
        if (tile.element != nullptr && hidden != tile.hidden) {
            tile.hidden = hidden;
            tile.element->SetProperty("visibility", hidden ? "hidden" : "visible");
        }
        if (corners)
        if (const auto m = grid_quad_matrix(*corners, tile_element)) {
            char text[400];
            std::snprintf(text, sizeof(text), "matrix3d(%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g)",
                          (*m)[0], (*m)[1], (*m)[2], (*m)[3], (*m)[4], (*m)[5], (*m)[6], (*m)[7], (*m)[8], (*m)[9], (*m)[10],
                          (*m)[11], (*m)[12], (*m)[13], (*m)[14], (*m)[15]);
            transform = text;
        }
        // The texture's middle grey is half white: twice the colour gives the colour.
        std::string colour = css(shown, 2.0f * shade);
        if (tile.element == nullptr) continue;
        if (tile.transform != transform) {
            tile.transform = transform;
            tile.element->SetProperty("transform", tile.transform);
        }
        if (tile.colour != colour) {
            tile.colour = std::move(colour);
            tile.element->SetProperty("image-color", tile.colour);
        }
    }
    const float fade = std::clamp(float(now - change_at_) / grid_fade_seconds, 0.0f, 1.0f);
    // The far background fades in and out with the tiles, unless they fly
    // out into the loading cover. Only on change: setting a property rebuilds
    // its shape, even to the same value.
    // It is whole by half way, so it hides what is behind early on the way
    // in (the game's own pause fade) and stays until the tiles are mostly gone.
    const float present = std::clamp(presence() * 2.0f, 0.0f, 1.0f);
    const float far_alpha = (leaving_ && into_cover_) ? 1.0f : present * present * (3.0f - 2.0f * present);
    std::string far = css(mix(far_from_, far_to_, ease_in_out(fade)), 1.0f);
    far.insert(3, "a");  // rgb( -> rgba(
    far.back() = ',';
    far += std::to_string(int(std::lround(far_alpha * 255.0f))) + ")";
    if (far_ != nullptr && far != far_css_) {
        far_css_ = std::move(far);
        far_->SetProperty("background-color", far_css_);
    }
    changing_ = active || (fade < 1.0f && !same(far_from_, far_to_));
}

float MenuGrid::fly_total() const { return std::max(0.01f, grid_tuning().fly_seconds) + std::max(0.0f, grid_tuning().fly_spread); }
float MenuGrid::presence() const { return fly_ / fly_total(); }

float MenuGrid::cover_allowance() const {
    if (!visible_ || !leaving_) return 1.0f;
    // The cover rises over the second half of the flight out.
    const float gone = 1.0f - std::clamp(presence(), 0.0f, 1.0f);
    const float t = std::clamp((gone - 0.5f) / 0.5f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void MenuGrid::update(Rml::Context* context, const std::string& asked, double now, bool into_cover) {
    const float dt = visible_ ? float(std::clamp(now - last_now_, 0.0, 0.1)) : 0.0f;
    last_now_ = now;
    // Hidden or unknown: fly out in the colour shown, then hide.
    const bool wanted_here = grid_colour(asked).has_value() || !grid_cycle(asked).empty();
    if (!wanted_here) {
        if (!visible_) {
            name_.clear();
            changing_ = false;
            return;
        }
        if (!leaving_) into_cover_ = into_cover;
        into_cover_ = into_cover_ || into_cover;
        leaving_ = true;
        fly_ = std::max(0.0f, fly_ - dt);
        if (fly_ <= 0.0f) {
            if (document_ != nullptr && document_->IsVisible()) document_->Hide();
            visible_ = false;
            leaving_ = false;
            name_.clear();
            changing_ = false;
            return;
        }
    } else {
        leaving_ = false;
        into_cover_ = false;
    }
    const std::string name = wanted_here ? asked : name_;
    // A cycle fades through its colours, from the first, while it stays shown.
    const bool renamed = name != name_;
    if (renamed) shown_at_ = now;
    const auto cycle = grid_cycle(name);
    const auto wanted = cycle.empty() ? grid_colour(name) : std::optional<GridRgb>(grid_cycle_colour(cycle, now - shown_at_));
    if (!wanted) return;
    if (document_ == nullptr) {
        if (failed_) return;
        document_ = context->LoadDocument(path_);
        far_ = document_ != nullptr ? document_->GetElementById("grid-far") : nullptr;
        layer_ = document_ != nullptr ? document_->GetElementById("grid-tiles") : nullptr;
        if (document_ == nullptr || far_ == nullptr || layer_ == nullptr) {
            std::fprintf(stderr, "{\"event\":\"ui_screen_missing\",\"screen\":\"grid\"}\n");
            failed_ = true;
            if (document_ != nullptr) document_->Close();
            document_ = nullptr;
            return;
        }
        std::fprintf(stdout, "{\"event\":\"ui_grid\",\"texture\":\"%s\"}\n", grid_tile_atlas().width > 0 && grid_texture_from_rom() ? "rom" : "procedural");
        std::fflush(stdout);
    }
    if (!visible_) {
        // Shown again (or for the first time): in the colour at once, flying in.
        target_ = *wanted;
        far_from_ = far_to_ = grid_far_colour(*wanted);
        change_at_ = now;
        fly_ = 0.0f;
        for (auto& tile : tiles_) tile.face = tile.shown = tile.back = *wanted, tile.flipping = false, tile.flip_end = -1e9;
    } else if (!cycle.empty() && !renamed) {
        drift(*wanted);
    } else {
        retarget(*wanted, now);
    }
    if (wanted_here) fly_ = std::min(fly_total(), fly_ + dt);
    name_ = name;
    if (context->GetDimensions() != size_ || tiles_.empty()) build(context);
    visible_ = true;
    place(now);
    if (!document_->IsVisible()) document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    // Behind every menu screen.
    document_->PushToBack();
}

} // namespace blockplanet::ui
