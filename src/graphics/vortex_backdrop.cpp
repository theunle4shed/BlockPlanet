// The jukebox's Vortex background (include/tetrisphere/vortex_backdrop.h).
//
// The game's reset screen (func_8005C410, run by the top-level loop while
// 0x800E11F8 is set) rewinds the model pool to the mark after the common
// assets (0x801026B8), copies the last picture to its fade buffer, and, with
// the VORTEX flag (0x800E12F4) on and 300 frames counted, builds scene 0x130
// and plays song 0x143 on the jingle player, drawing the scene on a loop.
// On a console a restart follows; here it is undone instead:
//   entering  wait for the graphics tasks in flight, save the pool from the
//             mark to its top and the values the reset screen changes, then
//             set the flag, the reset screen's flag and its counters (past
//             the 300 frames, so the vortex comes at once);
//   on        black out the copied picture, stop the vortex's song once it
//             starts (the jukebox's track plays on, on the other player);
//   leaving   wait for the graphics tasks, put everything back.

#include "tetrisphere/vortex_backdrop.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

extern "C" {
void func_8002A140(std::uint8_t* rdram, recomp_context* ctx);
void func_8007ADF8(std::uint8_t* rdram, recomp_context* ctx);
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
constexpr std::uint32_t tasks_sent = 0x800DF710u, tasks_done = 0x800DF714u;
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

// As the game does before rebuilding models: wait for the graphics tasks sent.
void wait_for_graphics(std::uint8_t* rdram, recomp_context* ctx) {
    for (int guard = 0; guard < 120 && static_cast<std::int32_t>(load32(rdram, tasks_done)) <
                                           static_cast<std::int32_t>(load32(rdram, tasks_sent)); ++guard) {
        const recomp_context saved = *ctx;
        func_8002A140(rdram, ctx);
        *ctx = saved;
    }
}

bool at_title_or_menus(const std::uint8_t* rdram) {
    // The title, or the game's menus (not play, not its pause).
    if (load16(rdram, 0x800E1646u) == 0) return true;
    return load16(rdram, scene_row) >= 0;  // -1 in play (its pause included)
}

void enter(std::uint8_t* rdram, recomp_context* ctx) {
    const std::uint32_t base = load32(rdram, pool_mark), top = load32(rdram, pool_top);
    if ((base >> 24) != 0x80u || top < base || top - base > 0x600000u) {
        std::fprintf(stderr, "{\"event\":\"vortex_refused\",\"base\":\"%08X\",\"top\":\"%08X\"}\n", base, top);
        wanted.store(false);
        return;
    }
    wait_for_graphics(rdram, ctx);
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
    showing.store(true);
    std::fprintf(stdout, "{\"event\":\"vortex_backdrop\",\"on\":true,\"saved_bytes\":%zu}\n", saved_pool.size());
}

void leave(std::uint8_t* rdram, recomp_context* ctx) {
    wait_for_graphics(rdram, ctx);
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

void set_vortex_backdrop(bool on) { wanted.store(on); }
bool vortex_backdrop_on() { return showing.load(); }

} // namespace blockplanet

// From the top of the game's top-level loop (blockplanet_attract_guard,
// 0x80070180), once a frame on the game's thread.
extern "C" void blockplanet_vortex_frame(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace blockplanet;
    using namespace tetrisphere::guest;
    const bool want = wanted.load();
    if (state == State::off) {
        if (want && load16(rdram, reset_screen) == 0 && at_title_or_menus(rdram)) enter(rdram, ctx);
        return;
    }
    if (!want) {
        leave(rdram, ctx);
        return;
    }
    ++frames_on;
    static const bool log = std::getenv("BLOCKPLANET_VORTEX_LOG") != nullptr;
    if (log && (frames_on <= 40 || frames_on % 30 == 0))
        std::fprintf(stdout, "{\"event\":\"vortex_frame\",\"n\":%d,\"reset\":%d,\"started\":%d,\"t1208\":%u,\"t120C\":%u,\"t1210\":%u,\"scene\":\"%08X\",\"fade\":%d,\"cnt\":%d}\n",
                     frames_on, load16(rdram, reset_screen), load16(rdram, vortex_started), load32(rdram, timer_a), load32(rdram, timer_b),
                     load32(rdram, 0x800E1210u), load32(rdram, 0x800E1264u), load16(rdram, 0x800DFF08u), load16(rdram, 0x800E123Eu));
    if (log) std::fflush(stdout);
    // Past the reset screen's 300-frame wait at once, and its counters kept
    // running (BlockPlanet's menus hold them otherwise: linux_main.cpp).
    if (frames_on == 2) store32(rdram, timer_b, 310);
    // The copied picture (320 x 240, 16-bit) filled dark, so only the vortex shows.
    static const bool keep_picture = std::getenv("BLOCKPLANET_VORTEX_KEEP_PICTURE") != nullptr;  // research
    if (frames_on <= 3 && !keep_picture) {
        const std::uint32_t picture = load32(rdram, fade_picture);
        if ((picture >> 24) == 0x80u)
            {
            // RGBA 5551 with the alpha bit set (zero, transparent, hid the scene too).
            const std::uint32_t pixel = (1u << 11) | (1u << 6) | (4u << 1) | 1u;  // a very dark blue
            for (std::uint32_t at = picture; at < picture + 320u * 240u * 2u; at += 4) store32(rdram, at, pixel << 16 | pixel);
        }
    }
    if (!song_stopped && load16(rdram, vortex_started) != 0) {
        const recomp_context saved = *ctx;
        ctx->r4 = address(jingle_player);
        func_8007ADF8(rdram, ctx);
        *ctx = saved;
        song_stopped = true;
    }
}
