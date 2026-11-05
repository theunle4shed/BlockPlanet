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

} // namespace

namespace blockplanet {
void set_pause_menu_shown(bool shown) { menu_shown.store(shown); }
}

extern "C" bool blockplanet_draw_filter(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t helper) {
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
    if (shown && helper != 0x800353F4u) return true;  // keep only the background
    return s.skip.count(a1) != 0;
}
