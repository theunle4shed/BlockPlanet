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
void set_vortex_backdrop(bool wanted);
// Whether the game is showing the vortex now (or entering it).
bool vortex_backdrop_on();
// The window's width over its height (from the main loop), to fill wide screens.
void set_vortex_aspect(float aspect);

} // namespace blockplanet
