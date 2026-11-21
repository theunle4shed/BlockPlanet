#pragma once

#include <cstdint>

// BlockPlanet's own 3D power-up models (src/graphics/powerup_models.cpp).
namespace tetrisphere::powerup_models {

// After the game's model helper has drawn a power-up (its commands written
// from `start` in the frame's display list): its geometry replaced by
// BlockPlanet's model of `kind` (0 Firecracker to 5 Ray Gun) at `level`
// (1 to 3). False if nothing was replaced (no model of that kind yet).
// With `only_source` set, only a draw whose first vertices come from there (the
// held model's, as `source` gave back) is replaced: so after use the flying
// model is told from other models drawn the same way (the core's character).
// `source`, if given, receives the draw's first vertex source.
bool replace(std::uint8_t* rdram, std::uint32_t start, int kind, int level, std::uint32_t only_source = 0, std::uint32_t* source = nullptr);

}  // namespace tetrisphere::powerup_models
