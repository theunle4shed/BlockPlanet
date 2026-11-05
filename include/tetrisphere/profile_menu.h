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
//   profile_rename:ID          edit the name (name screen)
//   profile_character:ID       choose a character (character screen)
//   profile_set_character:ID:CHARACTER   (CHARACTER "none" clears it)
//   setting:profile_colour=N   the colour variant, on the character screen
// The name screen's keys report name_key:KEY (a character, or space,
// delete, shift), typing on a keyboard reports name_type:TEXT, and Done
// reports name_done.

#include "tetrisphere/profiles.h"
#include "../../src/ui/ui_actions.h"

#include <set>
#include <string>
#include <vector>

namespace tetrisphere {

// The playing profile first (marked "playing now"), then the others, then
// New profile and Play as guest. `guest` is true when playing as a guest.
std::vector<blockplanet::ui::ListItem> profile_list(const ProfileStore& store, bool guest);
// Keep playing (for the playing profile) or Play as NAME, then Rename,
// Character and Delete.
std::vector<blockplanet::ui::ListItem> profile_actions(const ProfileStore& store, const std::string& id, bool guest);
// Delete NAME, then Keep it (selected).
std::vector<blockplanet::ui::ListItem> delete_confirmation(const ProfileStore& store, const std::string& id);

// The characters a profile can choose: the robots rescued in Story (named
// in the ROM next to the credits). Unlocking them as they are rescued needs
// the chapter each one is rescued in (not researched yet).
struct Character {
    const char* id;
    const char* label;
};
const std::vector<Character>& characters();
// The character's label, or "" for none or an unknown id.
std::string character_label(const std::string& id);
// Colour variant names, 0 to 7 (0 is the character's own colours).
const std::vector<std::string>& colour_names();
// None, then the characters; the chosen one is marked.
std::vector<blockplanet::ui::ListItem> character_choices(const ProfileStore& store, const std::string& id);

// The name being typed on the name screen, with an on-screen keyboard for
// controllers: QWERTY with the numbers on top and a few symbols, then
// Shift, Space, Delete and Done. (Other layouts can come with localisation.)
class NameEditor {
public:
    void start(std::string target, std::string text);
    const std::string& target() const { return target_; }  // a profile id, or "new"
    const std::string& text() const { return text_; }
    // A key from the on-screen keyboard (name_key:KEY). Returns true if
    // the keyboard's labels changed (shift).
    bool press(const std::string& key);
    // Text typed on a keyboard; characters that names cannot hold are skipped.
    void type(const std::string& text);
    std::vector<blockplanet::ui::ListItem> keyboard() const;

private:
    std::string target_;
    std::string text_;
    bool shift_ = false;
};
constexpr int keyboard_columns = 10;

// Story's chapter list: Chapter 0 (Training Basics), then the chapters
// completed and the next one (up to `chapters`). The focus starts on
// Chapter 0 for a player who has done nothing yet, and otherwise on the
// next chapter. `completed` holds Story chapter numbers (0 is Training).
std::vector<blockplanet::ui::ListItem> story_chapters(const std::set<int>& completed, int chapters);

// Puzzle's list: Chapter 0 (Training Puzzle), then the puzzles. A new
// player starts on Chapter 0; once it is done, on the puzzles.
std::vector<blockplanet::ui::ListItem> puzzle_chapters(bool training_done);

// "Player N" with the lowest N not already taken.
std::string next_profile_name(const ProfileStore& store);
const Profile* profile_by_id(const ProfileStore& store, const std::string& id);

} // namespace tetrisphere
