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

// The original's tiles, as cell width over cell height (320 / 9 by 240 / 7).
constexpr float cell_ratio = (320.0f / 9.0f) / (240.0f / 7.0f);
// How much of its cell a tile covers (the rest is the gap).
constexpr float tile_fill = 0.9f;
// The sphere: its radius in half screen heights (at least), how far its middle
// comes towards the viewer (in half screen heights), and the viewer's distance
// (in screen heights).
constexpr float sphere_radius = 2.4f;
constexpr float sphere_depth = 0.12f;
constexpr float viewer_distance = 2.2f;
// The light, from the upper left and front (CSS axes: y down, z towards the viewer).
constexpr float light_x = -0.35f, light_y = -0.45f, light_z = 0.82f;

// The odd count nearest `value` (from a little below an even number up, so
// the wider tiles of the two are not chosen), at least `minimum`.
int odd_at_least(float value, int minimum) {
    const int odd = 2 * int(std::floor((value + 0.5f) / 2.0f)) + 1;
    return std::max(minimum, odd);
}

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
    // #2b0510; the others made to match them.
    if (name == "blue") return rgb(0x0d151f);
    if (name == "green") return rgb(0x0b1d18);  // dark teal-green
    if (name == "red") return rgb(0x2b0510);
    if (name == "purple") return rgb(0x1a0f26);
    if (name == "gold") return rgb(0x261d08);
    if (name == "grey") return rgb(0x151719);
    return std::nullopt;
}

GridRgb grid_far_colour(GridRgb tile) { return {tile.r * 0.45f, tile.g * 0.45f, tile.b * 0.45f}; }

GridLayout grid_layout(float width, float height) {
    GridLayout layout;
    if (width <= 0.0f || height <= 0.0f) return layout;
    // 9 x 7 at 4:3; more columns on wider screens, more rows on taller ones.
    layout.columns = odd_at_least(width / (height / 7.0f) / cell_ratio, 9);
    layout.rows = odd_at_least(height / (width / 9.0f) * cell_ratio, 7);
    if (width / height >= 4.0f / 3.0f) layout.rows = 7;
    else layout.columns = 9;
    const float cell_w = width / float(layout.columns), cell_h = height / float(layout.rows);
    const float half = height / 2.0f;
    layout.perspective = viewer_distance * height;
    // The sphere grows with the screen so the outer tiles never turn edge on.
    const float edge_x = (width - cell_w) / 2.0f / half, edge_y = (height - cell_h) / 2.0f / half;
    const float radius = std::max({sphere_radius, edge_x * 1.35f, edge_y * 1.35f});
    const float diagonal = float(layout.columns - 1 + layout.rows - 1);
    for (int row = 0; row < layout.rows; ++row) {
        for (int column = 0; column < layout.columns; ++column) {
            GridTile tile;
            tile.column = column;
            tile.row = row;
            tile.width = cell_w * tile_fill;
            tile.height = cell_h * tile_fill;
            const float cx = (float(column) + 0.5f) * cell_w, cy = (float(row) + 0.5f) * cell_h;
            tile.x = cx - tile.width / 2.0f;
            tile.y = cy - tile.height / 2.0f;
            // Offsets from the middle, in half screen heights.
            const float nx = (cx - width / 2.0f) / half, ny = (cy - height / 2.0f) / half;
            const float sx = std::clamp(nx / radius, -0.85f, 0.85f), sy = std::clamp(ny / radius, -0.85f, 0.85f);
            // Facing outwards: the right side turns its right edge away
            // (rotateY > 0), the lower side its lower edge (rotateX < 0).
            const float yaw = std::asin(sx), pitch = -std::asin(sy);
            tile.tilt_y = yaw * 180.0f / pi;
            tile.tilt_x = pitch * 180.0f / pi;
            tile.z = std::max(0.0f, sphere_depth * (1.0f - sx * sx - sy * sy)) * half;
            // Lit from the upper left: the face's normal after the tilt.
            const float nxn = std::cos(pitch) * std::sin(yaw), nyn = -std::sin(pitch), nzn = std::cos(pitch) * std::cos(yaw);
            const float facing = nxn * light_x + nyn * light_y + nzn * light_z;
            tile.shade = std::clamp(0.55f + 0.55f * facing, 0.55f, 1.15f);
            tile.variant = (column * 7 + row * 3 + (column * row) % 5) % 4;
            // The ripple runs from the top left corner to the bottom right.
            tile.delay = diagonal > 0.0f ? grid_ripple_seconds * float(column + row) / diagonal : 0.0f;
            layout.tiles.push_back(tile);
        }
    }
    return layout;
}

float grid_flip_progress(const GridTile& tile, float elapsed) {
    return std::clamp((elapsed - tile.delay) / grid_flip_seconds, 0.0f, 1.0f);
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

GridRgb MenuGrid::tile_colour(std::size_t index) const {
    if (index >= tiles_.size()) return {};
    const Tile& tile = tiles_[index];
    if (!tile.flipping) return tile.to;
    const float p = changing_ ? grid_flip_progress(layout_.tiles[index], float(last_elapsed_)) : 1.0f;
    return p >= 0.5f ? tile.to : tile.from;
}

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
    const GridRgb colour = grid_colour(colour_).value_or(GridRgb{});
    tiles_.assign(layout_.tiles.size(), Tile{});
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        Tile& tile = tiles_[i];
        tile.element = layer_->GetChild(int(i));
        tile.from = tile.to = colour;
        if (tile.element == nullptr) continue;
        const auto& place = layout_.tiles[i];
        tile.element->SetProperty("left", px(place.x));
        tile.element->SetProperty("top", px(place.y));
        tile.element->SetProperty("width", px(place.width));
        tile.element->SetProperty("height", px(place.height));
    }
    changing_ = false;
}

void MenuGrid::place(float elapsed) {
    last_elapsed_ = elapsed;
    bool still = false;
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        Tile& tile = tiles_[i];
        const GridTile& at = layout_.tiles[i];
        float angle = 0.0f;
        GridRgb shown = tile.to;
        if (tile.flipping) {
            const float p = grid_flip_progress(at, elapsed);
            if (p >= 1.0f) {
                tile.flipping = false;
                tile.from = tile.to;
            } else {
                still = true;
                // A half turn about the tile's upright axis; past the halfway
                // point the tile shows its back, in the new colour (drawn as
                // the front turned the other way, which looks the same).
                angle = 180.0f * ease_in_out(p);
                if (angle > 90.0f) angle -= 180.0f;
                else shown = tile.from;
            }
        }
        // Turned towards edge on, a tile catches less light.
        const float turn = std::abs(std::cos(angle * pi / 180.0f));
        const float shade = at.shade * (0.35f + 0.65f * turn);
        char transform[160];
        std::snprintf(transform, sizeof(transform), "translateZ(%.1fpx) rotateY(%.2fdeg) rotateX(%.2fdeg) rotateY(%.2fdeg)",
                      at.z, at.tilt_y, at.tilt_x, angle);
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
    const float fade = std::clamp(elapsed / grid_fade_seconds, 0.0f, 1.0f);
    if (far_ != nullptr) far_->SetProperty("background-color", css(mix(far_from_, far_to_, ease_in_out(fade)), 1.0f));
    changing_ = still || (fade < 1.0f && !same(far_from_, far_to_));
}

void MenuGrid::update(Rml::Context* context, const std::string& colour, double now) {
    const auto wanted = grid_colour(colour);
    if (!wanted) {
        if (document_ != nullptr && document_->IsVisible()) document_->Hide();
        visible_ = false;
        colour_.clear();
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
    if (colour_.empty()) {
        // Shown again (or for the first time): straight in the colour, still.
        colour_ = colour;
        far_from_ = far_to_ = grid_far_colour(*wanted);
        for (auto& tile : tiles_) tile.from = tile.to = *wanted, tile.flipping = false;
        change_at_ = now;
    } else if (colour != colour_) {
        // A new colour: the background fades from where it is, and each tile
        // flips from the colour it shows now (a tile part way through an
        // earlier flip starts again from there).
        const float elapsed = float(now - change_at_);
        far_from_ = mix(far_from_, far_to_, ease_in_out(std::clamp(elapsed / grid_fade_seconds, 0.0f, 1.0f)));
        far_to_ = grid_far_colour(*wanted);
        for (std::size_t i = 0; i < tiles_.size(); ++i) {
            Tile& tile = tiles_[i];
            tile.from = tile_colour(i);
            tile.to = *wanted;
            tile.flipping = !same(tile.from, tile.to);
        }
        colour_ = colour;
        change_at_ = now;
        changing_ = true;
    }
    if (context->GetDimensions() != size_ || tiles_.empty()) build(context);
    place(float(now - change_at_));
    if (!document_->IsVisible()) {
        document_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    // Behind every menu screen.
    document_->PushToBack();
    visible_ = true;
}

} // namespace blockplanet::ui
