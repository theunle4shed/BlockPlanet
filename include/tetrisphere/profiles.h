#pragma once

// Player profiles. Each profile has its own folder for the game's cartridge
// save, trophies and BlockPlanet progress; settings (graphics, controls)
// are shared by the PC. See docs/SAVES.md.
//
//   <data>/profiles.json                 the profile list and the active one
//   <data>/profiles/<id>/cartridge.eep   the game's 512-byte EEPROM save
//   <data>/profiles/<id>/trophies.json
//   <data>/profiles/<id>/backups/        cartridge saves from earlier sessions

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tetrisphere {

struct Profile {
    std::string id;    // folder name: 1, 2, 3 ...
    std::string name;  // shown to the player
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

private:
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
