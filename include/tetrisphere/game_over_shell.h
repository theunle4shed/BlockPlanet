#pragma once

// The game over shell: black 4 x 4 blocks settling onto the sphere's visible
// side when the last heart is lost (src/graphics/game_over_shell.cpp).

#include <cstdint>

namespace tetrisphere::sphere {

class ExtendedSphere;

// Called with the sphere, after its extended patch's last quarter, every
// frame; draws nothing unless player 1's game is over.
void draw_game_over_shell(std::uint8_t* rdram, const ExtendedSphere& sphere);

// Whether the shell would draw this frame (it is on and player 1's game is
// over), so the original sphere's path builds a sphere for it only then.
bool game_over_shell_wanted(const std::uint8_t* rdram);

// A failure that is not the game's own game over (a lost Minefield): the
// shell closes over the sphere while this is on (BlockPlanet then shows its
// Game Over panel itself).
void set_game_over_shell_forced(bool on);

// The last heart's loss held back while the shell closes (since when).
bool game_over_held();
// Let the held loss run (on the game's next frame): its game over follows.
void release_game_over();
// While held: whether the camera is back at its resting distance.
bool game_over_camera_settled();

} // namespace tetrisphere::sphere
