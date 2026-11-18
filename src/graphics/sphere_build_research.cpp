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
#include <map>
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
                           std::getenv("BLOCKPLANET_RNG_LOG") != nullptr;
    return on;
}

bool building = false;
int draws = 0;
std::map<unsigned, int> draws_by_range;  // how many draws asked for each range

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

extern "C" void blockplanet_level_build_begin(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (!enabled()) return;
    building = true;
    draws = 0;
    draws_by_range.clear();
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
    std::string callers;
    for (const auto& [range, n] : draws_by_range) {
        char item[32];
        std::snprintf(item, sizeof item, "%s\"%u\":%d", callers.empty() ? "" : ",", range, n);
        callers += item;
    }
    std::fprintf(stdout,
                 "{\"event\":\"level_build_end\",\"mode\":%d,\"level\":%d,\"draws\":%d,\"rng\":\"0x%08X\","
                 "\"record\":\"%s\",\"ranges\":{%s},\"layers\":[%s]}\n",
                 load16(rdram, mode), load16(rdram, level_counter), draws, load32(rdram, rng_state), record.c_str(),
                 callers.c_str(), layers.c_str());
    std::fflush(stdout);
}

extern "C" void blockplanet_rng_draw(std::uint8_t* rdram, recomp_context* ctx) {
    if (!building) return;
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
