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
// Kinds 8 and 9: one of the barrage's missiles, and its launcher once empty.
constexpr int barrage_missile_kind = 8;
constexpr int empty_launcher_kind = 9;
// The launcher's missiles sit this far from its column's axis (model units),
// at 0, 120 and 240 degrees, 300 back along z (barrage_scene.cpp starts them there).
constexpr float launcher_missile_radius = 820.0f;
constexpr int last_kind = 9;
// The display list of BlockPlanet's model of `kind` at `level` (built into
// model memory when first asked for), or 0.
std::uint32_t model_list(std::uint8_t* rdram, int kind, int level, std::uint32_t light_dir = 0x5A33C500u);
bool replace(std::uint8_t* rdram, std::uint32_t start, int kind, int level, std::uint32_t only_source = 0, std::uint32_t* source = nullptr,
             bool whole = false, int draw_kind = -1, double turn_degrees = 0.0);  // draw_kind: the model drawn, if not kind's

// Whether the power-up drawn from `start` flies nose backwards (our models'
// front is -z), from its movement since the last call; `restart` for a new
// flight.
bool nose_backwards(const std::uint8_t* rdram, std::uint32_t start, bool restart);
// How far the power-up's nose (-z) points at the camera, -1 to 1 (eye space
// z of its direction); -2 if unknown.
double nose_towards_camera(const std::uint8_t* rdram, std::uint32_t start);
// The game's geometry in the draw from `start` blanked (nothing drawn).
bool hide(std::uint8_t* rdram, std::uint32_t start);

}  // namespace tetrisphere::powerup_models
