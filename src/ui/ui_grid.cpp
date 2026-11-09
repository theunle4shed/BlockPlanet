#include "ui_grid.h"

#include "grid_texture.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace blockplanet::ui {
namespace {

constexpr float pi = 3.14159265358979f;

// The sphere: the grid spans this angle either side of its middle (degrees;
// 90 would be a half sphere). The tiles sit on it as on a cube's face pushed
// out to the sphere, equal angles apart along the rows and columns, so the
// corners pinch less than lines of latitude and longitude would.
constexpr float half_span_degrees = 62.0f;
// The viewer's distance, in the screen's longer side.
constexpr float viewer_distance = 1.6f;
// How far past the screen's edge the middle row and column reach (1: just to it).
constexpr float cover = 1.06f;
// How much of the space between neighbours a tile covers (the rest is the gap).
constexpr float tile_fill = 0.9f;
// The light, from the upper left and front (CSS axes: y down, z towards the viewer).
constexpr float light_x = -0.35f, light_y = -0.45f, light_z = 0.82f;
// The idle wave rolls diagonally, from the top left to the bottom right; one
// wave spans this many tiles.
constexpr float wave_tiles = 7.0f;

struct Vec3 {
    float x, y, z;
};
Vec3 direction(float a, float b) {  // a: across (right), b: down; radians
    const float x = std::tan(a), y = std::tan(b), length = std::sqrt(x * x + y * y + 1.0f);
    return {x / length, y / length, 1.0f / length};
}
float distance(Vec3 p, Vec3 q) { return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z)); }

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
    return std::nullopt;
}

GridRgb grid_far_colour(GridRgb tile) { return {tile.r * 0.45f, tile.g * 0.45f, tile.b * 0.45f}; }

std::vector<std::string_view> grid_cycle(std::string_view name) {
    if (name == "pause") return {"purple", "cyan", "teal", "peach"};
    return {};
}

GridLayout grid_layout(float width, float height) {
    GridLayout layout;
    if (width <= 0.0f || height <= 0.0f) return layout;
    layout.columns = grid_columns;
    layout.rows = grid_rows;
    // Round, never stretched: sized by the longer side, so the middle row
    // (wide screens) or column (tall ones) reaches just past the edges.
    const float side = std::max(width, height);
    const float span = half_span_degrees * pi / 180.0f;
    const float step = 2.0f * span / float(grid_columns);
    layout.perspective = viewer_distance * side;
    // The radius that puts the outer edge of the middle row at cover x half
    // the side, seen in perspective from the viewer (the sphere's front at
    // the screen, its centre behind it).
    const float half = side / 2.0f * cover, p = layout.perspective;
    layout.radius = half * p / (p * std::sin(span) - half * (1.0f - std::cos(span)));
    const float r = layout.radius;
    for (int row = 0; row < layout.rows; ++row) {
        for (int column = 0; column < layout.columns; ++column) {
            GridTile tile;
            tile.column = column;
            tile.row = row;
            const float a = -span + (float(column) + 0.5f) * step, b = -span + (float(row) + 0.5f) * step;
            const Vec3 d = direction(a, b);
            tile.x = r * d.x;
            tile.y = r * d.y;
            tile.z = r * d.z - r;
            // Sized to its neighbours, so tiles never overlap near the corners.
            tile.width = r * distance(direction(a - step / 2, b), direction(a + step / 2, b)) * tile_fill;
            tile.height = r * distance(direction(a, b - step / 2), direction(a, b + step / 2)) * tile_fill;
            // Facing outwards: CSS rotateY(yaw) rotateX(pitch) turns the
            // face's normal (0, 0, 1) to (cos p sin y, -sin p, cos p cos y).
            const float yaw = std::atan2(d.x, d.z), pitch = -std::asin(d.y);
            tile.yaw = yaw * 180.0f / pi;
            tile.pitch = pitch * 180.0f / pi;
            const float facing = d.x * light_x + d.y * light_y + d.z * light_z;
            tile.shade = std::clamp(0.5f + 0.6f * facing, 0.45f, 1.15f);
            tile.variant = (column * 7 + row * 3 + (column * row) % 5) % 4;
            // The ripple runs from the top left corner to the bottom right.
            tile.delay = grid_ripple_seconds * float(column + row) / float(layout.columns + layout.rows - 2);
            tile.phase = 2.0f * pi * float(column + row) / wave_tiles;
            layout.tiles.push_back(tile);
        }
    }
    return layout;
}

float grid_wave_offset(const GridTile& tile, double time) {
    const double cycle = std::fmod(time / double(grid_wave_seconds), 1.0);
    // Mostly resting, rising and sinking smoothly (a raised cosine).
    const float wave = 0.5f - 0.5f * std::cos(float(cycle) * 2.0f * pi - tile.phase);
    return grid_wave_depth * tile.width * wave * wave;
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
    layer_->SetProperty("perspective", px(layout_.perspective));
    tiles_.assign(layout_.tiles.size(), Tile{});
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        Tile& tile = tiles_[i];
        tile.element = layer_->GetChild(int(i));
        tile.face = tile.shown = target_;
        if (tile.element == nullptr) continue;
        // Each tile's middle at the screen's middle; its transform moves it out.
        const auto& place = layout_.tiles[i];
        tile.element->SetProperty("left", px(float(size_.x) / 2.0f - place.width / 2.0f));
        tile.element->SetProperty("top", px(float(size_.y) / 2.0f - place.height / 2.0f));
        tile.element->SetProperty("width", px(place.width));
        tile.element->SetProperty("height", px(place.height));
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

void MenuGrid::place(double now) {
    bool active = false;
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
        char transform[200];
        std::snprintf(transform, sizeof(transform),
                      "translate3d(%.1fpx, %.1fpx, %.1fpx) rotateY(%.2fdeg) rotateX(%.2fdeg) translateZ(%.1fpx) rotateY(%.1fdeg)",
                      at.x, at.y, at.z, at.yaw, at.pitch, grid_wave_offset(at, now), turn);
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
    // Only on change: setting a property rebuilds its shape, even to the same value.
    std::string far = css(mix(far_from_, far_to_, ease_in_out(fade)), 1.0f);
    if (far_ != nullptr && far != far_css_) {
        far_css_ = std::move(far);
        far_->SetProperty("background-color", far_css_);
    }
    changing_ = active || (fade < 1.0f && !same(far_from_, far_to_));
}

void MenuGrid::update(Rml::Context* context, const std::string& name, double now) {
    // A cycle shows its colours in turn, from the first, while it stays shown.
    if (name != name_) shown_at_ = now;
    std::string colour = name;
    if (const auto cycle = grid_cycle(name); !cycle.empty()) {
        const auto step = std::size_t(std::max(0.0, now - shown_at_) / grid_cycle_seconds);
        colour = std::string(cycle[step % cycle.size()]);
    }
    const auto wanted = grid_colour(colour);
    if (!wanted) {
        if (document_ != nullptr && document_->IsVisible()) document_->Hide();
        visible_ = false;
        name_.clear();
        changing_ = false;
        return;
    }
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
        // Shown again (or for the first time): straight in the colour, still.
        target_ = *wanted;
        far_from_ = far_to_ = grid_far_colour(*wanted);
        change_at_ = now;
        for (auto& tile : tiles_) tile.face = tile.shown = tile.back = *wanted, tile.flipping = false, tile.flip_end = -1e9;
    } else {
        retarget(*wanted, now);
    }
    name_ = name;
    if (context->GetDimensions() != size_ || tiles_.empty()) build(context);
    place(now);
    if (!document_->IsVisible()) document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    // Behind every menu screen.
    document_->PushToBack();
    visible_ = true;
}

} // namespace blockplanet::ui
