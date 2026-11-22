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
// Kind 6: one Dynamite stick, as each flies once the bundle splits.
constexpr int stick_kind = 6;
// Kind 7: the Missile turned round, for the Bomb's dive into the sphere (its
// own model leads with the other end, 2026-11-22).
constexpr int dive_kind = 7;
bool replace(std::uint8_t* rdram, std::uint32_t start, int kind, int level, std::uint32_t only_source = 0, std::uint32_t* source = nullptr,
             bool whole = false, int draw_kind = -1);  // draw_kind: the model drawn, if not kind's

// Whether the power-up drawn from `start` flies nose backwards (our models'
// front is -z), from its movement since the last call; `restart` for a new
// flight.
bool nose_backwards(const std::uint8_t* rdram, std::uint32_t start, bool restart);
// The game's geometry in the draw from `start` blanked (nothing drawn).
bool hide(std::uint8_t* rdram, std::uint32_t start);

}  // namespace tetrisphere::powerup_models
