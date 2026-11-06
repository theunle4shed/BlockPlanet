// Draw filter for the game's pause screen (docs/research/blockplanet-game-state.md).
//
// Hooks at the start of the game's shared drawing helpers call
// blockplanet_draw_filter. While the game's pause is open (pause row
// 0x800E1228 >= 0 during play) and BlockPlanet's pause menu is showing over
// it (set_pause_menu_shown), every draw except the pause background is
// skipped: the white line, the menu text, the button hints (image helper
// 0x8003396C and text helper 0x80038478) and the instructor model (three
// parts through 0x8002D93C). The background is the one draw through
// 0x800353F4. Found by logging the draws made while paused.
//
// Research switches:
//   BLOCKPLANET_PAUSE_LOG=1           log each distinct helper and first argument (a0, a1)
//                                     seen while paused
//   BLOCKPLANET_PAUSE_SKIP=a1,a1..    skip draws whose second argument (the image, for
//                                     the image helpers) is one of these while paused
//   BLOCKPLANET_HUD_LOG=1             while BlockPlanet's HUD is shown, log each distinct draw
//                                     made outside the game's HUD routine
//   BLOCKPLANET_HUD_SKIP_MODEL=addr   skip model draws (0x8002D93C) whose matrix (a2) is addr
// The HUD: while BlockPlanet's HUD is shown (set_original_hud_hidden), the 2D
// draws made inside the game's HUD routine func_80075CD8 are skipped, and so is
// the held magic's model (func_800C78A0).
// Recompiled calls do not keep return addresses, so draws are told apart by
// their arguments.

#include "tetrisphere/pause_draw.h"
#include "tetrisphere/recomp_support.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace {

std::int16_t guest16(const std::uint8_t* rdram, std::uint32_t address) {
    std::int16_t value = 0;
    const std::uint32_t offset = (address & 0x00FFFFFFu) ^ 2u;
    value = static_cast<std::int16_t>(rdram[offset] | (rdram[offset + 1] << 8));
    return value;
}

struct Settings {
    bool log = false;
    std::set<std::uint32_t> skip;
};

const Settings& settings() {
    static const Settings value = [] {
        Settings s;
        const char* log = std::getenv("BLOCKPLANET_PAUSE_LOG");
        s.log = log != nullptr && std::string(log) == "1";
        if (const char* skip = std::getenv("BLOCKPLANET_PAUSE_SKIP")) {
            std::string text(skip);
            std::size_t position = 0;
            while (position < text.size()) {
                const auto comma = std::min(text.find(',', position), text.size());
                const auto address = std::strtoul(text.substr(position, comma - position).c_str(), nullptr, 16);
                if (address != 0) s.skip.insert(static_cast<std::uint32_t>(address));
                position = comma + 1;
            }
        }
        return s;
    }();
    return value;
}

std::atomic<bool> menu_shown{false};
std::atomic<bool> hud_hidden{false};
// Set while func_80075CD8 runs (game thread only).
bool in_hud = false;

bool is_2d_helper(std::uint32_t helper) {
    return helper == 0x8003396Cu || helper == 0x800353F4u || helper == 0x80036FE4u || helper == 0x80071DE8u || helper == 0x80038478u || helper == 0x8007240Cu;
}

} // namespace

namespace blockplanet {
void set_pause_menu_shown(bool shown) { menu_shown.store(shown); }
void set_original_hud_hidden(bool hidden) { hud_hidden.store(hidden); }
}

extern "C" bool blockplanet_draw_filter(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t helper) {
    if (in_hud && is_2d_helper(helper) && hud_hidden.load(std::memory_order_relaxed)) return true;
    // The held magic's model above the bowl (func_800C78A0's model draw, told
    // apart by its matrices), only while held (player 1's +0x702 >= 0);
    // BlockPlanet's item box shows it instead. Once used, the same model flies
    // over the sphere and is drawn as normal.
    if (helper == 0x8002D93Cu && static_cast<std::uint32_t>(ctx->r6) == 0x80161540u &&
        static_cast<std::uint32_t>(ctx->r7) == 0x801309A8u && hud_hidden.load(std::memory_order_relaxed) &&
        static_cast<std::int8_t>(rdram[(0x80110922u & 0x00FFFFFFu) ^ 3u]) >= 0) {  // a magic is held, not in use
        ctx->r2 = 0;  // "animation finished": no
        return true;
    }
    static const std::uint32_t skip_model = [] {  // research
        const char* v = std::getenv("BLOCKPLANET_HUD_SKIP_MODEL");
        return v ? static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16)) : 0u;
    }();
    if (skip_model != 0 && helper == 0x8002D93Cu && static_cast<std::uint32_t>(ctx->r6) == skip_model) {
        ctx->r2 = 0;
        return true;
    }
    static const bool hud_log = std::getenv("BLOCKPLANET_HUD_LOG") != nullptr;  // research: draws outside the HUD routine
    if (hud_log && !in_hud && hud_hidden.load(std::memory_order_relaxed)) {
        static std::mutex mutex;
        static std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        std::lock_guard lock(mutex);
        if (seen.insert({helper, static_cast<std::uint32_t>(ctx->r5)}).second)
            std::fprintf(stdout, "{\"event\":\"hud_draw\",\"helper\":\"0x%08X\",\"a1\":\"0x%08X\",\"a2\":%d,\"a3\":%d}\n",
                         helper, static_cast<std::uint32_t>(ctx->r5), static_cast<int>(ctx->r6), static_cast<int>(ctx->r7));
    }
    const auto& s = settings();
    const bool shown = menu_shown.load(std::memory_order_relaxed);
    if (!shown && !s.log && s.skip.empty()) return false;
    const bool paused = guest16(rdram, 0x800E121Cu) == -1 && guest16(rdram, 0x800E1228u) >= 0;
    if (!paused) return false;
    const auto a0 = static_cast<std::uint32_t>(ctx->r4);
    const auto a1 = static_cast<std::uint32_t>(ctx->r5);
    if (s.log) {
        static std::mutex mutex;
        static std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        std::lock_guard lock(mutex);
        if (seen.insert({helper, a1}).second) {
            std::fprintf(stdout,
                         "{\"event\":\"pause_draw\",\"helper\":\"0x%08X\",\"a0\":\"0x%08X\",\"a1\":\"0x%08X\","
                         "\"a2\":%d,\"a3\":%d}\n",
                         helper, a0, a1, static_cast<int>(ctx->r6), static_cast<int>(ctx->r7));
            std::fflush(stdout);
        }
    }
    static const bool keep_all = std::getenv("BLOCKPLANET_PAUSE_KEEP_ALL") != nullptr;  // research
    if (shown && !keep_all && helper != 0x800353F4u) return true;  // keep only the background
    return s.skip.count(a1) != 0;
}

extern "C" void blockplanet_hud_begin(std::uint8_t*, recomp_context*) { in_hud = true; }
extern "C" void blockplanet_hud_end(std::uint8_t*, recomp_context*) { in_hud = false; }

extern "C" bool blockplanet_skip_queue_model(std::uint8_t*, recomp_context*) {
    return hud_hidden.load(std::memory_order_relaxed);
}
