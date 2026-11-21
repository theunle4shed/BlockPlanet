#pragma once

// The core's dark ball drawn flat black (the owner's idea, 2026-11-21: a
// black half sphere the camera always looks onto). The ball is a picture,
// four textured quads in a 2 x 2 square facing the camera, drawn while the
// core is open (func_800A18F4 and func_800A1E58 are needed for it); seen
// from the front a black half sphere is a black disc, so the picture is
// drawn black with its own round edge. BLOCKPLANET_CORE_BOWL=1 for now.
#include <cstdint>

namespace tetrisphere::core_bowl {

bool enabled();
// The model helper's commands from `start` to `end` (guest addresses): if they
// hold the ball, its geometry is replaced. Returns whether it did.
bool replace(std::uint8_t* rdram, std::uint32_t start, std::uint32_t end);
// The whole frame's list, just before it is submitted.
void observe(const std::uint8_t* rdram);

} // namespace tetrisphere::core_bowl
