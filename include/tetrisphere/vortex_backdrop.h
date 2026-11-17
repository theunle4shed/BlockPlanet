#pragma once

// The jukebox's Vortex background (docs/research/jukebox.md, VORTEX): the
// game's own reset-screen vortex scene (its robots sucked into a glowing
// ring), played behind the jukebox while it is chosen, with the jukebox's
// music instead of the vortex's song. Entering saves what the game's reset
// screen overwrites (the model memory above the common assets and a few of
// its values) and leaving puts it back, so the title or menu behind carries on.

namespace blockplanet {

// Asked for by the jukebox (any thread); applied on the game's thread at the
// top of its next frame, and only while the game shows its title or menus.
// `stage` 1 to 25: one of the game's play backgrounds instead of the vortex
// (drawn by the same reset screen); 0: the vortex.
void set_vortex_backdrop(bool wanted, int stage = 0);
// Whether the game is showing the vortex now (or entering it).
bool vortex_backdrop_on();
// Whether the chosen scene is on screen (until then the jukebox covers the
// screen, so the game's menu does not show while it is being set up).
bool vortex_backdrop_ready();
// The window's width over its height (from the main loop), to fill wide screens.
void set_vortex_aspect(float aspect);

// Step 4 of the direct launch (docs/ROADMAP.md): while BlockPlanet's menus are
// open over the game's own menu, the game is parked in the same reset screen,
// drawing nothing (unless the jukebox asks for a background), so the game's
// menu code does not run behind BlockPlanet's. Asked for by the main loop each
// frame; left as soon as it is not (a start, a save switch, a title restart).
// BLOCKPLANET_GAME_IDLE=0 turns it off.
void set_game_idle(bool idle);
bool game_idle();  // parked with no background asked for
// BlockPlanet's own pause (PauseBridge::set_parker): the game parked in play,
// play frozen where it is; and whether it is parked now.
void set_pause_park(bool park);
bool pause_parked();
// The same behind BlockPlanet's end screens (EndMenuBridge::set_parker).
void set_end_park(bool park);
bool end_parked();

} // namespace blockplanet
