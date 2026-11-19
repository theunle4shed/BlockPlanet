// The sharp bricks' pure parts (include/tetrisphere/brick_textures.h).
#include "tetrisphere/brick_textures.h"

#include <cassert>
#include <cstring>
#include <map>

using namespace tetrisphere::bricks;

int main() {
    // A display list: segment, palette load, sheet load, tile, size, a triangle.
    std::map<std::uint32_t, std::uint32_t> memory;
    std::uint32_t at = 0x1000;
    const auto put = [&](std::uint32_t w0, std::uint32_t w1) { memory[at] = w0, memory[at + 4] = w1, at += 8; };
    put(0xBA000E02u, 0x00008000u);               // other mode: TLUT RGBA16
    put(0xFD100000u, 0x00003000u);               // palette image (16-bit)
    put(0xF5000100u, 0x07000000u);               // load tile at TMEM 0x100 (byte 0x800)
    put(0xF0000000u, 0x0703C000u);               // load 16 colours
    put(0xFD100000u, 0x00002000u);               // sheet image (loaded as 16-bit)
    put(0xF5100000u, 0x07000000u);               // load tile at TMEM 0
    put(0xF3000000u, 0x073FF080u);               // 1,024 16-bit texels, 16 words a line
    put(0xF5402000u, 0x00100000u);               // tile 0: CI4, line 16, palette 1
    put(0xF2000000u, 0x003FC03Cu);               // 256 x 16
    put(0xBF000000u, 0x00000A14u);               // a triangle
    put(0xB8000000u, 0);
    // The palette (bank 0 here, so the draw's bank 1 reads what follows) and
    // a sheet whose first texel is index 1.
    for (std::uint32_t i = 0; i < 16; i += 2) memory[0x3000 + 2 * i] = (0xF801u << 16) | 0x07C1u;  // red, green
    memory[0x2000] = 0x10000000u;
    const Read32 read = [&](std::uint32_t a) { auto it = memory.find(a); return it == memory.end() ? 0u : it->second; };
    int draws = 0;
    Draw seen;
    find_draws(read, 0x1000, [&](const Draw& d) { ++draws, seen = d; });
    assert(draws == 1);
    assert(seen.width == 256 && seen.height == 16 && seen.tlut == 0x8000u && seen.tile.palette == 1);
    // Each colour four times in TMEM, from byte 0x800.
    assert(seen.tmem[0x800] == 0xF8 && seen.tmem[0x801] == 0x01 && seen.tmem[0x806] == 0xF8);
    assert(seen.tmem[0] == 0x10);

    // Scale2x keeps a hard edge and doubles the size.
    std::vector<std::uint8_t> two(2 * 2 * 4, 0);
    two[3] = 255;  // pixel (0, 0) opaque
    const auto big = scale2x(two, 2, 2);
    assert(big.size() == 4 * 4 * 4);
    assert(big[3] == 255 && big[(3 * 4 + 3) * 4 + 3] == 0);  // a lone pixel is rounded off, its far corner stays clear

    // Crafted: 8 times larger, opaque where the sheet is.
    const auto drawn = crafted(seen);
    assert(drawn.size() == std::size_t(256 * 8) * (16 * 8) * 4);
    assert(drawn[3] == 255);  // the first texel's colour (index 1, red) is opaque
    assert(drawn[0] > 150 && drawn[1] < 80);  // and red

    // DDS: 148 bytes of header, then the levels.
    const auto file = dds(std::vector<std::uint8_t>(8 * 4 * 4, 7), 8, 4, 3);
    assert(file.size() == 148 + (8 * 4 + 4 * 2 + 2 * 1) * 4);
    assert(std::memcmp(file.data(), "DDS ", 4) == 0);
    return 0;
}
