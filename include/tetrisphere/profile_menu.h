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
//   setting:profile_colour=N   the colour variant, on the character and coin screens
//   profile_coin:ID            choose a coin (coin screen)
//   profile_set_coin:ID:COIN   (COIN "none" clears it)
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
// `picker`: the start-up choice of who plays (after the title): choosing the
// playing profile carries on to the main menu, another restarts as it.
std::vector<blockplanet::ui::ListItem> profile_list(const ProfileStore& store, bool guest, bool picker = false);
// Keep playing (for the playing profile) or Play as NAME, then Rename,
// Character, Coin and Delete.
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

// The coins a profile can show beside its name: BlockPlanet's own emblems
// (tools/art/coins.py draws them into assets/ui/coins), tinted with the
// profile's colour variant.
struct Coin {
    const char* id;
    const char* label;
};
const std::vector<Coin>& coins();
// The coin's label, or "" for none or an unknown id.
std::string coin_label(const std::string& id);
// The coin's picture, from the screens folder ("" for none or unknown).
std::string coin_image(const std::string& id);
// A colour variant's tint for coins (#rrggbb; Original keeps the silver).
std::string coin_colour(int palette);
// None, then the coins with their pictures; the chosen one is marked.
std::vector<blockplanet::ui::ListItem> coin_choices(const ProfileStore& store, const std::string& id);

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

// Story's chapter list: the chapters completed and the next one (up to
// `chapters`), focused on the next. `completed` holds Story chapter numbers
// (0, Training Basics, is ignored: Training is on the menus' X button).
std::vector<blockplanet::ui::ListItem> story_chapters(const std::set<int>& completed, int chapters, bool all_open = false);

// A Story chapter's stage list: "Whole episode", then each stage completed
// and the next one (up to `levels`), with its best time as a note
// (best_levels: seconds per stage, 0 = none). Actions: chapter:N (the whole
// episode) and stage:N:L (one stage).
std::vector<blockplanet::ui::ListItem> story_stages(int chapter, const std::set<int>& levels_done,
                                                   const std::vector<double>& best_levels, int levels = 10,
                                                   bool all_open = false);

// Story's Training lessons (X on Story in the menus): Basics and Advanced,
// focused on the first not yet done.
std::vector<blockplanet::ui::ListItem> story_training(bool basics_done, bool advanced_done);
// The puzzles to choose from: each solved one (with its best time and fewest
// moves, when known) and the next unsolved, up to `count`; the next is focused.
// The game's all-capitals text in BlockPlanet's style: short labels (three
// words or fewer) in Title Case ("TRY AGAIN" -> "Try Again"), longer lines in
// sentence case. Abbreviations (VS, CPU, ...) stay in capitals, "&" gets
// spaces ("HIDE&SEEK" -> "Hide & Seek"), text in [brackets] (button names) is
// kept as it is. Text with any lower case is unchanged.
std::string proper_case(const std::string& text);
// The game's % (its apostrophe and quote mark) as typographic quotes, and no
// space before ! or ?.
std::string game_quotes(const std::string& text);
// The original's wording fixed, on the game's capitals (codes kept): "sphere"
// for "ball", "bricks" for "pieces", "tallies", "cannot", "fewer than", single ! and ?.
std::string fix_wording(const std::string& text);

// BlockPlanet's credits, built from the game's credits data (read from the
// player's ROM at run time). Each entry: the game's group (its role) and the
// name as the game writes it (capitals); `line2` marks the second of a
// two-line entry, to be joined to the one before.
struct CreditEntry { int group = 0; std::string name; bool line2 = false; };
// A name in Title Case ("JON SCOTT MCBAIN" -> "Jon Scott McBain"; NOA and
// Roman numerals stay in capitals; [nickname] in quotes).
std::string credit_name(const std::string& name);
// The credits as lines for the credits screen: "#Role" a heading, "" a gap,
// ">text" a closing line, anything else a name. Roles are BlockPlanet's own
// generic titles; each role's names are listed together (the original splits
// long roles over several screens), in the order the roles first appear.
std::vector<std::string> credit_lines(const std::vector<CreditEntry>& entries);
// BlockPlanet's own credits, after the game's: the port, Blocksphere, and the
// projects it is built on (the README's credits), in the same line format.
std::vector<std::string> blockplanet_credit_lines();
std::vector<blockplanet::ui::ListItem> puzzle_list(const std::set<int>& solved, const std::vector<double>& best_times,
                                                   const std::vector<double>& best_moves, int count = 100);

// "Player N" with the lowest N not already taken.
std::string next_profile_name(const ProfileStore& store);
const Profile* profile_by_id(const ProfileStore& store, const std::string& id);

} // namespace tetrisphere
