#pragma once

// Player profiles. Each profile has its own folder for the game's cartridge
// save, trophies and BlockPlanet progress; settings (graphics, controls)
// are shared by the PC. See docs/SAVES.md.
//
//   <data>/profiles.json                 the profile list and the active one
//   <data>/profiles/<id>/cartridge.eep   the game's 512-byte EEPROM save
//   <data>/profiles/<id>/trophies.json
//   <data>/profiles/<id>/backups/        cartridge saves from earlier sessions
//   <data>/deleted/<id>-<name>/          deleted profiles, kept for 30 days
//
// A guest plays without a profile: nothing is saved and no trophies are
// earned (see docs/SAVES.md).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tetrisphere {

struct Profile {
    std::string id;    // folder name: 1, 2, 3 ...
    std::string name;  // shown to the player
    // The character representing the player (empty until chosen; the
    // available ones come with the profile screen) and its colour variant.
    std::string character;
    int palette = 0;
    // The profile's coin (BlockPlanet's own emblems, assets/ui/coins; empty
    // for none), shown beside its name and tinted with the colour variant.
    std::string coin;
};

// A deleted profile that can still be restored.
struct DeletedProfile {
    std::string folder;  // under <data>/deleted
    Profile profile;
};

class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path data_directory);

    // Reads profiles.json, or starts with one profile, "Player 1".
    bool load(std::string& error);
    bool save(std::string& error) const;

    const std::vector<Profile>& profiles() const { return profiles_; }
    const Profile& active() const;
    std::filesystem::path directory(const Profile& profile) const;

    // Makes the profile with this name active, creating it if needed.
    // Names are 1 to 24 printable characters; matching ignores case.
    bool use(std::string_view name, std::string& error);
    // Adds a profile without making it active.
    bool add(std::string_view name, std::string& error);
    // Sets the profile's character (up to 32 of a-z, 0-9, '-'; empty for
    // none) and colour variant (0 to 7).
    bool set_character(std::string_view name, std::string_view character, int palette, std::string& error);
    // Sets the profile's coin (the same form as a character id; empty for none).
    bool set_coin(std::string_view name, std::string_view coin, std::string& error);
    // Renames a profile (its folder keeps its id). A change of case only is
    // allowed; another profile's name is not.
    bool rename(std::string_view name, std::string_view new_name, std::string& error);

    // Moves the profile's folder to <data>/deleted and removes it from the
    // list. If it was active, the first remaining profile becomes active; if
    // none remain, a new "Player 1" is created.
    bool remove(std::string_view name, std::string& error);
    std::vector<DeletedProfile> deleted() const;
    // Brings back the most recently deleted profile with this name. It gets
    // a new id if its old one has been reused.
    bool restore(std::string_view name, std::string& error);
    // Permanently removes deleted profiles older than `days`.
    void purge_deleted(int days);

    const Profile* find(std::string_view name) const;

private:
    std::string next_id() const;

    std::filesystem::path data_directory_;
    std::vector<Profile> profiles_;
    std::string active_id_;
};

bool valid_profile_name(std::string_view name);

// Copies `file` into `backups` as <stem>.1<ext>, moving older copies up and
// keeping `keep` of them. Does nothing if the file does not exist.
bool rotate_backup(const std::filesystem::path& file, const std::filesystem::path& backups, int keep,
                   std::string& error);

// One-time copy of data from where earlier builds kept it (`legacy`, beside
// the executable) into the new data folder and the first profile. Nothing
// is copied if the new folder already has a profile list, or if the two
// folders are the same. Returns the names of the files copied.
std::vector<std::string> migrate_legacy_data(const std::filesystem::path& legacy,
                                             const std::filesystem::path& data_directory);

} // namespace tetrisphere
