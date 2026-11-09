#pragma once

// The menu grid's tile texture (docs/UI.md, Menu grid). The game's grey
// marble texture is read from the player's ROM at run time (main archive
// entry 0x000: 160 x 120, 8-bit intensity) and never stored; without it a
// plain procedural marble is used. Written for BlockPlanet. No RmlUi or RT64
// here, so it can be tested on its own.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace blockplanet::ui {

struct GreyImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;  // one byte per pixel, rows top to bottom
};

// Unpacks one entry of an archive in the game's format (a big-endian u16
// entry count, then count + 1 u32 offsets from the archive's start; each
// entry its pass letters and length, then "SQSH" blocks of pass J). Only
// pass J is supported. Returns nothing for anything out of bounds or not J.
std::optional<std::vector<std::uint8_t>> unpack_archive_entry(std::span<const std::uint8_t> rom,
                                                              std::size_t archive, unsigned entry);

// The marble texture from a big-endian (.z64) ROM, or nothing when it cannot
// be read (no ROM, a different size than expected).
std::optional<GreyImage> marble_from_rom(std::span<const std::uint8_t> rom);

// A plain marble-like texture of the same size, made here.
GreyImage procedural_marble();

// The tiles' texture: `variants` square tiles side by side, each a different
// part of the marble, with a bevel (light top and left edges, dark bottom and
// right). RGBA with full alpha, made to be tinted by a colour of twice the
// tile's base colour: the marble's middle grey is 128, its highlights about
// 1.5 times and its shadows about 0.6 times that.
struct TileAtlas {
    int width = 0, height = 0, tile = 0, variants = 0;
    std::vector<std::uint8_t> rgba;
};
TileAtlas make_tile_atlas(const GreyImage& marble, int tile = 96, int variants = 4);

// The texture for the grid's tiles: from the ROM given to set_grid_rom when
// it can be read, otherwise procedural. Made once, then kept.
void set_grid_rom(std::span<const std::uint8_t> rom);
const TileAtlas& grid_tile_atlas();
bool grid_texture_from_rom();  // after grid_tile_atlas(): whether the ROM's texture was used

} // namespace blockplanet::ui
