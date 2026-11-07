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
//   BLOCKPLANET_DRAW_LOG=1            log each distinct 2D draw (helper, image) anywhere
//   BLOCKPLANET_HUD_LOG=1             while BlockPlanet's HUD is shown, log each distinct draw
//                                     made outside the game's HUD routine
//   BLOCKPLANET_HUD_SKIP_LOG=1        log each distinct 2D draw skipped in the game's HUD routine
//   BLOCKPLANET_HUD_SKIP_MODEL=addr   skip model draws (0x8002D93C) whose matrix (a2) is addr
// The HUD: while BlockPlanet's HUD is shown (set_original_hud_hidden), the 2D
// draws made inside the game's HUD routine func_80075CD8 are skipped, and so is
// the held magic's model (func_800C78A0).
// Recompiled calls do not keep return addresses, so draws are told apart by
// their arguments.

#include "tetrisphere/pause_draw.h"
#include "tetrisphere/magic_model.h"
#include "tetrisphere/recomp_support.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace {

std::uint32_t guest32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t v;
    std::memcpy(&v, rdram + (address & 0x00FFFFFFu), 4);
    return v;
}

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
    return helper == 0x8003396Cu || helper == 0x800353F4u || helper == 0x80036FE4u || helper == 0x80071DE8u || helper == 0x80038478u ||
           helper == 0x8007240Cu || helper == 0x800382B4u;
}

} // namespace

namespace {
std::atomic<bool> text_replaced{true};
std::mutex text_mutex;
struct TimedText { blockplanet::GameText text; std::chrono::steady_clock::time_point at; };
std::vector<TimedText> texts;
}

namespace blockplanet {
void set_game_text_replaced(bool replaced) { text_replaced.store(replaced); }
std::vector<GameText> recent_game_text() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(text_mutex);
    std::vector<GameText> out;
    std::vector<TimedText> keep;
    for (const auto& t : texts)
        if (now - t.at < std::chrono::milliseconds(150)) keep.push_back(t), out.push_back(t.text);
    texts = std::move(keep);
    return out;
}
void set_pause_menu_shown(bool shown) { menu_shown.store(shown); }
void set_original_hud_hidden(bool hidden) { hud_hidden.store(hidden); }
}

extern "C" bool blockplanet_draw_filter(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t helper) {
    if (in_hud && is_2d_helper(helper) && hud_hidden.load(std::memory_order_relaxed)) {
        // Research: BLOCKPLANET_HUD_SKIP_LOG=1 logs each distinct 2D draw skipped here.
        static const bool skip_log = std::getenv("BLOCKPLANET_HUD_SKIP_LOG") != nullptr;
        if (skip_log) {
            static std::mutex mutex;
            static std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>> seen;
            std::lock_guard lock(mutex);
            if (seen.insert({helper, std::uint32_t(ctx->r5), std::uint32_t(ctx->r6), std::uint32_t(ctx->r7)}).second)
                std::fprintf(stdout, "{\"event\":\"hud_skip\",\"helper\":\"0x%08X\",\"a1\":\"0x%08X\",\"a2\":%d,\"a3\":%d}\n", helper,
                             std::uint32_t(ctx->r5), int(ctx->r6), int(ctx->r7));
        }
        return true;
    }
    // Research: BLOCKPLANET_MODEL_LOG=1 logs each distinct model drawn
    // (0x8002D93C's model, a0) the first time, with the time, scene and mode;
    // BLOCKPLANET_MODEL_ONLY=addr,addr draws only those models; BLOCKPLANET_MODEL_SKIP skips them.
    if (helper == 0x8002D93Cu) {
        static const bool model_log = std::getenv("BLOCKPLANET_MODEL_LOG") != nullptr;
        static const std::set<std::uint32_t> only = [] {
            std::set<std::uint32_t> list;
            if (const char* v = std::getenv("BLOCKPLANET_MODEL_ONLY")) {
                std::string text(v);
                std::size_t position = 0;
                while (position < text.size()) {
                    const auto comma = std::min(text.find(',', position), text.size());
                    list.insert(static_cast<std::uint32_t>(std::strtoul(text.substr(position, comma - position).c_str(), nullptr, 16)));
                    position = comma + 1;
                }
            }
            return list;
        }();
        const auto model = static_cast<std::uint32_t>(ctx->r4);
        if (model_log) {
            static std::mutex mutex;
            static std::set<std::uint32_t> seen;
            static const auto start = std::chrono::steady_clock::now();
            std::lock_guard lock(mutex);
            if (seen.insert(model).second)
                std::fprintf(stdout, "{\"event\":\"model\",\"model\":\"0x%08X\",\"ms\":%lld,\"scene\":%d,\"mode\":%d,\"mtx\":\"0x%08X\"}\n", model,
                             (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(),
                             static_cast<int>(guest16(rdram, 0x800E121Cu)), static_cast<int>(guest16(rdram, 0x800E44A8u)),
                             static_cast<std::uint32_t>(ctx->r7));
        }
        static const std::set<std::uint32_t> skip = [] {
            std::set<std::uint32_t> list;
            if (const char* v = std::getenv("BLOCKPLANET_MODEL_SKIP")) {
                std::string text(v);
                std::size_t position = 0;
                while (position < text.size()) {
                    const auto comma = std::min(text.find(',', position), text.size());
                    list.insert(static_cast<std::uint32_t>(std::strtoul(text.substr(position, comma - position).c_str(), nullptr, 16)));
                    position = comma + 1;
                }
            }
            return list;
        }();
        if (skip.count(model) != 0 || (!only.empty() && only.count(model) == 0)) {
            ctx->r2 = 0;
            return true;
        }
    }
    // Research: BLOCKPLANET_IMAGE_SKIP=addr,addr skips 2D draws of those images anywhere.
    static const std::set<std::uint32_t> image_skip = [] {
        std::set<std::uint32_t> list;
        if (const char* v = std::getenv("BLOCKPLANET_IMAGE_SKIP")) {
            std::string text(v);
            std::size_t position = 0;
            while (position < text.size()) {
                const auto comma = std::min(text.find(',', position), text.size());
                list.insert(static_cast<std::uint32_t>(std::strtoul(text.substr(position, comma - position).c_str(), nullptr, 16)));
                position = comma + 1;
            }
        }
        return list;
    }();
    if (helper != 0x8002D93Cu && image_skip.count(static_cast<std::uint32_t>(ctx->r5)) != 0) return true;
    if (helper == 0x8003396Cu && blockplanet_title_skip_image(rdram, static_cast<std::uint32_t>(ctx->r5))) return true;
    static const bool draw_log = std::getenv("BLOCKPLANET_DRAW_LOG") != nullptr;  // research: every distinct 2D draw
    if (draw_log) {
        static std::mutex mutex;
        static std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        std::lock_guard lock(mutex);
        if (seen.insert({helper, static_cast<std::uint32_t>(ctx->r5)}).second)
            std::fprintf(stdout, "{\"event\":\"draw\",\"helper\":\"0x%08X\",\"a0\":\"0x%08X\",\"a1\":\"0x%08X\",\"a2\":%d,\"a3\":%d,\"scene\":%d}\n",
                         helper, static_cast<std::uint32_t>(ctx->r4), static_cast<std::uint32_t>(ctx->r5), static_cast<int>(ctx->r6),
                         static_cast<int>(ctx->r7), static_cast<int>(guest16(rdram, 0x800E121Cu)));
    }
    // Research: BLOCKPLANET_TEXT_LOG=1 logs each distinct string drawn by the
    // game's text routine (0x80038478: a1 x, a2 y, a3 string, then four words
    // on the stack), with those words.
    static const bool text_log = std::getenv("BLOCKPLANET_TEXT_LOG") != nullptr;
    if (text_log && helper == 0x800382B4u) {
        static std::mutex mutex;
        static std::set<std::string> seen;
        std::string text;
        std::uint32_t at = 0;
        std::memcpy(&at, rdram + ((static_cast<std::uint32_t>(ctx->r29) + 0x10u) & 0x00FFFFFFu), 4);
        for (std::uint32_t k = 0; k < 96 && at >= 0x80000000u; ++k) {
            const char ch = static_cast<char>(rdram[((at + k) & 0x00FFFFFFu) ^ 3u]);
            if (ch == 0) break;
            text += (ch >= 32 && ch < 127 && ch != '"' && ch != '\\') ? ch : '?';
        }
        std::uint32_t stack[4];
        for (int k = 0; k < 4; ++k)
            std::memcpy(&stack[k], rdram + ((static_cast<std::uint32_t>(ctx->r29) + 0x14u + 4u * std::uint32_t(k)) & 0x00FFFFFFu), 4);
        std::lock_guard lock(mutex);
        std::uint32_t raw0 = 0, raw1 = 0;
        std::memcpy(&raw0, rdram + (at & 0x00FFFFFCu), 4);
        std::memcpy(&raw1, rdram + ((at & 0x00FFFFFCu) + 4u), 4);
        if (seen.insert(text + "@" + std::to_string(int(ctx->r6)) + "," + std::to_string(int(ctx->r7))).second)
            std::fprintf(stdout, "{\"event\":\"text\",\"raw\":\"%08X %08X\",\"ptr\":\"0x%08X\",\"text\":\"%s\",\"font\":%d,\"x\":%d,\"y\":%d,\"args\":[%u,%u,%u,%u],\"scene\":%d,\"mode\":%d}\n",
                         raw0, raw1, at, text.c_str(), int(ctx->r5), int(ctx->r6), int(ctx->r7), stack[0], stack[1], stack[2], stack[3],
                         int(guest16(rdram, 0x800E121Cu)), int(guest16(rdram, 0x800E44A8u)));
    }
    // The game's text, in BlockPlanet's font: kept for the UI and not drawn.
    if (helper == 0x800382B4u && text_replaced.load(std::memory_order_relaxed)) {
        static const bool keep_original = std::getenv("BLOCKPLANET_ORIGINAL_TEXT") != nullptr;
        // Font 0 only for now (menus, Try Again, numbers): the cards' font 1
        // passes its characters another way, still to decode.
        if (!keep_original && (ctx->r5 & 0xFF) == 0) {
            blockplanet::GameText t;
            // func_800382B4(display list, font, x, y; string, r, g, b, a on the stack)
            std::uint32_t st[5];
            for (int k = 0; k < 5; ++k)
                std::memcpy(&st[k], rdram + ((static_cast<std::uint32_t>(ctx->r29) + 0x10u + 4u * std::uint32_t(k)) & 0x00FFFFFFu), 4);
            const std::uint32_t at = st[0];
            for (std::uint32_t k = 0; k < 160 && at >= 0x80000000u; ++k) {
                const char ch = static_cast<char>(rdram[((at + k) & 0x00FFFFFFu) ^ 3u]);
                if (ch == 0) break;
                t.text += ch;
            }
            t.font = int(ctx->r5 & 0xFF);
            t.x = int(ctx->r6), t.y = int(ctx->r7);
            const std::uint32_t c[3] = {st[1], st[2], st[3]};
            t.r = c[0] & 0xFFu, t.g = c[1] & 0xFFu, t.b = c[2] & 0xFFu;
            // Its width in the game's font, as func_800387CC measures it: the sum
            // of each character's s16 width at 0x80102D0C + font * 0x88 + char * 2.
            if (guest32(rdram, 0x80102D48u + 0x88u * std::uint32_t(t.font)) != 0)
                for (unsigned char ch : t.text)
                    t.width += guest16(rdram, 0x80102D0Cu + 0x88u * std::uint32_t(t.font) + 2u * ch);
            if (!t.text.empty()) {
                const auto now = std::chrono::steady_clock::now();
                std::lock_guard lock(text_mutex);
                bool found = false;
                for (auto& e : texts)
                    if (e.text.x == t.x && e.text.y == t.y && e.text.text == t.text) e.at = now, e.text = t, found = true;
                if (!found && texts.size() < 256) texts.push_back({t, now});
            }
            return true;
        }
    }
    if (helper == 0x8002D93Cu) {
        blockplanet_magic_flight(rdram, ctx);
        blockplanet_title_watch(rdram);
    }
    // The held magic's model above the bowl (func_800C78A0's model draw, told
    // apart by its matrices), only while held (player 1's +0x702 >= 0) and
    // only when it cannot be drawn in BlockPlanet's item box (magic_hooks.cpp
    // moves it there), as on portrait screens where the box is outside the
    // picture. Once used, the same model flies
    // over the sphere and is drawn as normal.
    if (helper == 0x8002D93Cu && static_cast<std::uint32_t>(ctx->r6) == 0x80161540u &&
        static_cast<std::uint32_t>(ctx->r7) == 0x801309A8u && hud_hidden.load(std::memory_order_relaxed) &&
        !blockplanet::magic_box_active() &&
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

// Research: objects inside LightWave scenes (func_800321D4's a0 = scene,
// a3 = object index).
//   BLOCKPLANET_OBJECT_LOG=1           log each scene's highest object index as it grows
//   BLOCKPLANET_OBJECT_ONLY=scene:i,j  in that scene draw only objects i, j (others in it skipped)
//   BLOCKPLANET_OBJECT_CYCLE=scene:ms  in that scene draw one object at a time, the next every ms
extern "C" bool blockplanet_scene_object(std::uint8_t* rdram, recomp_context* ctx) {
    const auto scene = static_cast<std::uint32_t>(ctx->r4);
    const int object = static_cast<int>(ctx->r7);
    if (blockplanet_title_object(rdram, scene, object, static_cast<std::uint32_t>(ctx->r6))) return true;
    static const bool log = std::getenv("BLOCKPLANET_OBJECT_LOG") != nullptr;
    if (log) {
        static std::mutex mutex;
        static std::set<std::pair<std::uint32_t, int>> seen;
        std::lock_guard lock(mutex);
        if (seen.insert({scene, object}).second)
            std::fprintf(stdout, "{\"event\":\"scene_object\",\"scene\":\"0x%08X\",\"object\":%d,\"geometry\":%d,\"parts\":\"0x%08X\"}\n", scene,
                         object, static_cast<int>(ctx->r5), static_cast<std::uint32_t>(ctx->r6));
    }
    struct Filter { std::uint32_t scene = 0; std::set<int> only; int cycle_ms = 0; };
    static const Filter filter = [] {
        Filter f;
        if (const char* v = std::getenv("BLOCKPLANET_OBJECT_ONLY")) {
            std::string text(v);
            const auto colon = text.find(':');
            f.scene = static_cast<std::uint32_t>(std::strtoul(text.substr(0, colon).c_str(), nullptr, 16));
            std::size_t position = colon + 1;
            while (colon != std::string::npos && position < text.size()) {
                const auto comma = std::min(text.find(',', position), text.size());
                f.only.insert(std::atoi(text.substr(position, comma - position).c_str()));
                position = comma + 1;
            }
        }
        if (const char* v = std::getenv("BLOCKPLANET_OBJECT_CYCLE")) {
            std::string text(v);
            const auto colon = text.find(':');
            f.scene = static_cast<std::uint32_t>(std::strtoul(text.substr(0, colon).c_str(), nullptr, 16));
            f.cycle_ms = std::atoi(text.c_str() + colon + 1);
        }
        return f;
    }();
    if (filter.scene == 0 || scene != filter.scene) return false;
    if (filter.cycle_ms > 0) {
        static const auto start = std::chrono::steady_clock::now();
        static int shown = -1;
        const int index = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() / filter.cycle_ms) % 64;
        if (index != shown) {
            shown = index;
            std::fprintf(stdout, "{\"event\":\"object_cycle\",\"object\":%d,\"ms\":%lld}\n", index,
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            std::fflush(stdout);
        }
        return object != index;
    }
    return filter.only.count(object) == 0;
}

// Research: BLOCKPLANET_FN_PROBE=1 logs the first call of each probed routine
// each second it runs (time and scene), to see which runs when.
extern "C" void blockplanet_fn_probe(std::uint8_t* rdram, recomp_context*, std::uint32_t fn) {
    static const bool on = std::getenv("BLOCKPLANET_FN_PROBE") != nullptr;
    if (!on) return;
    static std::mutex mutex;
    static std::set<std::pair<std::uint32_t, long long>> seen;
    static const auto start = std::chrono::steady_clock::now();
    const long long second = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
    std::lock_guard lock(mutex);
    if (seen.insert({fn, second}).second)
        std::fprintf(stdout, "{\"event\":\"fn\",\"fn\":\"0x%08X\",\"s\":%lld,\"scene\":%d}\n", fn, second,
                     static_cast<int>(guest16(rdram, 0x800E121Cu)));
}
