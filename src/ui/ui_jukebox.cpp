// The jukebox's full-screen visualisers (ui_jukebox.h). Each is built once
// as plain elements when chosen, then only their positions and colours
// change each frame:
//   Spectrum  Winamp's classic analyser as a wall of the game's bricks, 16
//             columns lit from the bottom (blue, teal, yellow, orange, red),
//             each with a white peak brick that falls back slowly.
//   Scope     Winamp's oscilloscope: the sound wave traced in bricks, with a
//             fainter trace of the frame before.
//   Pieces    the game's pieces (in its brick colours) dropping through the
//             dark, a new one on each beat, faster when the music is loud.

#include "ui_jukebox.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace blockplanet::ui {
namespace {

constexpr int columns = 16, rows = 12;
constexpr int scope_points = 64;
constexpr int piece_pool = 28;

std::string brick(const std::string& id, int variant = 0) {
    const int tile = 96;  // the atlas's tile size (grid_texture.h)
    return "<img class=\"jbbrick\" id=\"" + id + "\" src=\"/blockplanet-grid-tile\" rect=\"" +
           std::to_string((variant % 4) * tile) + " 0 " + std::to_string(tile) + " " + std::to_string(tile) + "\"/>";
}

std::string pct(float v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.3f%%", v);
    return text;
}

std::string colour(int r, int g, int b, int a = 255) {
    char text[16];
    std::snprintf(text, sizeof text, "#%02x%02x%02x%02x", std::clamp(r, 0, 255), std::clamp(g, 0, 255),
                  std::clamp(b, 0, 255), std::clamp(a, 0, 255));
    return text;
}

// The texture's middle grey is half white, so a tint of twice a colour shows it.
std::string tint(int r, int g, int b, int a = 255) { return colour(r * 2, g * 2, b * 2, a); }

// Row colours, bottom to top, in the menus' palette.
void row_colour(int row, int& r, int& g, int& b) {
    static const int stops[5][3] = {{40, 80, 200}, {30, 160, 170}, {232, 184, 24}, {245, 130, 31}, {220, 40, 40}};
    const float t = float(row) / float(rows - 1) * 4.0f;
    const int i = std::min(3, int(t));
    const float f = t - float(i);
    r = int(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f);
    g = int(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f);
    b = int(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f);
}

// The game's piece colours (bright versions of its bricks).
const int piece_colours[6][3] = {{220, 50, 50}, {50, 110, 230}, {60, 190, 80}, {235, 200, 40}, {170, 70, 210}, {240, 130, 30}};
// Four-brick shapes (x, y), as in the game's piece set.
const int shapes[7][4][2] = {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {{0, 0}, {1, 0}, {2, 0}, {1, 1}},
                             {{0, 0}, {1, 0}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {0, 1}, {1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
                             {{2, 0}, {0, 1}, {1, 1}, {2, 1}}};

struct Piece {
    bool live = false;
    float x = 0, y = 0, speed = 0, angle = 0, spin = 0, size = 0;
};

struct State {
    int built = -1;  // the mode the layer holds
    Rml::Element* layer = nullptr;
    std::vector<Rml::Element*> bricks;  // spectrum: column * rows + row; scope: points then ghosts
    std::vector<Rml::Element*> pieces;
    std::array<float, columns> peak{};
    std::array<float, columns> peak_at{};
    std::array<float, scope_points> ghost{};
    std::vector<Piece> falling;
    double last = 0.0, last_spawn = 0.0;
    float average = 0.0f;
    std::mt19937 random{12345};
} state;

void build(Rml::Element* layer, int mode) {
    state.layer = layer;
    state.built = mode;
    state.bricks.clear();
    state.pieces.clear();
    std::string rml;
    if (mode == vis_spectrum) {
        for (int c = 0; c < columns; ++c)
            for (int r = 0; r < rows; ++r) rml += brick("b" + std::to_string(c * rows + r), c + r);
    } else if (mode == vis_scope) {
        for (int i = 0; i < scope_points * 2; ++i) rml += brick("b" + std::to_string(i), i);
    } else if (mode == vis_pieces) {
        for (int p = 0; p < piece_pool; ++p) {
            rml += "<div class=\"jbpiece\" id=\"p" + std::to_string(p) + "\">";
            for (int k = 0; k < 4; ++k) rml += brick("p" + std::to_string(p) + "k" + std::to_string(k), p + k);
            rml += "</div>";
        }
    }
    layer->SetInnerRML(rml);
    layer->SetClass("full", jukebox_full_screen(mode) && !jukebox_game_picture(mode));
    if (mode == vis_spectrum || mode == vis_scope) {
        const int count = mode == vis_spectrum ? columns * rows : scope_points * 2;
        for (int i = 0; i < count; ++i) state.bricks.push_back(layer->GetElementById("b" + std::to_string(i)));
    }
    if (mode == vis_spectrum) {
        // A wall across the screen: each brick a share of the width, square.
        for (int c = 0; c < columns; ++c)
            for (int r = 0; r < rows; ++r) {
                Rml::Element* e = state.bricks[std::size_t(c * rows + r)];
                if (e == nullptr) continue;
                e->SetProperty("left", pct(4.0f + 92.0f * float(c) / columns));
                e->SetProperty("width", pct(92.0f / columns - 0.6f));
                e->SetProperty("bottom", pct(6.0f + 7.2f * float(r)));
                e->SetProperty("height", pct(6.6f));
            }
        state.peak.fill(0.0f);
    }
    if (mode == vis_pieces) {
        state.falling.assign(piece_pool, Piece{});
        for (int p = 0; p < piece_pool; ++p) state.pieces.push_back(layer->GetElementById("p" + std::to_string(p)));
    }
}

void draw_spectrum(const JukeboxSound& sound, float dt) {
    for (int c = 0; c < columns; ++c) {
        // Spread out: the bands mostly sit between a third and full.
        const float v = std::clamp((sound.bands[std::size_t(c)] - 0.3f) / 0.7f, 0.0f, 1.0f);
        const float lit = v * rows;
        if (lit >= state.peak[std::size_t(c)]) state.peak[std::size_t(c)] = lit, state.peak_at[std::size_t(c)] = 0.0f;
        else {
            // The peak waits a moment, then falls faster and faster.
            state.peak_at[std::size_t(c)] += dt;
            if (state.peak_at[std::size_t(c)] > 0.35f)
                state.peak[std::size_t(c)] = std::max(0.0f, state.peak[std::size_t(c)] - dt * 9.0f * (state.peak_at[std::size_t(c)] - 0.3f) * 3.0f);
        }
        const int peak_row = std::min(rows - 1, int(state.peak[std::size_t(c)]));
        for (int r = 0; r < rows; ++r) {
            Rml::Element* e = state.bricks[std::size_t(c * rows + r)];
            if (e == nullptr) continue;
            int cr, cg, cb;
            row_colour(r, cr, cg, cb);
            std::string shown;
            if (float(r) < lit) shown = tint(cr, cg, cb);
            else if (r == peak_row && state.peak[std::size_t(c)] > 0.5f) shown = tint(230, 230, 240);
            else shown = tint(cr / 5, cg / 5, cb / 5, 120);
            e->SetProperty("image-color", shown);
        }
    }
}

void draw_scope(const JukeboxSound& sound) {
    const float size = 1.2f;  // percent of the width
    for (int i = 0; i < scope_points; ++i) {
        const float v = sound.wave[std::size_t(i * 2)];
        Rml::Element* e = state.bricks[std::size_t(i)];
        Rml::Element* g = state.bricks[std::size_t(scope_points + i)];
        const float x = 3.0f + 94.0f * float(i) / float(scope_points - 1);
        if (e != nullptr) {
            e->SetProperty("left", pct(x - size / 2));
            e->SetProperty("width", pct(size));
            e->SetProperty("height", pct(size * 16.0f / 9.0f));
            e->SetProperty("top", pct(50.0f - v * 38.0f - size));
            const float a = std::abs(v);
            e->SetProperty("image-color", tint(int(232 + 10 * a), int(184 - 120 * a), int(24 + 20 * a)));
        }
        if (g != nullptr) {
            g->SetProperty("left", pct(x - size / 2));
            g->SetProperty("width", pct(size));
            g->SetProperty("height", pct(size * 16.0f / 9.0f));
            g->SetProperty("top", pct(50.0f - state.ghost[std::size_t(i)] * 38.0f - size));
            g->SetProperty("image-color", tint(40, 90, 200, 110));
        }
        state.ghost[std::size_t(i)] = state.ghost[std::size_t(i)] * 0.6f + v * 0.4f;
    }
}

void draw_pieces(const JukeboxSound& sound, float dt, double now) {
    // A beat: the level jumps above its recent average.
    state.average = state.average * 0.95f + sound.level * 0.05f;
    const bool beat = sound.level > state.average * 1.25f + 0.03f && now - state.last_spawn > 0.18;
    const bool idle = now - state.last_spawn > 0.9;
    if ((beat || idle) && sound.level > 0.02f) {
        for (std::size_t p = 0; p < state.falling.size(); ++p) {
            Piece& piece = state.falling[p];
            if (piece.live) continue;
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            piece.live = true;
            piece.size = 2.2f + u(state.random) * 2.4f;  // brick size, percent of the width
            piece.x = 4.0f + u(state.random) * 88.0f;
            piece.y = -15.0f;
            piece.speed = 14.0f + u(state.random) * 10.0f;
            piece.angle = u(state.random) * 360.0f;
            piece.spin = (u(state.random) - 0.5f) * 90.0f;
            const int shape = int(u(state.random) * 7) % 7;
            const int* c = piece_colours[int(u(state.random) * 6) % 6];
            Rml::Element* e = state.pieces[p];
            if (e != nullptr) {
                for (int k = 0; k < 4; ++k) {
                    Rml::Element* b = e->GetChild(k);
                    if (b == nullptr) continue;
                    b->SetProperty("left", pct(25.0f * float(shapes[shape][k][0])));
                    b->SetProperty("top", pct(25.0f * float(shapes[shape][k][1]) + 25.0f));
                    b->SetProperty("image-color", tint(c[0], c[1], c[2]));
                }
            }
            state.last_spawn = now;
            break;
        }
    }
    for (std::size_t p = 0; p < state.falling.size(); ++p) {
        Piece& piece = state.falling[p];
        Rml::Element* e = state.pieces[p];
        if (e == nullptr) continue;
        if (!piece.live) {
            e->SetProperty("display", "none");
            continue;
        }
        // Faster when the music is loud.
        piece.y += piece.speed * (0.5f + sound.level * 2.5f) * dt;
        piece.angle += piece.spin * dt;
        if (piece.y > 110.0f) {
            piece.live = false;
            e->SetProperty("display", "none");
            continue;
        }
        e->SetProperty("display", "block");
        e->SetProperty("left", pct(piece.x));
        e->SetProperty("top", pct(piece.y));
        e->SetProperty("width", pct(piece.size * 4.0f));
        e->SetProperty("height", pct(piece.size * 4.0f * 16.0f / 9.0f));
        char transform[48];
        std::snprintf(transform, sizeof transform, "rotate(%.1fdeg)", piece.angle);
        e->SetProperty("transform", transform);
    }
}

} // namespace

void update_jukebox_visual(Rml::ElementDocument* document, int mode, const JukeboxSound& sound, double now) {
    if (document == nullptr) return;
    Rml::Element* layer = document->GetElementById("jbvis");
    if (layer == nullptr) return;
    if (layer != state.layer || mode != state.built) build(layer, mode);
    const float dt = state.last > 0.0 ? float(std::clamp(now - state.last, 0.0, 0.1)) : 0.016f;
    state.last = now;
    if (mode == vis_spectrum) draw_spectrum(sound, dt);
    else if (mode == vis_scope) draw_scope(sound);
    else if (mode == vis_pieces) draw_pieces(sound, dt, now);
}

} // namespace blockplanet::ui
