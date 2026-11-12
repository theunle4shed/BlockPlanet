// The box art on the grid (include/tetrisphere/box_art.h).
//
// The title routine (func_800685FC) draws its backdrop, the grid sphere, as a
// LightWave scene of its own (pointer at 0x800E1674, drawn first, before the
// depth clear) with func_8002D93C; the box art's parts follow. In the box
// art's place that call is skipped and a fill of the whole frame drawn
// instead. RT64 keeps the fill's alpha (1, the colour's low bit) and writes 0
// wherever the box art draws over it, which is the coverage the under layer
// lays the game's picture over BlockPlanet's grid by. The fill's colour is
// near the blue grid's tiles, as the box art's edges are already blended
// with it.

#include "tetrisphere/box_art.h"
#include "tetrisphere/direct_start.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"
#include "tetrisphere/under_layer.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <cstring>

namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t backdrop_scene_pointer = 0x800E1674u;
constexpr std::uint32_t display_list_pointer = 0x800F22B4u;

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_BOX_ART");
        return v == nullptr || v[0] != '0';
    }();
    return on;
}

// The title shows (the intro over, the main menu not yet reached).
bool on_title(const std::uint8_t* rdram) { return load16(rdram, 0x800E1648u) == 1 && load16(rdram, 0x800E1646u) == 0; }

// The intro scene's frame where it hands over (the last robot, Wheels, leaves
// on the right about here; the intro's own end is about 1230).
// BLOCKPLANET_BOX_ART_CUT=frame changes it, 0 keeps the intro to its end.
int cut_frame() {
    static const int frame = [] {
        const char* v = std::getenv("BLOCKPLANET_BOX_ART_CUT");
        return v ? std::atoi(v) : 1185;
    }();
    return frame;
}

std::uint32_t rgba5551(std::uint32_t rgb) {
    const std::uint32_t r = (rgb >> 16) & 0xFFu, g = (rgb >> 8) & 0xFFu, b = rgb & 0xFFu;
    return (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1u;
}

} // namespace

// Once a frame, from the game's main loop (direct_start_hooks.cpp): whether the
// box art shows, so the under layer follows the title exactly (the menus'
// first frames draw no scene at all).
extern "C" void blockplanet_box_art_update(const std::uint8_t* rdram) {
    const bool title = enabled() && on_title(rdram) && load16(rdram, 0x800E11F8u) == 0;
    tetrisphere::box_art::showing().store(title, std::memory_order_relaxed);
    tetrisphere::under_layer::wanted().store(title, std::memory_order_relaxed);
}

// From the draw filter (src/graphics/pause_draw_hooks.cpp), for each scene
// drawn by func_8002D93C: returns true when the call is replaced.
extern "C" bool blockplanet_box_art_scene(std::uint8_t* rdram, recomp_context* ctx) {
    // The intro flows into the box art (the owner's design, 2026-11-10): as
    // the last robot (Wheels) leaves the intro's scene, the intro is ended (at
    // the next frame's start, as it does at its own end, 0x800685BC) and the
    // title starts on the box art.
    if (enabled() && cut_frame() > 0 && load16(rdram, 0x800E1648u) == 0 && load16(rdram, 0x800E1646u) == 0) {
        const std::uint32_t intro = load32(rdram, 0x800E1670u);
        if (intro != 0 && static_cast<std::uint32_t>(ctx->r4) == intro && load32(rdram, 0x800E166Cu) != intro &&
            static_cast<int>(load32(rdram, intro + 0x24u)) >= cut_frame()) {
            // The hand-over is done at the start of the next frame, before
            // the intro runs (direct_start_hooks.cpp): changing the intro's
            // state here, inside its own frame, is not safe.
            if (!tetrisphere::box_art::handover_pending().exchange(true)) {
                tetrisphere::direct_start::log_event("{\"event\":\"box_art_from_intro\",\"frame\":" +
                                                     std::to_string(static_cast<int>(load32(rdram, intro + 0x24u))) + "}");
            }
            ctx->r2 = 0;
            return true;  // not drawn: the next frame is the title's
        }
    }
    const bool title = enabled() && on_title(rdram);
    const std::uint32_t backdrop = load32(rdram, backdrop_scene_pointer);
    if (!title || backdrop == 0 || static_cast<std::uint32_t>(ctx->r4) != backdrop) return false;
    std::uint32_t dl = load32(rdram, display_list_pointer);
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
        store32(rdram, dl, w0);
        store32(rdram, dl + 4u, w1);
        dl += 8u;
    };
    const std::uint32_t colour = rgba5551(0x0B121Au);
    cmd(0xE7000000u, 0);                                   // pipe sync
    cmd(0xBA001402u, 0x00300000u);                         // fill mode
    cmd(0xF7000000u, (colour << 16) | colour);             // fill colour (alpha 1)
    cmd(0xF6000000u | (319u << 14) | (239u << 2), 0);      // the whole 320 x 240 frame
    cmd(0xE7000000u, 0);
    cmd(0xBA001402u, 0x00000000u);                         // one-cycle again
    store32(rdram, display_list_pointer, dl);
    ctx->r2 = 0;  // "animation finished": no
    return true;
}
