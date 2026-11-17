// The jukebox's Vortex background (include/tetrisphere/vortex_backdrop.h).
//
// The game's reset screen (func_8005C410, run by the top-level loop while
// 0x800E11F8 is set) rewinds the model pool to the mark after the common
// assets (0x801026B8), copies the last picture to its fade buffer, and, with
// the VORTEX flag (0x800E12F4) on and 300 frames counted, builds scene 0x130
// and plays song 0x143 on the jingle player, drawing the scene on a loop.
// On a console a restart follows; here it is undone instead:
//   entering  save the pool from the mark to its top and the values the reset screen changes, then
//             set the flag, the reset screen's flag and its counters (past
//             the 300 frames, so the vortex comes at once);
//   on        black out the copied picture, stop the vortex's song once it
//             starts (the jukebox's track plays on, on the other player);
//   leaving   put everything back.

#include "tetrisphere/vortex_backdrop.h"
#include "tetrisphere/audio_settings.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

extern "C" {
void func_8007ADF8(std::uint8_t* rdram, recomp_context* ctx);
void func_8002A0C0(std::uint8_t* rdram, recomp_context* ctx);
void func_8002A100(std::uint8_t* rdram, recomp_context* ctx);
void func_8002CF70(std::uint8_t* rdram, recomp_context* ctx);
void func_8002D93C(std::uint8_t* rdram, recomp_context* ctx);
}

namespace blockplanet {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t reset_screen = 0x800E11F8u;  // u16: the top loop runs the reset screen
constexpr std::uint32_t vortex_flag = 0x800E12F4u;   // u16: the VORTEX name code
constexpr std::uint32_t timer_a = 0x800E1208u, timer_b = 0x800E120Cu;
constexpr std::uint32_t vortex_started = 0x800E11FCu;  // u16: the scene built, its song started
constexpr std::uint32_t pool_top = 0x800F22C0u;      // the model pool's next free byte
constexpr std::uint32_t pool_mark = 0x801026B8u;     // the pool after the common assets
constexpr std::uint32_t fade_picture = 0x800DFF00u;  // the reset screen's copy of the last picture
constexpr std::uint32_t reset_signal = 0x800DF728u;
constexpr std::uint32_t jingle_player = 0x80109DC0u;
constexpr std::uint32_t scene_row = 0x800E121Cu;     // -1 in play

// The values the reset screen changes, saved whole.
constexpr std::pair<std::uint32_t, std::uint32_t> saved_ranges[] = {
    {0x800DFEF0u, 0x800DFF20u},  // its fade picture, state and colours
    {0x800E11F0u, 0x800E1270u},  // its flags, the counters, the scene pointers
    {0x800E12A0u, 0x800E12D8u},  // the scene's object list
    {0x800E12F0u, 0x800E12F8u},  // the VORTEX flag
    {0x800E28C8u, 0x800E28D8u},
    {0x8010B330u, 0x8010B360u},  // the effects player's settings it changes
    {0x800F22C0u, 0x800F22C4u},  // the pool's top
};

enum class State { off, on };
std::atomic<bool> wanted{false};
std::atomic<int> wanted_stage{0};  // 0 the vortex, 1 to 25 a play background
int shown_stage = 0;               // what the reset screen draws now (game thread)
bool stage_built = false;
std::atomic<bool> showing{false};
State state = State::off;
bool song_stopped = false;
int frames_on = 0;
std::uint32_t saved_base = 0;
std::vector<std::uint8_t> saved_pool;
std::vector<std::vector<std::uint8_t>> saved_values;

gpr address(std::uint32_t value) { return static_cast<gpr>(static_cast<std::int64_t>(static_cast<std::int32_t>(value))); }

void copy_out(const std::uint8_t* rdram, std::uint32_t from, std::uint32_t to, std::vector<std::uint8_t>& out) {
    out.resize(to - from);
    for (std::uint32_t a = from; a < to; ++a) out[a - from] = load8(rdram, a);
}

void copy_in(std::uint8_t* rdram, std::uint32_t from, const std::vector<std::uint8_t>& in) {
    for (std::uint32_t i = 0; i < in.size(); ++i) store8(rdram, from + i, in[i]);
}

bool at_title_or_menus(const std::uint8_t* rdram) {
    // The title, or the game's menus (not play, not its pause).
    if (load16(rdram, 0x800E1646u) == 0) return true;
    return load16(rdram, scene_row) >= 0;  // -1 in play (its pause included)
}

void enter(std::uint8_t* rdram) {
    const std::uint32_t base = load32(rdram, pool_mark), top = load32(rdram, pool_top);
    if ((base >> 24) != 0x80u || top < base || top - base > 0x600000u) {
        std::fprintf(stderr, "{\"event\":\"vortex_refused\",\"base\":\"%08X\",\"top\":\"%08X\"}\n", base, top);
        wanted.store(false);
        return;
    }
    // No wait for the graphics tasks in flight here: taking the scheduler's
    // messages out of turn (func_8002A140) crashed the renderer about every
    // other time; the reset screen waits itself before it rewrites the pool,
    // and saving only reads.
    saved_base = base;
    copy_out(rdram, base, top, saved_pool);
    saved_values.clear();
    for (const auto& [from, to] : saved_ranges) {
        saved_values.emplace_back();
        copy_out(rdram, from, to, saved_values.back());
    }
    store16(rdram, vortex_flag, 1);
    store32(rdram, timer_a, 0);
    store32(rdram, timer_b, 0);
    store16(rdram, reset_screen, 1);
    state = State::on;
    song_stopped = false;
    frames_on = 0;
    shown_stage = wanted_stage.load();
    stage_built = false;
    showing.store(true);
    std::fprintf(stdout, "{\"event\":\"vortex_backdrop\",\"on\":true,\"saved_bytes\":%zu,\"started\":%d,\"scene\":\"%08X\",\"fade\":%d,\"init\":%d}\n",
                 saved_pool.size(), load16(rdram, vortex_started), load32(rdram, 0x800E1264u), load16(rdram, 0x800DFF08u), load16(rdram, 0x800DFD5Cu));
    std::fflush(stdout);
}

void leave(std::uint8_t* rdram, recomp_context* ctx) {
    // No wait for the graphics tasks here either (see enter): the pool is put
    // back at the top of the game's frame, before this frame's drawing.
    {  // the jingle player silenced (the vortex's song, if it started)
        const recomp_context saved = *ctx;
        ctx->r4 = address(jingle_player);
        func_8007ADF8(rdram, ctx);
        *ctx = saved;
    }
    copy_in(rdram, saved_base, saved_pool);
    std::size_t i = 0;
    for (const auto& [from, to] : saved_ranges) copy_in(rdram, from, saved_values[i++]);
    store32(rdram, reset_signal, 0);
    saved_pool.clear();
    saved_pool.shrink_to_fit();
    state = State::off;
    showing.store(false);
    std::fprintf(stdout, "{\"event\":\"vortex_backdrop\",\"on\":false}\n");
}

} // namespace

// Centring (the owner's request, 2026-11-17): the scene's camera looks a
// little to one side, so on a wide screen the ring sat right of centre. Each
// frame, at its first object, the camera matrix (scene +0xF8, double-buffered
// by +0x3D; N64 fixed point, row vectors) is sheared: x' = x + k z in view
// space, which moves the whole picture sideways by the same amount at every
// depth. The game builds the camera afresh each frame, so nothing builds up.
float vortex_shift() {
    static const float shift = [] {
        const char* v = std::getenv("BLOCKPLANET_VORTEX_SHIFT");  // research: tuning
        return v != nullptr ? std::strtof(v, nullptr) : 0.27f;  // measured: the ring from 69% across a 16:9 screen to the middle
    }();
    return shift;
}

double mtx_get(const std::uint8_t* rdram, std::uint32_t at, int i, int j) {
    const int k = i * 4 + j;
    const std::int32_t fixed = static_cast<std::int32_t>(
        (std::uint32_t(std::uint16_t(load16(rdram, at + 2u * k))) << 16) | std::uint16_t(load16(rdram, at + 32u + 2u * k)));
    return fixed / 65536.0;
}

void mtx_set(std::uint8_t* rdram, std::uint32_t at, int i, int j, double value) {
    const int k = i * 4 + j;
    const auto fixed = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::llround(value * 65536.0)));
    store16(rdram, at + 2u * k, static_cast<std::int16_t>(fixed >> 16));
    store16(rdram, at + 32u + 2u * k, static_cast<std::int16_t>(fixed & 0xFFFFu));
}

// Wider than 16:9 the tunnel's ends show in the corners: the picture is
// zoomed in by the extra width (about the middle, so the ring stays centred).
std::atomic<float> window_aspect{16.0f / 9.0f};
void set_vortex_aspect(float aspect) {
    if (aspect > 0.1f && aspect < 10.0f) window_aspect.store(aspect, std::memory_order_relaxed);
}

void set_vortex_backdrop(bool on, int stage) {
    static const bool off = std::getenv("BLOCKPLANET_VORTEX_OFF") != nullptr;  // research: the jukebox side only
    wanted_stage.store(stage >= 1 && stage <= 25 ? stage : 0);
    wanted.store(on && !off);
}
bool vortex_backdrop_on() { return showing.load(); }

} // namespace blockplanet

// From the top of the game's top-level loop (blockplanet_attract_guard,
// 0x80070180), once a frame on the game's thread.
extern "C" void blockplanet_vortex_frame(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace blockplanet;
    using namespace tetrisphere::guest;
    const bool want = wanted.load();
    if (state == State::off) {
        if (want && load16(rdram, reset_screen) == 0 && at_title_or_menus(rdram)) enter(rdram);
        return;
    }
    if (!want || wanted_stage.load() != shown_stage) {
        leave(rdram, ctx);  // (a different background: entered again next frame)
        return;
    }
    ++frames_on;
    static const bool log = std::getenv("BLOCKPLANET_VORTEX_LOG") != nullptr;
    if (log && (frames_on <= 40 || frames_on % 30 == 0))
        std::fprintf(stdout, "{\"event\":\"vortex_frame\",\"n\":%d,\"reset\":%d,\"started\":%d,\"t1208\":%u,\"t120C\":%u,\"t1210\":%u,\"scene\":\"%08X\",\"fade\":%d,\"cnt\":%d}\n",
                     frames_on, load16(rdram, reset_screen), load16(rdram, vortex_started), load32(rdram, timer_a), load32(rdram, timer_b),
                     load32(rdram, 0x800E1210u), load32(rdram, 0x800E1264u), load16(rdram, 0x800DFF08u), load16(rdram, 0x800E123Eu));
    if (log) std::fflush(stdout);
    // Through the reset screen's 300-frame wait at once, and its counters kept
    // running (BlockPlanet's menus hold them otherwise: linux_main.cpp).
    // Set just short of the wait's end (301), so the scene's first draws step
    // it a frame at a time: jumping past it made the first draw step ~300
    // frames at once, and the game's scene player crashed now and then.
    if (frames_on == 2) store32(rdram, timer_b, 298);
    // The copied picture (320 x 240, 16-bit) filled dark, so only the vortex shows.
    static const bool keep_picture = std::getenv("BLOCKPLANET_VORTEX_KEEP_PICTURE") != nullptr;  // research
    if (frames_on <= 3 && !keep_picture) {
        const std::uint32_t picture = load32(rdram, fade_picture);
        if ((picture >> 24) == 0x80u)
            {
            // RGBA 5551 with the alpha bit set (zero, transparent, hid the scene too).
            static const int fill = [] {  // research: BLOCKPLANET_VORTEX_FILL=zero|dark
                const char* v = std::getenv("BLOCKPLANET_VORTEX_FILL");
                return v != nullptr && std::string(v) == "zero" ? 0 : v != nullptr && std::string(v) == "white" ? 2 : 1;
            }();
            const std::uint32_t pixel = fill == 0 ? 0u : fill == 2 ? 0xFFFFu : (1u << 11) | (1u << 6) | (4u << 1) | 1u;  // a very dark blue
            for (std::uint32_t at = picture; at < picture + 320u * 240u * 2u; at += 4) store32(rdram, at, pixel << 16 | pixel);
        }
    }
    if (!song_stopped && load16(rdram, vortex_started) != 0) {
        const recomp_context saved = *ctx;
        ctx->r4 = address(jingle_player);
        func_8007ADF8(rdram, ctx);
        *ctx = saved;
        song_stopped = true;
        // The reset screen took the jingle player from the jukebox's extra
        // song, if one was playing (audio_hooks.cpp): start it again.
        if (const int song = extra_song(); song >= 0) play_song(song);
    }
    // A play background instead (the jukebox's Stage backgrounds): built once
    // the vortex is (the reset screen's pool and lights then set up), as the
    // game's own loader does (func_8006EE6C: scene and object ids from the
    // tables at 0x800E2262 and 0x800E2296, one object, the pool aligned to 16,
    // the built scene's byte +0x49 cleared), and put where the reset screen
    // draws its scene from (0x800E1264).
    if (shown_stage > 0 && !stage_built && song_stopped) {
        stage_built = true;
        const auto scene_id = static_cast<std::uint16_t>(load16(rdram, 0x800E2262u + 2u * std::uint32_t(shown_stage)));
        const auto object_id = static_cast<std::uint16_t>(load16(rdram, 0x800E2296u + 2u * std::uint32_t(shown_stage)));
        const recomp_context saved = *ctx;
        ctx->r4 = scene_id;
        func_8002A0C0(rdram, ctx);
        const std::uint32_t scene = static_cast<std::uint32_t>(ctx->r2);
        *ctx = saved;
        ctx->r4 = object_id;
        func_8002A100(rdram, ctx);
        const std::uint32_t object = static_cast<std::uint32_t>(ctx->r2);
        *ctx = saved;
        // The object list and the size out, in the pool just before the build.
        const std::uint32_t scratch = (load32(rdram, pool_top) + 15u) & ~15u;
        store32(rdram, scratch, object);
        store32(rdram, scratch + 4u, 0);
        const std::uint32_t build_at = scratch + 16u;
        ctx->r29 = static_cast<gpr>(static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(ctx->r29) - 0x40u)));
        store32(rdram, static_cast<std::uint32_t>(ctx->r29) + 0x10u, scratch + 4u);
        ctx->r4 = address(build_at);
        ctx->r5 = address(scene);
        ctx->r6 = address(scratch);
        ctx->r7 = 1;
        func_8002CF70(rdram, ctx);
        const std::uint32_t built = static_cast<std::uint32_t>(ctx->r2);
        *ctx = saved;
        const std::uint32_t size = load32(rdram, scratch + 4u);
        std::fprintf(stdout, "{\"event\":\"vortex_stage\",\"stage\":%d,\"scene_id\":%u,\"object_id\":%u,\"built\":\"%08X\",\"size\":%u}\n",
                     shown_stage, scene_id, object_id, built, size);
        std::fflush(stdout);
        if ((built >> 24) == 0x80u && size > 0 && size < 0x400000u) {
            store32(rdram, pool_top, build_at + size);
            store8(rdram, built + 0x49u, 0);
            store32(rdram, built + 0x24u, 0);  // its frame
            store32(rdram, 0x800E1264u, built);
        }
    }
}

// From the scene object hook (pause_draw_hooks.cpp, blockplanet_scene_object),
// before each object of a scene is drawn.
extern "C" void blockplanet_vortex_object(std::uint8_t* rdram, std::uint32_t scene, int object) {
    using namespace blockplanet;
    using namespace tetrisphere::guest;
    (void)object;
    if (!showing.load(std::memory_order_relaxed) || scene == 0 || scene != load32(rdram, 0x800E1264u)) return;
    const float k = shown_stage == 0 ? vortex_shift() : 0.0f;  // the play backgrounds are centred
    // With a margin (8%) once wider: the camera turns, and a corner showed at 21:9 without it.
    const float wider = window_aspect.load(std::memory_order_relaxed) / (16.0f / 9.0f);
    const float zoom = wider > 1.01f ? wider * 1.08f : 1.0f;
    // Once a frame: the scene's frame and the matrix buffer in use.
    static std::uint32_t done_frame = 0xFFFFFFFFu, done_sel = 0xFFu;
    const std::uint32_t frame = load32(rdram, scene + 0x24u);
    const std::uint8_t sel = load8(rdram, scene + 0x3Du) & 1u;
    if (frame == done_frame && sel == done_sel) return;
    done_frame = frame, done_sel = sel;
    const std::uint32_t cam = scene + 0xF8u + sel * 0x40u;
    for (int i = 0; i < 4; ++i) {
        mtx_set(rdram, cam, i, 0, zoom * (mtx_get(rdram, cam, i, 0) + k * mtx_get(rdram, cam, i, 2)));
        if (zoom != 1.0f) mtx_set(rdram, cam, i, 1, zoom * mtx_get(rdram, cam, i, 1));
    }
}

// The stage backgrounds' brightness (the owner: "match how they look in
// play"). In play a background is drawn into an off-screen picture that is
// then copied to the screen as tiles, and it comes out twice as bright as the
// same scene drawn straight, as the reset screen does (measured on Training's
// background 25: x2.0 to x2.2 in every channel, at every level). So each
// layer's colour is brightened at the colour mixer. The game mixes in two
// steps: (TEXEL1 - TEXEL0) * LOD + TEXEL0 (the texture's two detail levels),
// then COMBINED * SHADE. It becomes TEXEL0 * SHADE (the detail-level blend
// dropped), then (1 - COMBINED) * COMBINED + COMBINED: c (2 - c), twice the
// colour while it is dark (most of these backgrounds), easing off towards
// white. A plain doubling (COMBINED * ENV + COMBINED) overflowed in the bright
// parts: wrong colours (yellow edges, green patches) instead of white.
// Also tried, and no better: doubling the lights (the shading tops out),
// changing the layers' blend (no effect here), a full-screen rectangle adding
// the screen to itself (no effect here). The commands are changed in place
// (sub-lists hung or crashed: the model pool is restored when the background
// changes, and a jump in the game's own list stopped its frames). Called from
// the scene draw filter (pause_draw_hooks.cpp) for the reset screen's scene:
// the scene is drawn here and its commands then changed. Returns whether it drew.
extern "C" bool blockplanet_vortex_draw(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace blockplanet;
    using namespace tetrisphere::guest;
    if (!showing.load(std::memory_order_relaxed) || shown_stage == 0 || !stage_built) return false;
    if (static_cast<std::uint32_t>(ctx->r4) != load32(rdram, 0x800E1264u)) return false;
    static bool inside = false;  // the draw below comes back through the filter
    if (inside) return false;
    constexpr std::uint32_t cursor = 0x800F22B4u;  // the game's next free display-list command
    std::uint32_t at = load32(rdram, cursor);
    if ((at >> 24) != 0x80u) return false;
    const std::uint32_t from = at;
    const recomp_context saved = *ctx;
    ctx->r29 = static_cast<gpr>(static_cast<std::int32_t>(saved.r29) - 0x80);
    inside = true;
    func_8002D93C(rdram, ctx);
    inside = false;
    const auto result = ctx->r2;
    *ctx = saved;
    ctx->r2 = result;
    const std::uint32_t to = load32(rdram, cursor);
    if (to < from || to - from > 0x10000u) return true;
    static const bool off = std::getenv("BLOCKPLANET_STAGE_LIGHT") != nullptr && std::getenv("BLOCKPLANET_STAGE_LIGHT")[0] == '0';
    if (off) return true;
    for (std::uint32_t a = from; a + 8u <= to; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if (w0 == 0xFC26A004u && w1 == 0x1F0C93FFu) {
            store32(rdram, a, 0xFC1220C0u);
            store32(rdram, a + 4u, 0xF00F923Fu);
        }
    }
    return true;
}
