#pragma once

// The jukebox's full-screen visualisers (the owner's request, 2026-11-17:
// "a few winamp inspired visualisations", using the game's own look). Drawn
// with RmlUi elements in the jukebox screens' #jbvis layer, built from the
// game's grey marble brick (the menu grid's tile texture, /blockplanet-grid-tile).

#include <RmlUi/Core.h>

#include <array>
#include <cmath>

namespace blockplanet::ui {

// The visualisers, after the grid's own (0 Water, 1 Pulse, 2 Bars).
enum JukeboxVisual { vis_water = 0, vis_pulse, vis_bars, vis_spectrum, vis_scope, vis_pieces, vis_vortex, vis_count };
// Whether a visualiser replaces the menu grid (full screen, on black; the
// Vortex is the game's own picture, src/graphics/vortex_backdrop.cpp, so its
// layer stays clear).
inline bool jukebox_full_screen(int mode) { return mode >= vis_spectrum; }
// The Vortex and, after it, the 25 stage backgrounds (modes 7 to 31).
inline bool jukebox_game_picture(int mode) { return mode >= vis_vortex; }

// The slow colour drift every visualiser shares (the owner, 2026-11-18: "changing
// their hue slowly over time"): degrees round the colour wheel at `now`
// (seconds), once round every three minutes, in steps of two degrees so the
// elements' colours (which rebuild their shapes) change only now and then.
// It starts from the visualisers' own colours (0) each time the jukebox opens.
float jukebox_hue(double now);
// Once a frame (the presentation thread): whether a jukebox screen is open.
void set_jukebox_hue_running(bool open, double now);
// A colour (0 to 1 each) turned `degrees` round the colour wheel, keeping its
// brightness: a rotation about the grey axis.
inline void hue_rotate(float& r, float& g, float& b, float degrees) {
    if (degrees == 0.0f) return;
    const float a = degrees * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    const float k = (1.0f - c) / 3.0f, q = s * 0.57735027f;  // about (1,1,1)/sqrt(3)
    const float nr = r * (c + k) + g * (k - q) + b * (k + q);
    const float ng = r * (k + q) + g * (c + k) + b * (k - q);
    const float nb = r * (k - q) + g * (k + q) + b * (c + k);
    r = nr > 0.0f ? nr : 0.0f, g = ng > 0.0f ? ng : 0.0f, b = nb > 0.0f ? nb : 0.0f;
}

struct JukeboxSound {
    std::array<float, 16> bands{};  // bass to treble, 0 to 1
    float level = 0.0f;             // overall, 0 to 1
    std::array<float, 128> wave{};  // -1 to 1
};

// Once a frame while a jukebox screen is on top: draws `mode` into the
// document's #jbvis (emptied for the grid's own modes).
void update_jukebox_visual(Rml::ElementDocument* document, int mode, const JukeboxSound& sound, double now);

} // namespace blockplanet::ui
