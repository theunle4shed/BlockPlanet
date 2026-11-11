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
//   BLOCKPLANET_BG_DL_LOG=N           log the play background's display-list commands on its
//                                     Nth and (N+30)th draws (default 120), with data hashes
// The HUD: while BlockPlanet's HUD is shown (set_original_hud_hidden), the 2D
// draws made inside the game's HUD routine func_80075CD8 are skipped, and so is
// the held magic's model (func_800C78A0).
// Recompiled calls do not keep return addresses, so draws are told apart by
// their arguments.

#include "tetrisphere/pause_draw.h"
#include "tetrisphere/brick_sky.h"
#include "tetrisphere/magic_model.h"
#include "tetrisphere/recomp_support.h"
#include "tetrisphere/intro_cover.h"

#include <algorithm>
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
std::atomic<bool> exit_hidden{false};  // after Resume: the pause background skipped too
std::atomic<bool> hud_hidden{false};
std::atomic<bool> hud_keep_models{false};
// While BlockPlanet's menus are open over the game's own menus: their 2D
// drawing (frame, lines, logo, hints, text) is skipped.
std::atomic<bool> game_menu_hidden{false};
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
std::optional<blockplanet::CardBox> card_box;
std::chrono::steady_clock::time_point card_box_at, card_prompt_at;
int card_prompt = 0;
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
std::optional<CardBox> recent_card_box() {
    std::lock_guard lock(text_mutex);
    const auto now = std::chrono::steady_clock::now();
    if (!card_box || now - card_box_at > std::chrono::milliseconds(150)) return std::nullopt;
    CardBox box = *card_box;
    box.prompt = now - card_prompt_at < std::chrono::milliseconds(150) ? card_prompt : 0;
    return box;
}
void set_pause_menu_shown(bool shown) { menu_shown.store(shown); }
void set_pause_exit_hidden(bool hidden) { exit_hidden.store(hidden); }
void set_original_hud_hidden(bool hidden) { hud_hidden.store(hidden); }
void set_original_hud_keep_models(bool keep) { hud_keep_models.store(keep); }
void set_game_menu_hidden(bool hidden) { game_menu_hidden.store(hidden); }
}

namespace {
// Research: BLOCKPLANET_BG_DL_LOG=N logs the F3D commands the play background
// (the built scene at 0x801033BC, drawn through 0x8002D93C from the main
// loop) writes on its Nth draw and its (N+30)th (default N = 120), following
// G_DL calls, with hashes of the vertices, matrices, palettes and texels they
// point at, so two frames can be compared: mesh motion shows as matrices or
// vertices changing, scrolling as tile sizes, colour cycling as palette
// hashes, tints as prim or env colours (docs/research/blockplanet-play-backgrounds.md).
std::uint32_t resolve(std::uint32_t address) {
    // K0 or physical addresses only; other segments are logged unresolved.
    const std::uint32_t top = address >> 24;
    return top == 0x80u || top == 0x00u ? (address & 0x00FFFFFFu) | 0x80000000u : 0u;
}

std::uint64_t hash_words(const std::uint8_t* rdram, std::uint32_t address, std::uint32_t bytes) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::uint32_t i = 0; i + 4 <= bytes && i < 0x10000u; i += 4) {
        h ^= guest32(rdram, address + i);
        h *= 1099511628211ull;
    }
    return h;
}

void log_background_dl(const std::uint8_t* rdram, std::uint32_t from, std::uint32_t to, int draw) {
    std::fprintf(stdout, "{\"event\":\"bg_dl\",\"draw\":%d,\"from\":\"0x%08X\",\"commands\":%u}\n", draw, from, (to - from) / 8u);
    std::uint32_t image = 0, image_size = 0;
    int lines = 0;
    const auto walk = [&](auto&& self, std::uint32_t start, std::uint32_t end, int depth) -> void {
        for (std::uint32_t a = start; a < end && lines < 6000; a += 8u) {
            const std::uint32_t w0 = guest32(rdram, a), w1 = guest32(rdram, a + 4u);
            const std::uint32_t op = w0 >> 24;
            ++lines;
            std::fprintf(stdout, "BGDL %d %d %08X %08X", draw, depth, w0, w1);
            const std::uint32_t at = resolve(w1);
            if (op == 0x04u && at) {  // G_VTX: length in the low 16 bits
                std::fprintf(stdout, " vtx n=%u hash=%016llX", ((w0 >> 20) & 0xFu) + 1u,
                             (unsigned long long)hash_words(rdram, at, w0 & 0xFFFFu));
            } else if (op == 0x01u && at) {  // G_MTX
                std::fprintf(stdout, " mtx p=%02X hash=%016llX", (w0 >> 16) & 0xFFu,
                             (unsigned long long)hash_words(rdram, at, 64));
            } else if (op == 0xFDu) {  // G_SETTIMG
                image = at;
                image_size = (w0 >> 19) & 3u;
                std::fprintf(stdout, " settimg fmt=%u siz=%u w=%u", (w0 >> 21) & 7u, image_size, (w0 & 0xFFFu) + 1u);
            } else if (op == 0xF0u && image) {  // G_LOADTLUT
                const std::uint32_t count = ((w1 >> 14) & 0x3FFu) + 1u;
                std::fprintf(stdout, " tlut n=%u hash=%016llX", count, (unsigned long long)hash_words(rdram, image, count * 2u));
            } else if (op == 0xF3u && image) {  // G_LOADBLOCK
                const std::uint32_t texels = ((w1 >> 12) & 0xFFFu) + 1u;
                const std::uint32_t bytes = image_size == 0 ? texels / 2u : texels << (image_size - 1u);
                std::fprintf(stdout, " block texels=%u hash=%016llX", texels, (unsigned long long)hash_words(rdram, image, bytes));
            } else if (op == 0xF2u || op == 0xF4u) {  // G_SETTILESIZE, G_LOADTILE
                std::fprintf(stdout, " tilesize uls=%u ult=%u lrs=%u lrt=%u", (w0 >> 12) & 0xFFFu, w0 & 0xFFFu,
                             (w1 >> 12) & 0xFFFu, w1 & 0xFFFu);
            }
            std::fprintf(stdout, "\n");
            if (op == 0x06u && at && depth < 4) {  // G_DL
                self(self, at, at + 8u * 2000u, depth + 1);
                if (((w0 >> 16) & 0xFFu) == 1u) return;  // branch, no return
            }
            if (op == 0xB8u && depth > 0) return;  // G_ENDDL
        }
    };
    walk(walk, from, to, 0);
    std::fflush(stdout);
}
} // namespace

extern "C" bool blockplanet_draw_filter(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t helper) {
    // The N64 logo's held wordmark and short start (intro_logo_hooks.cpp), also
    // before each of its draws: the hook after the scene's step is not reached
    // on its first draws (no frame elapsed), which showed the N alone.
    if (helper == 0x8002D93Cu && guest16(rdram, 0x800E1648u) == 0 && std::uint32_t(ctx->r4) == guest32(rdram, 0x800E166Cu)) {
        blockplanet_logo_scene_before(rdram, ctx, static_cast<std::uint32_t>(ctx->r4));
    }
    // The box art on BlockPlanet's grid (src/graphics/box_art_hooks.cpp).
    if (helper == 0x8002D93Cu && blockplanet_box_art_scene(rdram, ctx)) return true;
    // Portrait windows: the intro zooms to cover (tetrisphere/intro_cover.h).
    // BLOCKPLANET_INTRO_COVER=0 keeps the letterbox.
    static const bool intro_cover = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_COVER");
        return v == nullptr || std::string(v) != "0";
    }();
    if (intro_cover)
        tetrisphere::intro_cover_active().store(guest16(rdram, 0x800E1648u) == 0 && guest16(rdram, 0x800E1646u) == 0,
                                                std::memory_order_relaxed);
    // The brick sky (an experiment, src/graphics/brick_sky.cpp): when on, it
    // is drawn in the play background's place and the game's is skipped.
    if (helper == 0x8002D93Cu && guest16(rdram, 0x800E121Cu) == -1 &&
        static_cast<std::uint32_t>(ctx->r4) == guest32(rdram, 0x801033BCu) && guest32(rdram, 0x801033BCu) != 0 &&
        tetrisphere::brick_sky::draw(rdram)) {
        ctx->r2 = 0;  // "animation finished": no
        return true;
    }
    static const int bg_dl_draw = [] {
        const char* v = std::getenv("BLOCKPLANET_BG_DL_LOG");
        return v == nullptr ? 0 : std::max(1, std::atoi(v) > 0 ? std::atoi(v) : 120);
    }();
    static bool bg_dl_inside = false;
    if (bg_dl_draw > 0 && !bg_dl_inside && helper == 0x8002D93Cu && guest16(rdram, 0x800E121Cu) == -1 &&
        static_cast<std::uint32_t>(ctx->r4) == guest32(rdram, 0x801033BCu) && guest32(rdram, 0x801033BCu) != 0) {
        static int draws = 0;
        ++draws;
        if (draws == bg_dl_draw || draws == bg_dl_draw + 30) {
            // Draw it here, between two reads of the game's next free
            // display-list command, then skip the original call.
            const std::uint32_t from = guest32(rdram, 0x800F22B4u);
            const recomp_context saved = *ctx;
            ctx->r29 = static_cast<gpr>(static_cast<std::int32_t>(saved.r29) - 0x80);
            bg_dl_inside = true;
            func_8002D93C(rdram, ctx);
            bg_dl_inside = false;
            const auto result = ctx->r2;
            *ctx = saved;
            ctx->r2 = result;
            log_background_dl(rdram, from, guest32(rdram, 0x800F22B4u), draws);
            return true;
        }
    }
    // Story hides its score (decided 2026-11-08): the game's score bubbles
    // that rise over breaking bricks (its "1x" to "20x" pictures, 25 to 27
    // by 17) are not drawn there.
    if (helper == 0x8003396Cu && hud_hidden.load(std::memory_order_relaxed) && guest16(rdram, 0x800E121Cu) == -1 &&
        guest16(rdram, 0x800E44A8u) == 1) {
        const auto image = static_cast<std::uint32_t>(ctx->r5);
        if ((image >> 24) == 0x80u) {
            const int w = guest16(rdram, image), h = guest16(rdram, image + 2);
            if (w >= 25 && w <= 27 && h == 17) return true;
        }
    }
    // Story's combo counter (the game's teal number at the top right, drawn
    // as text at about (268, 32)): BlockPlanet's HUD shows it instead.
    if ((helper == 0x800382B4u || helper == 0x80038478u) && static_cast<std::uint32_t>(ctx->r4) == 0x800F22B4u &&
        hud_hidden.load(std::memory_order_relaxed) && guest16(rdram, 0x800E121Cu) == -1 && guest16(rdram, 0x800E44A8u) == 1) {
        const int x = static_cast<int>(helper == 0x800382B4u ? ctx->r6 : ctx->r5);
        const int y = static_cast<int>(helper == 0x800382B4u ? ctx->r7 : ctx->r6);
        if (y >= 24 && y <= 40 && x >= 230 && x <= 300) return true;
    }
    if (game_menu_hidden.load(std::memory_order_relaxed) && is_2d_helper(helper) && guest16(rdram, 0x800E121Cu) >= 0) return true;
    // Training draws its speech (text and bubble) and its pointing arrows in
    // the HUD routine too: they stay when the original HUD is hidden (the
    // speech is then captured and replaced below).
    // The same for the rules and goal cards (Hide & Seek, VS, Lines): their
    // text, box and "more" prompt are drawn in the HUD routine.
    bool training_keep = false;
    const int draw_mode = in_hud ? guest16(rdram, 0x800E44A8u) : 0;
    if (in_hud && (draw_mode == 3 || draw_mode == 5 || draw_mode == 6 || draw_mode == 9)) {
        if (helper == 0x800382B4u) training_keep = true;
        if (helper == 0x8003396Cu && (static_cast<std::uint32_t>(ctx->r5) >> 24) == 0x80u) {
            const std::uint32_t image = static_cast<std::uint32_t>(ctx->r5);
            const int w = guest16(rdram, image), h = guest16(rdram, image + 2);
            const int x = int(ctx->r6), y = int(ctx->r7);
            const bool controller_area = x >= 155 && x <= 215 && y >= 120 && y <= 185;
            if (draw_mode == 3)
                training_keep = (w == 150 && h == 115) || (w == 62 && h == 14) || (w == 50 && h == 14) ||
                                (!controller_area && ((w == 28 && h == 16) || (w == 16 && h == 28)));
            else
                training_keep = (w == 203 && h == 143) || (w == 62 && h == 14);
        }
    }
    if (in_hud && !training_keep && is_2d_helper(helper) && hud_hidden.load(std::memory_order_relaxed)) {
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
        // Losing a life: the original HUD's skull, drawn where the held magic
        // sits (matrix 0x801309A8) while no magic is held (losing a life
        // also loses it). Hidden with the original HUD in Story. But a magic
        // just used is drawn through the same matrix too (its flight and its
        // effect, such as the dynamite's sparks), and the game waits for
        // those animations to finish before the magic goes off: skipping
        // them left the magic hanging forever (the owner's report,
        // 2026-11-11). So from a use (held magic gone with no heart lost)
        // until nothing has been drawn there for half a second, nothing is
        // skipped.
        if (static_cast<std::uint32_t>(ctx->r7) == 0x801309A8u) {
            using Clock = std::chrono::steady_clock;
            static std::int8_t last_held = -1;
            static int last_hearts = -1;
            static bool magic_in_use = false;
            static Clock::time_point use_started{}, last_draw{};
            const auto now = Clock::now();
            const auto held = static_cast<std::int8_t>(rdram[((0x80110220u + 0x702u) & 0x7FFFFFu) ^ 3u]);
            const int hearts = guest16(rdram, 0x80110220u + 0x2D7Eu);
            if (last_held >= 0 && held < 0 && !(last_hearts >= 0 && hearts < last_hearts)) {
                magic_in_use = true;
                use_started = now;
            }
            last_held = held;
            last_hearts = hearts;
            const bool quiet = now - last_draw > std::chrono::milliseconds(500) && now - use_started > std::chrono::milliseconds(500);
            if (magic_in_use && (quiet || now - use_started > std::chrono::seconds(20)))
                magic_in_use = false;
            last_draw = now;
            if (!magic_in_use && hud_hidden.load(std::memory_order_relaxed) && guest16(rdram, 0x800E121Cu) == -1 &&
                guest16(rdram, 0x800E44A8u) == 1 && held < 0)
                return true;
        }
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
    // The intro's backdrop (the N64 logo's white picture of Nintendo and H2O
    // logos, one 320 x 240 bitmap that blurs when enlarged) is not drawn, as
    // the owner chose (2026-11-10); a white fill takes its place, since the
    // game clears the frame with it (without it the logo leaves trails).
    // BLOCKPLANET_INTRO_BACKDROP=1 keeps the original.
    static const bool intro_backdrop = std::getenv("BLOCKPLANET_INTRO_BACKDROP") != nullptr;
    if (helper == 0x8003396Cu && !intro_backdrop && guest16(rdram, 0x800E1648u) == 0 && guest16(rdram, 0x800E1646u) == 0) {
        const std::uint32_t image = static_cast<std::uint32_t>(ctx->r5);
        if ((image >> 24) == 0x80u && guest16(rdram, image) == 320 && guest16(rdram, image + 2) == 240 &&
            guest16(rdram, image + 4) == 3 && int(ctx->r6) == 0 && int(ctx->r7) == 0) {
            const std::uint32_t pointer = static_cast<std::uint32_t>(ctx->r4);  // the game's display list pointer
            std::uint32_t dl = guest32(rdram, pointer);
            const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
                std::memcpy(rdram + (dl & 0x00FFFFFFu), &w0, 4);
                std::memcpy(rdram + ((dl + 4u) & 0x00FFFFFFu), &w1, 4);
                dl += 8u;
            };
            cmd(0xE7000000u, 0);                                   // pipe sync
            cmd(0xBA001402u, 0x00300000u);                         // fill mode
            cmd(0xF7000000u, 0xFFFFFFFFu);                         // fill colour: white
            cmd(0xF6000000u | (319u << 14) | (239u << 2), 0);      // the whole 320 x 240 frame
            cmd(0xE7000000u, 0);
            cmd(0xBA001402u, 0x00000000u);                         // one-cycle again
            std::memcpy(rdram + (pointer & 0x00FFFFFFu), &dl, 4);
            return true;
        }
    }
    // Training's speech bubble and its prompts (in play, mode 3), replaced by
    // BlockPlanet's card panel along with the text. Told apart by size and
    // format, as their addresses depend on what the game has loaded.
    static const bool original_text = std::getenv("BLOCKPLANET_ORIGINAL_TEXT") != nullptr;  // the game's text, bubble and all
    // A rules or goal card's box (Hide & Seek, VS, Lines: a 203 x 143 one-byte
    // picture), replaced by BlockPlanet's card panel along with its text.
    if (helper == 0x8003396Cu && !original_text && text_replaced.load(std::memory_order_relaxed) && guest16(rdram, 0x800E121Cu) == -1) {
        const int mode = guest16(rdram, 0x800E44A8u);
        const std::uint32_t image = static_cast<std::uint32_t>(ctx->r5);
        if ((mode == 5 || mode == 6 || mode == 9) && (image >> 24) == 0x80u && guest16(rdram, image + 4) == 2 &&
            guest16(rdram, image) == 203 && guest16(rdram, image + 2) == 143) {
            std::lock_guard lock(text_mutex);
            card_box = blockplanet::CardBox{int(ctx->r6), int(ctx->r7), 203, 143, 0, 1};
            card_box_at = std::chrono::steady_clock::now();
            return true;
        }
        if ((mode == 5 || mode == 6 || mode == 9) && (image >> 24) == 0x80u && guest16(rdram, image + 4) == 2 &&
            guest16(rdram, image) == 62 && guest16(rdram, image + 2) == 14) {  // "more" under the card
            std::lock_guard lock(text_mutex);
            card_prompt = 1;
            card_prompt_at = std::chrono::steady_clock::now();
            return true;
        }
    }
    if (helper == 0x8003396Cu && !original_text && text_replaced.load(std::memory_order_relaxed) && guest16(rdram, 0x800E121Cu) == -1 &&
        guest16(rdram, 0x800E44A8u) == 3) {
        const std::uint32_t image = static_cast<std::uint32_t>(ctx->r5);
        // The N64 controller picture under the bubble, its lit buttons and the
        // arrows pointing at them (few players hold an N64 pad; the cards name
        // their own buttons): everything small drawn in that area.
        if ((image >> 24) == 0x80u) {
            const int x = int(ctx->r6), y = int(ctx->r7);
            if (x >= 155 && x <= 215 && y >= 120 && y <= 185 && guest16(rdram, image) <= 70 && guest16(rdram, image + 2) <= 60)
                return true;
        }
        if ((image >> 24) == 0x80u && guest16(rdram, image + 4) == 2) {
            const int w = guest16(rdram, image), h = guest16(rdram, image + 2);
            const int prompt = w == 62 && h == 14 ? 1 : w == 50 && h == 14 ? 2 : 0;
            if ((w == 150 && h == 115) || prompt != 0) {
                std::lock_guard lock(text_mutex);
                const auto now = std::chrono::steady_clock::now();
                if (prompt != 0) card_prompt = prompt, card_prompt_at = now;
                else card_box = blockplanet::CardBox{int(ctx->r6), int(ctx->r7), w, h, 0}, card_box_at = now;
                return true;
            }
        }
    }
    static const bool draw_log = std::getenv("BLOCKPLANET_DRAW_LOG") != nullptr;  // research: every distinct 2D draw
    if (draw_log) {
        static std::mutex mutex;
        static std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
        std::lock_guard lock(mutex);
        if (seen.insert({helper, static_cast<std::uint32_t>(ctx->r5)}).second)
        {
            // For the image helper, the image's header: s16 width, height, format.
            const std::uint32_t image = static_cast<std::uint32_t>(ctx->r5);
            const bool has_image = helper == 0x8003396Cu && (image >> 24) == 0x80u;
            std::fprintf(stdout, "{\"event\":\"draw\",\"helper\":\"0x%08X\",\"a0\":\"0x%08X\",\"a1\":\"0x%08X\",\"a2\":%d,\"a3\":%d,\"scene\":%d,\"w\":%d,\"h\":%d,\"format\":%d}\n",
                         helper, static_cast<std::uint32_t>(ctx->r4), image, static_cast<int>(ctx->r6),
                         static_cast<int>(ctx->r7), static_cast<int>(guest16(rdram, 0x800E121Cu)),
                         has_image ? guest16(rdram, image) : 0, has_image ? guest16(rdram, image + 2) : 0,
                         has_image ? guest16(rdram, image + 4) : 0);
        }
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
        for (int k = 0; k < 4; ++k) raw0 = raw0 << 8 | rdram[((at + std::uint32_t(k)) & 0x00FFFFFFu) ^ 3u];
        raw1 = static_cast<std::uint32_t>(ctx->r30);
        if (seen.insert(text + "@" + std::to_string(int(ctx->r6)) + "," + std::to_string(int(ctx->r7))).second)
            std::fprintf(stdout, "{\"event\":\"text\",\"raw\":\"%08X %08X\",\"ptr\":\"0x%08X\",\"text\":\"%s\",\"font\":%d,\"x\":%d,\"y\":%d,\"args\":[%u,%u,%u,%u],\"scene\":%d,\"mode\":%d}\n",
                         raw0, raw1, at, text.c_str(), int(ctx->r5), int(ctx->r6), int(ctx->r7), stack[0], stack[1], stack[2], stack[3],
                         int(guest16(rdram, 0x800E121Cu)), int(guest16(rdram, 0x800E44A8u)));
    }
    // The game's text, in BlockPlanet's font: kept for the UI and not drawn.
    if (helper == 0x800382B4u && text_replaced.load(std::memory_order_relaxed)) {
        static const bool keep_original = std::getenv("BLOCKPLANET_ORIGINAL_TEXT") != nullptr;
        // Fonts 0 (menus, Try Again, numbers) and 1 (the cards: their text
        // types out word by word, each word fading in by its alpha).
        if (!keep_original && (ctx->r5 & 0xFF) <= 1) {
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
            t.r = c[0] & 0xFFu, t.g = c[1] & 0xFFu, t.b = c[2] & 0xFFu, t.a = st[4] & 0xFFu;
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
                    // One text per place: a newer string there replaces the
                    // older (a card word typing in, "pu" then "puz").
                    if (e.text.x == t.x && e.text.y == t.y && e.text.font == t.font) e.at = now, e.text = t, found = true;
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
    if (shown && !keep_all && exit_hidden.load(std::memory_order_relaxed)) return true;  // after Resume: that too
    return s.skip.count(a1) != 0;
}

extern "C" void blockplanet_hud_begin(std::uint8_t*, recomp_context*) { in_hud = true; }
extern "C" void blockplanet_hud_end(std::uint8_t*, recomp_context*) { in_hud = false; }

extern "C" bool blockplanet_skip_queue_model(std::uint8_t*, recomp_context*) {
    // With models kept (the GAME OVER scene), only the HUD routine's own
    // models (the next pieces, the bowl) stay hidden.
    return hud_hidden.load(std::memory_order_relaxed) && (!hud_keep_models.load(std::memory_order_relaxed) || in_hud);
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
    blockplanet_logo_object_draw(rdram, scene, object);  // the N64 logo's wordmark, straight on
    // While BlockPlanet's menus are open over the game's own menus, its 3D
    // menu scenes (the ring with its character, the MENU sign) are not drawn:
    // only the grid background stays (the owner's choice, 2026-11-08).
    if (game_menu_hidden.load(std::memory_order_relaxed) && guest16(rdram, 0x800E121Cu) >= 0) return true;
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
