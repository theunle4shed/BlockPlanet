#pragma once

// The contents of the profile screens (docs/UI.md, docs/SAVES.md): the
// profile list, what can be done with one profile, and the delete
// confirmation. Commands, chosen by the player and handled by the game:
//   profile:ID                 show what can be done with profile ID
//   profile_play:ID            restart as profile ID
//   profile_new                add the next "Player N" and restart as it
//   profile_guest              restart as a guest
//   profile_delete:ID          ask to confirm
//   profile_delete_confirmed:ID

#include "tetrisphere/profiles.h"
#include "../../src/ui/ui_actions.h"

#include <string>
#include <vector>

namespace tetrisphere {

// The playing profile first (marked "playing now"), then the others, then
// New profile and Play as guest. `guest` is true when playing as a guest.
std::vector<blockplanet::ui::ListItem> profile_list(const ProfileStore& store, bool guest);
// Keep playing (for the playing profile) or Play as NAME, then Delete.
std::vector<blockplanet::ui::ListItem> profile_actions(const ProfileStore& store, const std::string& id, bool guest);
std::vector<blockplanet::ui::ListItem> delete_confirmation(const ProfileStore& store, const std::string& id);

// "Player N" with the lowest N not already taken.
std::string next_profile_name(const ProfileStore& store);
const Profile* profile_by_id(const ProfileStore& store, const std::string& id);

} // namespace tetrisphere
