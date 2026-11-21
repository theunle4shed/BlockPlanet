#include "tetrisphere/core_bowl.h"

#include "tetrisphere/guest_memory.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string_view>
#include <tuple>

namespace tetrisphere::core_bowl {
namespace {
using guest::load16;
using guest::load32;
using guest::store32;

// The ball is a picture: four 32 x 32 IA textures on four quads, a 2 x 2
// square facing the camera. Its combine's colour inputs are set to 0 (black)
// and its alpha inputs kept (the round edge, and the game's fades).
constexpr std::uint32_t colour_w0 = (0xFu << 20) | (0x1Fu << 15) | (0xFu << 5) | 0x1Fu;  // a and c, both cycles
constexpr std::uint32_t colour_w1 = (0xFu << 28) | (0xFu << 24) | (7u << 15) | (7u << 6);   // b and d

std::uint32_t physical(std::uint32_t a) { return 0x80000000u | (a & 0x7FFFFFu); }

// The quads after a combine command at `at`: each a texture load (an IA
// texture), a load of four vertices, then two triangles. Returns how many,
// and the distinct corner positions among them.
int count_quads(const std::uint8_t* rdram, std::uint32_t at, std::uint32_t end, std::size_t& corners) {
    std::set<std::tuple<int, int, int>> positions;
    int quads = 0;
    bool texture = false;
    for (std::uint32_t a = at + 8; a < end && a < at + 8u * 80u; a += 8) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4);
        const std::uint8_t op = static_cast<std::uint8_t>(w0 >> 24);
        if (op == 0xFD) {
            if ((w0 >> 16) != 0xFD70u) break;
            texture = true;
            continue;
        }
        if (op == 0x04) {
            if (w0 != 0x04300040u || !texture) break;
            const std::uint32_t v = physical(w1);
            for (std::uint32_t i = 0; i < 4; ++i)
                positions.insert({load16(rdram, v + 16 * i), load16(rdram, v + 16 * i + 2), load16(rdram, v + 16 * i + 4)});
            ++quads;
            texture = false;
            continue;
        }
        // Texture loading, syncs, triangles and mode setup belong to the
        // quads; anything else ends them.
        if (op == 0xBF || op == 0xE6 || op == 0xE7 || op == 0xE8 || op == 0xF5 || op == 0xF3 || op == 0xF2 ||
            op == 0xF0 || op == 0xB9 || op == 0xB6 || op == 0xB7 || op == 0xBA)
            continue;
        break;
    }
    corners = positions.size();
    return quads;
}

} // namespace

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_CORE_BOWL");
        return v != nullptr && std::string_view(v) != "0";
    }();
    return on;
}

bool replace(std::uint8_t* rdram, std::uint32_t start, std::uint32_t end) {
    if (end <= start || end - start > 8u * 40000u) return false;
    for (std::uint32_t a = start; a < end; a += 8) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4);
        if ((w0 >> 24) != 0xFCu) continue;
        std::size_t corners = 0;
        // A 2 x 2 square of quads shares its corners: 9 positions, not 16.
        if (count_quads(rdram, a, end, corners) != 4 || corners > 9) continue;
        store32(rdram, a, w0 | colour_w0);
        store32(rdram, a + 4, w1 | colour_w1);
        static const bool log = std::getenv("BLOCKPLANET_CORE_BOWL_LOG") != nullptr;  // research
        static int logged = 0;
        if (log && logged++ % 300 == 0) {
            std::fprintf(stdout, "{\"event\":\"core_ball_black\",\"at\":\"0x%08X\"}\n", a);
            std::fflush(stdout);
        }
        return true;
    }
    return false;
}

void observe(const std::uint8_t* rdram_const) {
    auto* rdram = const_cast<std::uint8_t*>(rdram_const);
    const std::uint32_t start = load32(rdram, 0x800F22B8u), end = load32(rdram, 0x800F22B4u);
    if ((start & 0xFF000000u) != 0x80000000u || end <= start) return;
    replace(rdram, start, end);
}

} // namespace tetrisphere::core_bowl
