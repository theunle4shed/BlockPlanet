// Research: how a stage's sphere is built (docs/research/blockplanet-sphere-generation.md).
//
// Hooks in config/recomp/tetrisphere.us.rev0.toml call these:
//   blockplanet_sphere_seed         - in the new-game routine (func_800CC6E0, before
//                                     0x800CC7C4), where the 16-bit sphere seed has just
//                                     been drawn; logs it and, with
//                                     BLOCKPLANET_SPHERE_SEED=n, replaces it with n.
//   blockplanet_level_build_begin / _end - round the level builder (func_800906E8):
//                                     logs the mode, level, generator state, and at the
//                                     end a hash and the cell-value counts of each layer.
//   blockplanet_rng_draw            - at the random-number routine (func_80081E90):
//                                     with BLOCKPLANET_RNG_LOG=1, logs each draw made
//                                     while a level is being built (range asked, state).
//   blockplanet_stage_record_edit   - with BLOCKPLANET_STAGE_RECORD_EDIT="BYTE=VALUE,...",
//                                     edits the settings record before it is unpacked.
//   BLOCKPLANET_PIECE_SEED=n also replaces the piece seed drawn just before the
//                                     sphere seed (player 1's piece generator), which
//                                     the builder uses too.
// All of it is off unless BLOCKPLANET_SPHERE_RESEARCH=1 (or one of the switches above)
// is set. Nothing here changes the game unless BLOCKPLANET_SPHERE_SEED is set.

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <map>
#include <vector>
#include <string>

namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t rng_state = 0x800E2AA0u;
constexpr std::uint32_t grid_pointer = 0x800E44D0u;  // the grid being built (32 x 32 s16 a layer)
constexpr std::uint32_t mode = 0x800E44A8u;
constexpr std::uint32_t level_counter = 0x800E4498u;
constexpr std::uint32_t settings_record = 0x8013D270u;  // the stage's settings record, as read

bool enabled() {
    static const bool on = std::getenv("BLOCKPLANET_SPHERE_RESEARCH") != nullptr ||
                           std::getenv("BLOCKPLANET_SPHERE_SEED") != nullptr ||
                           std::getenv("BLOCKPLANET_PIECE_SEED") != nullptr ||
                           std::getenv("BLOCKPLANET_SPHERE_LAYOUT") != nullptr ||
                           std::getenv("BLOCKPLANET_STAGE_RECORD_EDIT") != nullptr ||
                           std::getenv("BLOCKPLANET_RNG_LOG") != nullptr;
    return on;
}

bool building = false;
int draws = 0;
std::map<unsigned, int> draws_by_range;  // how many draws asked for each range
// The pieces' kinds handed out during a build (BLOCKPLANET_PIECE_BALANCE).
std::vector<int> build_kinds;
std::chrono::steady_clock::time_point build_started;  // for the build's time
std::chrono::steady_clock::time_point first_draw, last_draw;  // the placing: first to last random draw
// Timing marks through the builder (address, time), in the order passed.
std::vector<std::pair<std::uint32_t, std::chrono::steady_clock::time_point>> build_marks;

// BLOCKPLANET_SPHERE_LAYOUT=file: replaces layers of the built grid by hand.
// The file holds "layer N" lines, each followed by 32 rows of 32 cells, either
// 32 characters ('.' empty, '#' a single block, '*' a special block) or 32
// numbers (raw cell values, see the research note). Layers not in the file
// keep what the game built. Lines starting with ';' are comments.
void apply_layout(std::uint8_t* rdram) {
    const char* path = std::getenv("BLOCKPLANET_SPHERE_LAYOUT");
    if (path == nullptr) return;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stdout, "{\"event\":\"sphere_layout\",\"error\":\"cannot read the file\"}\n");
        return;
    }
    const std::uint32_t grid = load32(rdram, grid_pointer);
    std::string line;
    int layer = -1, row = 0, layers = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == ';') continue;
        if (line.rfind("layer ", 0) == 0) {
            layer = std::atoi(line.c_str() + 6);
            row = 0;
            if (layer < 0 || layer > 8) layer = -1;
            else ++layers;
            continue;
        }
        if (layer < 0 || row >= 32) continue;
        int cells[32];
        int n = 0;
        if (line.find_first_of("0123456789") != std::string::npos) {
            std::istringstream values(line);
            while (n < 32 && values >> cells[n]) ++n;
        } else {
            for (char ch : line) {
                if (n == 32) break;
                if (ch == '.') cells[n++] = 0;
                else if (ch == '#') cells[n++] = 9;
                else if (ch == '*') cells[n++] = 63;
            }
        }
        if (n != 32) continue;
        for (int x = 0; x < 32; ++x)
            store16(rdram, grid + 2u * (static_cast<std::uint32_t>(layer) * 1024u + static_cast<std::uint32_t>(row) * 32u + x),
                    static_cast<std::int16_t>(cells[x]));
        ++row;
    }
    std::fprintf(stdout, "{\"event\":\"sphere_layout\",\"layers\":%d}\n", layers);
}

}  // namespace

// BLOCKPLANET_STAGE_RECORD_EDIT="2=1,0=5": changes bytes of the stage's settings
// record (BYTE=VALUE, decimal or 0x hex) after the game reads it from the ROM and
// before it unpacks it (func_8002A870, before its call to func_80088914).
extern "C" void func_8002AB74(std::uint8_t* rdram, recomp_context* ctx);

// BLOCKPLANET_RECORD_SURVEY=N: the first time a record is loaded, also loads
// records 0 to N-1 with the game's own loader (as func_8002A870 does) and logs
// each, then puts the stage's own record back. Research logs only.
void survey_records(std::uint8_t* rdram, recomp_context* ctx) {
    static bool done = false;
    const char* setting = std::getenv("BLOCKPLANET_RECORD_SURVEY");
    if (done || setting == nullptr) return;
    done = true;
    std::uint8_t saved[20];
    for (std::uint32_t i = 0; i < 20; ++i) saved[i] = load8(rdram, settings_record + i);
    const int count = std::atoi(setting);
    for (int n = 0; n < count; ++n) {
        recomp_context call = *ctx;
        call.r4 = 0x007D4A70u;  // as func_8002A870 passes them: the table, the destination, the number
        call.r5 = static_cast<gpr>(static_cast<std::int32_t>(settings_record));
        call.r6 = static_cast<gpr>(n);
        func_8002AB74(rdram, &call);
        std::string record;
        char hex[4];
        for (std::uint32_t i = 0; i < 20; ++i) {
            std::snprintf(hex, sizeof hex, "%02X", load8(rdram, settings_record + i));
            record += hex;
        }
        std::fprintf(stdout, "{\"event\":\"record_survey\",\"n\":%d,\"record\":\"%s\"}\n", n, record.c_str());
    }
    for (std::uint32_t i = 0; i < 20; ++i) store8(rdram, settings_record + i, saved[i]);
    std::fflush(stdout);
}

extern "C" void blockplanet_stage_record_edit(std::uint8_t* rdram, recomp_context* ctx) {
    survey_records(rdram, ctx);
    const char* edits = std::getenv("BLOCKPLANET_STAGE_RECORD_EDIT");
    if (edits == nullptr) return;
    std::string list(edits), applied;
    std::size_t at = 0;
    while (at < list.size()) {
        const std::size_t end = std::min(list.find(',', at), list.size());
        const std::string item = list.substr(at, end - at);
        const std::size_t eq = item.find('=');
        if (eq != std::string::npos) {
            const unsigned index = static_cast<unsigned>(std::strtoul(item.c_str(), nullptr, 0));
            const unsigned value = static_cast<unsigned>(std::strtoul(item.c_str() + eq + 1, nullptr, 0));
            if (index < 20) {
                store8(rdram, settings_record + index, static_cast<std::uint8_t>(value));
                applied += (applied.empty() ? "" : ",") + std::to_string(index) + "=" + std::to_string(value & 0xFF);
            }
        }
        at = end + 1;
    }
    std::fprintf(stdout, "{\"event\":\"stage_record_edit\",\"applied\":\"%s\"}\n", applied.c_str());
    std::fflush(stdout);
}

extern "C" void blockplanet_sphere_seed(std::uint8_t* rdram, recomp_context* ctx) {
    if (!enabled()) return;
    const unsigned drawn = static_cast<unsigned>(ctx->r2) & 0xFFFFu;
    unsigned used = drawn;
    if (const char* forced = std::getenv("BLOCKPLANET_SPHERE_SEED")) {
        used = static_cast<unsigned>(std::strtoul(forced, nullptr, 0)) & 0xFFFFu;
        ctx->r2 = used;
    }
    // The piece seed, drawn just before (player 1's at 0x801125BC, copied to 0x80115824).
    const unsigned piece_drawn = load32(rdram, 0x801125BCu) & 0xFFFFu;
    unsigned piece_used = piece_drawn;
    if (const char* forced = std::getenv("BLOCKPLANET_PIECE_SEED")) {
        piece_used = static_cast<unsigned>(std::strtoul(forced, nullptr, 0)) & 0xFFFFu;
        store32(rdram, 0x801125BCu, piece_used);
        store32(rdram, 0x80115824u, piece_used);
    }
    std::fprintf(stdout, "{\"event\":\"sphere_seed\",\"drawn\":%u,\"used\":%u,\"piece_drawn\":%u,\"piece_used\":%u}\n",
                 drawn, used, piece_drawn, piece_used);
    std::fflush(stdout);
}

extern "C" void blockplanet_powerup_new_stage(std::uint8_t* rdram);  // magic_hooks.cpp

extern "C" void blockplanet_level_build_begin(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    blockplanet_powerup_new_stage(rdram);  // the power-up order starts again with each stage
    if (!enabled()) return;
    building = true;
    draws = 0;
    draws_by_range.clear();
    build_kinds.clear();
    build_started = std::chrono::steady_clock::now();
    build_marks.clear();
    std::fprintf(stdout, "{\"event\":\"level_build_begin\",\"mode\":%d,\"level\":%d,\"rng\":\"0x%08X\"}\n",
                 load16(rdram, mode), load16(rdram, level_counter), load32(rdram, rng_state));
    std::fflush(stdout);
}

extern "C" void blockplanet_level_build_end(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (!enabled()) return;
    building = false;
    apply_layout(rdram);
    // The settings record as the game read it (private research log only).
    std::string record;
    char hex[4];
    for (std::uint32_t i = 0; i < 20; ++i) {
        std::snprintf(hex, sizeof hex, "%02X", load8(rdram, settings_record + i));
        record += hex;
    }
    const std::uint32_t grid = load32(rdram, grid_pointer);
    std::string layers;
    for (int layer = 0; layer < 10; ++layer) {
        std::uint32_t hash = 2166136261u;
        std::map<int, int> counts;
        for (std::uint32_t c = 0; c < 1024; ++c) {
            const int v = static_cast<std::uint16_t>(load16(rdram, grid + 2u * (layer * 1024u + c)));
            hash = (hash ^ static_cast<std::uint32_t>(v)) * 16777619u;
            ++counts[v];
        }
        std::string values;
        for (const auto& [v, n] : counts) values += (values.empty() ? "" : ",") + std::to_string(v) + ":" + std::to_string(n);
        layers += std::string(layers.empty() ? "" : ",") + "{\"layer\":" + std::to_string(layer) + ",\"hash\":\"" +
                  std::to_string(hash) + "\",\"values\":\"" + values + "\"}";
    }
    // The kinds handed out: how many of each, the longest run of one kind,
    // and the most one kind got ahead of another.
    std::string pieces = "{}";
    if (!build_kinds.empty()) {
        std::map<int, int> by_kind;
        int longest = 0, run = 0, last = -1, lead = 0;
        for (const int k : build_kinds) {
            ++by_kind[k];
            run = k == last ? run + 1 : 1;
            last = k;
            longest = std::max(longest, run);
            int lo = 1 << 30, hi = 0;
            for (const auto& [kind, n] : by_kind) lo = std::min(lo, n), hi = std::max(hi, n);
            if (by_kind.size() > 1) lead = std::max(lead, hi - lo);
        }
        std::string kinds;
        for (const auto& [kind, n] : by_kind) kinds += (kinds.empty() ? "" : ",") + std::string("\"") + std::to_string(kind) + "\":" + std::to_string(n);
        pieces = "{\"count\":" + std::to_string(build_kinds.size()) + ",\"by_kind\":{" + kinds + "},\"longest_run\":" +
                 std::to_string(longest) + ",\"max_lead\":" + std::to_string(lead) + "}";
    }
    // Research: BLOCKPLANET_LAYER_MAP=1 logs each layer as 32 rows of 32
    // characters: '.' empty, '#' a single filler block, '*' special, else
    // 'a' + the block's kind - 32 (a square, b flat bar, c standing bar...).
    if (std::getenv("BLOCKPLANET_LAYER_MAP") != nullptr) {
        for (int layer = 1; layer < 8; ++layer) {
            std::string rows;
            bool any = false;
            for (std::uint32_t c = 0; c < 1024; ++c) {
                const int v = static_cast<std::uint16_t>(load16(rdram, grid + 2u * (layer * 1024u + c)));
                char ch = '.';
                if (v == 9) ch = '#';
                else if (v == 63) ch = '*';
                else if (v != 0) ch = char('a' + std::clamp(v / 9 - 32, 0, 20));
                any = any || v != 0;
                rows += ch;
            }
            if (any) std::fprintf(stdout, "{\"event\":\"layer_map\",\"layer\":%d,\"cells\":\"%s\"}\n", layer, rows.c_str());
        }
    }
    // The time from each mark to the next (the last to the build's end), in ms.
    // Summed per mark (some are inside loops), with how often each was passed.
    std::map<std::uint32_t, std::pair<double, int>> per_mark;
    for (std::size_t i = 0; i < build_marks.size(); ++i) {
        const auto next = i + 1 < build_marks.size() ? build_marks[i + 1].second : std::chrono::steady_clock::now();
        auto& entry = per_mark[build_marks[i].first];
        entry.first += std::chrono::duration<double, std::milli>(next - build_marks[i].second).count();
        ++entry.second;
    }
    std::string marks;
    for (const auto& [at, entry] : per_mark) {
        char item[80];
        std::snprintf(item, sizeof item, "%s\"0x%08X\":[%.2f,%d]", marks.empty() ? "" : ",", at, entry.first, entry.second);
        marks += item;
    }
    if (!build_marks.empty())
        std::fprintf(stdout, "{\"event\":\"build_marks\",\"first_ms\":%.2f,\"marks\":{%s}}\n",
                     std::chrono::duration<double, std::milli>(build_marks.front().second - build_started).count(), marks.c_str());
    std::string callers;
    for (const auto& [range, n] : draws_by_range) {
        char item[32];
        std::snprintf(item, sizeof item, "%s\"%u\":%d", callers.empty() ? "" : ",", range, n);
        callers += item;
    }
    std::fprintf(stdout,
                 "{\"event\":\"level_build_end\",\"ms\":%lld,\"mode\":%d,\"level\":%d,\"draws\":%d,\"rng\":\"0x%08X\","
                 "\"record\":\"%s\",\"ranges\":{%s},\"pieces\":%s,\"build_ms\":%.2f,\"placing_ms\":%.2f,\"layers\":[%s]}\n",
                 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),
                 load16(rdram, mode), load16(rdram, level_counter), draws, load32(rdram, rng_state), record.c_str(),
                 callers.c_str(), pieces.c_str(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - build_started).count(),
                 draws > 0 ? std::chrono::duration<double, std::milli>(last_draw - first_draw).count() : 0.0, layers.c_str());
    std::fflush(stdout);
}

extern "C" void blockplanet_rng_draw(std::uint8_t* rdram, recomp_context* ctx) {
    if (!building) return;
    last_draw = std::chrono::steady_clock::now();
    if (draws == 0) first_draw = last_draw;
    ++draws;
    ++draws_by_range[static_cast<unsigned>(ctx->r4) & 0xFFFFu];
    static const bool log_each = std::getenv("BLOCKPLANET_RNG_LOG") != nullptr;
    if (log_each && draws % 600 == 0) {
        // How full each layer is so far (cells not 0), to see the order layers fill in.
        const std::uint32_t grid = load32(rdram, grid_pointer);
        std::string filled;
        for (int layer = 1; layer < 9; ++layer) {
            int n = 0;
            for (std::uint32_t c = 0; c < 1024; ++c) n += load16(rdram, grid + 2u * (layer * 1024u + c)) != 0;
            filled += (layer > 1 ? "," : "") + std::to_string(n);
        }
        std::fprintf(stdout, "{\"event\":\"build_progress\",\"draws\":%d,\"filled\":[%s]}\n", draws, filled.c_str());
    }
    if (log_each)
        std::fprintf(stdout, "{\"event\":\"rng_draw\",\"n\":%d,\"range\":%u,\"state\":\"0x%08X\"}\n",
                     draws, static_cast<unsigned>(ctx->r4) & 0xFFFFu, load32(rdram, rng_state));
}

// BLOCKPLANET_PLAY_LOG=1: during play, logs player 1's hearts, how full each
// layer of the grid is (cells not 0) and the tallest column whenever any of them
// changes (checked every frame), with the time since play began.
// BLOCKPLANET_KEEP_HEARTS=1 also keeps the hearts at 3 (to keep dropping).
extern "C" void blockplanet_sphere_play_log(std::uint8_t* rdram) {
    static const bool on = std::getenv("BLOCKPLANET_PLAY_LOG") != nullptr;
    static const bool keep = std::getenv("BLOCKPLANET_KEEP_HEARTS") != nullptr;
    if (!on || rdram == nullptr) return;
    static auto start = std::chrono::steady_clock::time_point{};
    if (load16(rdram, 0x800E121Cu) != -1) {  // not in play
        start = {};
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (start == std::chrono::steady_clock::time_point{}) start = now;
    const std::uint32_t grid = load32(rdram, grid_pointer);
    std::string filled;
    int tallest = 0;
    for (int layer = 1; layer < 10; ++layer) {
        int n = 0;
        for (std::uint32_t c = 0; c < 1024; ++c) {
            if (load16(rdram, grid + 2u * (layer * 1024u + c)) != 0) {
                ++n;
                if (layer < 9) tallest = std::max(tallest, layer);
            }
        }
        filled += (layer > 1 ? "," : "") + std::to_string(n);
    }
    const int hearts = load16(rdram, 0x80112F9Eu);
    static std::string last;
    const std::string state = std::to_string(hearts) + "|" + filled;
    if (state != last) {
        last = state;
        std::fprintf(stdout, "{\"event\":\"play_log\",\"ms\":%lld,\"hearts\":%d,\"tallest\":%d,\"filled\":[%s]}\n",
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count()),
                     hearts, tallest, filled.c_str());
        std::fflush(stdout);
    }
    if (keep && hearts >= 0 && hearts < 3) store16(rdram, 0x80112F9Eu, 3);
}

// Research: BLOCKPLANET_PIECE_BALANCE=on|off|bias:P (func_8008A63C, a piece's kind
// from the slot drawn). The game's own rule ("on"): each of the 4 x kinds
// slots hands out the kinds in turn (its counter at the player context's
// +0x2C1E + 2 x slot), which keeps the kinds even. "off": the kind is the
// slot's own, slot % kinds, so each piece is an independent draw. Either way
// the kinds are recorded for the build's summary. Unset: the original runs.
extern "C" bool blockplanet_piece_kind(std::uint8_t* rdram, recomp_context* ctx) {
    static const char* setting = std::getenv("BLOCKPLANET_PIECE_BALANCE");
    if (setting == nullptr) return false;
    const int slot = static_cast<std::int16_t>(ctx->r4);
    const int kinds = std::max<int>(1, load16(rdram, 0x800E44B8u));
    int kind;
    const std::string mode_name(setting);
    if (mode_name == "off") {
        kind = ((slot % kinds) + kinds) % kinds;
    } else if (mode_name.rfind("bias:", 0) == 0) {
        // "bias:P": the first kind P % of the time, the others shared (from
        // the slot drawn, which spreads evenly over 0 to 4 x kinds - 1).
        const int percent = std::clamp(std::atoi(mode_name.c_str() + 5), 0, 100);
        const int slots = 4 * kinds, first = slots * percent / 100;
        kind = slot < first || kinds == 1 ? 0 : 1 + (slot - first) % (kinds - 1);
    } else {
        const std::uint32_t at = load32(rdram, 0x8013DD00u) + 0x2C1Eu + 2u * std::uint32_t(slot);
        const int counter = load16(rdram, at);
        store16(rdram, at, static_cast<std::int16_t>((counter + 1) % kinds));
        kind = counter % kinds;
    }
    if (building) build_kinds.push_back(kind);
    ctx->r2 = static_cast<gpr>(static_cast<std::int16_t>(kind));
    return true;
}

// Research: a timing mark in the builder (BLOCKPLANET_SPHERE_RESEARCH).
extern "C" void blockplanet_build_mark(std::uint8_t*, std::uint32_t at) {
    if (!building) return;
    build_marks.emplace_back(at, std::chrono::steady_clock::now());
}
