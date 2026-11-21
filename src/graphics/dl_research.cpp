#include "tetrisphere/dl_research.h"

#include "tetrisphere/guest_memory.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace tetrisphere::dl_research {
namespace {
using guest::load32;
using guest::store32;

std::atomic<bool> dump_requested{false};
int dumps = 0;

const char* directory() { return std::getenv("BLOCKPLANET_DL_DUMP"); }

std::vector<std::uint32_t> mute_list() {
    std::vector<std::uint32_t> out;
    if (const char* v = std::getenv("BLOCKPLANET_DL_MUTE")) {
        std::string s(v);
        std::size_t at = 0;
        while (at < s.size()) {
            const std::size_t comma = s.find(',', at);
            out.push_back(static_cast<std::uint32_t>(std::strtoul(s.substr(at, comma - at).c_str(), nullptr, 16)));
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }
    return out;
}

const char* name(std::uint8_t op) {
    switch (op) {
    case 0x00: return "NOOP";
    case 0x01: return "MTX";
    case 0x03: return "MOVEMEM";
    case 0x04: return "VTX";
    case 0x06: return "DL";
    case 0xB1: return "TRI2";
    case 0xB5: return "LINE3D";
    case 0xB6: return "CLEARGEOM";
    case 0xB7: return "SETGEOM";
    case 0xB8: return "ENDDL";
    case 0xB9: return "OTHERMODE_L";
    case 0xBA: return "OTHERMODE_H";
    case 0xBB: return "TEXTURE";
    case 0xBC: return "MOVEWORD";
    case 0xBD: return "POPMTX";
    case 0xBE: return "CULLDL";
    case 0xBF: return "TRI1";
    case 0xE4: return "TEXRECT";
    case 0xE7: return "PIPESYNC";
    case 0xE8: return "TILESYNC";
    case 0xE9: return "FULLSYNC";
    case 0xF0: return "LOADTLUT";
    case 0xF2: return "SETTILESIZE";
    case 0xF3: return "LOADBLOCK";
    case 0xF4: return "LOADTILE";
    case 0xF5: return "SETTILE";
    case 0xF6: return "FILLRECT";
    case 0xF7: return "FILLCOLOR";
    case 0xF8: return "FOGCOLOR";
    case 0xF9: return "BLENDCOLOR";
    case 0xFA: return "PRIMCOLOR";
    case 0xFB: return "ENVCOLOR";
    case 0xFC: return "COMBINE";
    case 0xFD: return "SETTIMG";
    case 0xFE: return "SETZIMG";
    case 0xFF: return "SETCIMG";
    default: return "?";
    }
}

struct Walker {
    std::uint8_t* rdram;
    std::uint32_t segments[16]{};
    std::FILE* out = nullptr;
    const std::vector<std::uint32_t>* mute = nullptr;
    int commands = 0;
    bool blanking = false;

    std::uint32_t resolve(std::uint32_t a) const {
        if ((a & 0xFF000000u) == 0x80000000u) return a;
        return 0x80000000u | ((segments[(a >> 24) & 0xF] + (a & 0x00FFFFFFu)) & 0x7FFFFFu);
    }
    bool muted(std::uint32_t a) const {
        for (std::uint32_t m : *mute)
            if (m == a) return true;
        return false;
    }
    void walk(std::uint32_t address, int depth) {
        if (depth > 12) return;
        for (int guard = 0; guard < 200000 && commands < 400000; ++guard, address += 8, ++commands) {
            const std::uint32_t w0 = load32(rdram, address), w1 = load32(rdram, address + 4);
            const std::uint8_t op = static_cast<std::uint8_t>(w0 >> 24);
            if (out) std::fprintf(out, "%*s%08X %08X %08X %s\n", depth * 2, "", address, w0, w1, name(op));
            if (op == 0xBC && (w0 & 0xFF) == 0x06) segments[((w0 >> 8) & 0xFFFF) / 4 & 0xF] = w1 & 0x7FFFFFu;
            if (op == 0x04) {
                // A muted load and the triangles drawn from it are blanked.
                blanking = muted(resolve(w1));
                if (blanking) store32(rdram, address, 0x00000000u), store32(rdram, address + 4, 0);
            }
            if (blanking && (op == 0xBF || op == 0xB1 || op == 0xB5))
                store32(rdram, address, 0x00000000u), store32(rdram, address + 4, 0);
            if (op == 0x06) {
                const std::uint32_t target = resolve(w1);
                if (muted(target)) {
                    if (((w0 >> 16) & 0xFF) == 1) return;  // a branch: nothing after it runs
                    store32(rdram, address, 0x00000000u), store32(rdram, address + 4, 0);
                    continue;
                }
                walk(target, depth + 1);
                if (((w0 >> 16) & 0xFF) == 1) return;
            }
            if (op == 0xB8) return;
        }
    }
};

} // namespace

bool enabled() { return directory() != nullptr || std::getenv("BLOCKPLANET_DL_MUTE") != nullptr; }
void request_dump() { dump_requested.store(true); }

void observe(const std::uint8_t* rdram_const) {
    static const std::vector<std::uint32_t> mute = mute_list();
    auto* rdram = const_cast<std::uint8_t*>(rdram_const);
    const std::uint32_t start = load32(rdram, 0x800F22B8u);
    if ((start & 0xFF000000u) != 0x80000000u) return;
    Walker w{rdram};
    w.mute = &mute;
    if (dump_requested.exchange(false) && directory()) {
        const std::string path = std::string(directory()) + "/dl-" + std::to_string(dumps++) + ".txt";
        w.out = std::fopen(path.c_str(), "w");
    }
    if (!w.out && mute.empty()) return;
    w.walk(start, 0);
    if (w.out) {
        std::fclose(w.out);
        std::fprintf(stdout, "{\"event\":\"dl_dump\",\"commands\":%d}\n", w.commands);
        std::fflush(stdout);
    }
}

} // namespace tetrisphere::dl_research
