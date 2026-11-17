#pragma once

// The jukebox's full-screen visualisers (the owner's request, 2026-11-17:
// "a few winamp inspired visualisations", using the game's own look). Drawn
// with RmlUi elements in the jukebox screens' #jbvis layer, built from the
// game's grey marble brick (the menu grid's tile texture, /blockplanet-grid-tile).

#include <RmlUi/Core.h>

#include <array>

namespace blockplanet::ui {

// The visualisers, after the grid's own (0 Water, 1 Pulse, 2 Bars).
enum JukeboxVisual { vis_water = 0, vis_pulse, vis_bars, vis_spectrum, vis_scope, vis_pieces, vis_count };
// Whether a visualiser replaces the menu grid (full screen, on black).
inline bool jukebox_full_screen(int mode) { return mode >= vis_spectrum; }

struct JukeboxSound {
    std::array<float, 16> bands{};  // bass to treble, 0 to 1
    float level = 0.0f;             // overall, 0 to 1
    std::array<float, 128> wave{};  // -1 to 1
};

// Once a frame while a jukebox screen is on top: draws `mode` into the
// document's #jbvis (emptied for the grid's own modes).
void update_jukebox_visual(Rml::ElementDocument* document, int mode, const JukeboxSound& sound, double now);

} // namespace blockplanet::ui
