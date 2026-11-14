#pragma once

// The menus as they open up with progress (the owner's plan,
// docs/MENU_STRUCTURE.md, 2026-11-14), and each item's development status.
//
// With `everything` set (the main build, the owner's call) every item shows,
// each with its status in brackets. Otherwise only what the player has
// unlocked shows, without statuses:
//   from the start  Main: Solo, Training, Profiles, Options, Credits, Exit;
//                   Solo: Rescue (which launches Episode 1)
//   Episode 1 done  Rescue: Campaign, Score Attack, Casual
//   Episode 3 done  Rescue: Time Attack as well
//   Episode 5 done  Solo: Adventure
// The other modes (Puzzle, Hide & Seek, Lines, Minefield, ...) come through
// Adventure into Bonus, later.

#include "../../src/ui/ui_actions.h"

#include <set>
#include <vector>

namespace tetrisphere {

struct MenuUnlocks {
    bool everything = false;   // the main build: all shown, with statuses
    std::set<int> episodes;    // Rescue's episodes completed (1-based)
};

std::vector<blockplanet::ui::ListItem> main_menu_items(const MenuUnlocks& unlocks);
std::vector<blockplanet::ui::ListItem> solo_menu_items(const MenuUnlocks& unlocks);
std::vector<blockplanet::ui::ListItem> rescue_menu_items(const MenuUnlocks& unlocks);
// Whether Solo > Rescue opens its menu; until Episode 1 is done it launches
// Episode 1 straight away.
bool rescue_menu_open(const MenuUnlocks& unlocks);

} // namespace tetrisphere
