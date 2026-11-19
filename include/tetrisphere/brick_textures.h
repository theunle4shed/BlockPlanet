#pragma once

// Sharp bricks (the owner's request, 2026-11-19): the sphere's brick textures
// are tiny (16 x 16 texels a tile, 16 tiles on a 256 x 16 sheet with a
// 16-colour palette), so close views blur them. BlockPlanet makes larger
// copies from the player's own game as it draws them, and RT64 draws those in
// their place (a replacement pack in the data folder, never in the
// repository). See docs/research/blockplanet-sharp-bricks.md.
//
// This part has no RT64 dependency: it finds the brick draws in a display
// list (keeping a copy of the texture memory, TMEM, as the commands fill it),
// decodes them, scales them up and writes the replacement file. Hashing and
// handing the file to RT64 are in rt64_context.cpp.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tetrisphere::bricks {

struct Tile {
    std::uint8_t fmt = 0, siz = 0, palette = 0;
    std::uint16_t line = 0, tmem = 0;
};

// One draw with a brick sheet: the tile, its size, the palette mode (the
// other mode's TLUT bits, 0x8000 for RGBA16) and TMEM as it was.
struct Draw {
    Tile tile;
    std::uint16_t width = 0, height = 0;
    std::uint32_t tlut = 0;
    std::array<std::uint8_t, 4096> tmem{};
};

// Reads a big-endian 32-bit word of guest memory at a physical address.
using Read32 = std::function<std::uint32_t(std::uint32_t)>;

// Walks the display list at `list` (physical address), following calls and
// segments, and reports each triangle drawn with a brick sheet's tile (colour
// index, 4-bit, 256 x 16). At most `budget` commands are read.
void find_draws(const Read32& read32, std::uint32_t list, const std::function<void(const Draw&)>& found,
                int budget = 400000);

// The draw's texels as RGBA8 (width x height x 4 bytes).
std::vector<std::uint8_t> decode(const Draw& draw);

// Scale2x (EPX): twice the size, edges kept sharp, diagonals smoothed.
std::vector<std::uint8_t> scale2x(const std::vector<std::uint8_t>& rgba, int width, int height);

// A DDS file (RGBA8, DX10 header) with `levels` mipmaps, each half the last.
std::vector<std::uint8_t> dds(const std::vector<std::uint8_t>& rgba, int width, int height, int levels);

// The brick styles (Graphics > Bricks).
enum class Style { Sharp, Crafted };

// Crafted: each piece tile (three colours or fewer: the brick, the
// original's inner ring and the outline) redrawn at 8 times the size, square:
// a deeper outline, and inside it a bevel, light on the top and left and dark
// on the bottom and right, meeting on the diagonal at the corners (the owner,
// 2026-11-19: square bevelled corners, not rounded), with a gentle light from
// above. Patterned tiles keep Sharp's look. RGBA8, 8 times larger.
std::vector<std::uint8_t> crafted(const Draw& draw);

// The replacement for a draw in a style: 8 times larger, mipmaps down to the
// original size, as a DDS file.
std::vector<std::uint8_t> replacement(const Draw& draw, Style style = Style::Sharp);

}  // namespace tetrisphere::bricks
