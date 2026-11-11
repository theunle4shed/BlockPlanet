#include "tetrisphere/profiles.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>

namespace tetrisphere {
namespace {

// 2 added character and palette. The optional "coin" (2026-11-11) needs no
// new version: earlier builds ignore it.
constexpr int schema_version = 2;
constexpr const char* list_file = "profiles.json";

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool write_atomically(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out.flush()) {
            error = "could not write " + temporary;
            return false;
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code) {
        error = "could not replace " + path.string() + ": " + code.message();
        return false;
    }
    return true;
}

} // namespace

bool valid_profile_name(std::string_view name) {
    if (name.empty() || name.size() > 24) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u >= 0x20 && u < 0x7F;
    });
}

ProfileStore::ProfileStore(std::filesystem::path data_directory) : data_directory_(std::move(data_directory)) {}

bool ProfileStore::load(std::string& error) {
    profiles_.clear();
    active_id_.clear();
    const auto path = data_directory_ / list_file;
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        profiles_.push_back({"1", "Player 1"});
        active_id_ = "1";
        return save(error);
    }
    try {
        std::ifstream in(path, std::ios::binary);
        const auto document = nlohmann::json::parse(in);
        if (document.at("schema_version").get<int>() > schema_version) {
            error = "profiles.json is from a newer version";
            return false;
        }
        for (const auto& entry : document.at("profiles")) {
            Profile p{entry.at("id").get<std::string>(), entry.at("name").get<std::string>(),
                      entry.value("character", std::string{}), entry.value("palette", 0),
                      entry.value("coin", std::string{})};
            const bool numeric = !p.id.empty() && std::all_of(p.id.begin(), p.id.end(), [](char c) {
                return c >= '0' && c <= '9';
            });
            if (!numeric || !valid_profile_name(p.name) || p.palette < 0 || p.palette > 7) {
                error = "profiles.json has an invalid profile";
                return false;
            }
            profiles_.push_back(std::move(p));
        }
        active_id_ = document.at("active").get<std::string>();
    } catch (const std::exception&) {
        error = "profiles.json is malformed";
        return false;
    }
    if (profiles_.empty() ||
        std::none_of(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.id == active_id_; })) {
        error = "profiles.json has no valid active profile";
        return false;
    }
    error.clear();
    return true;
}

bool ProfileStore::save(std::string& error) const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& p : profiles_)
        list.push_back({{"id", p.id}, {"name", p.name}, {"character", p.character}, {"palette", p.palette},
                        {"coin", p.coin}});
    const nlohmann::json document{{"schema_version", schema_version}, {"active", active_id_}, {"profiles", list}};
    return write_atomically(data_directory_ / list_file, document.dump(2) + "\n", error);
}

const Profile& ProfileStore::active() const {
    for (const auto& p : profiles_) {
        if (p.id == active_id_) return p;
    }
    return profiles_.front();
}

std::filesystem::path ProfileStore::directory(const Profile& profile) const {
    return data_directory_ / "profiles" / profile.id;
}

const Profile* ProfileStore::find(std::string_view name) const {
    for (const auto& p : profiles_) {
        if (same_name(p.name, name)) return &p;
    }
    return nullptr;
}

std::string ProfileStore::next_id() const {
    // Never reuse an id still held by a deleted profile's folder name.
    int next = 1;
    for (const auto& p : profiles_) next = std::max(next, std::stoi(p.id) + 1);
    for (const auto& d : deleted()) next = std::max(next, std::stoi(d.profile.id) + 1);
    return std::to_string(next);
}

bool ProfileStore::add(std::string_view name, std::string& error) {
    if (!valid_profile_name(name)) {
        error = "profile names are 1 to 24 printable characters";
        return false;
    }
    if (find(name) != nullptr) {
        error = "a profile with that name already exists";
        return false;
    }
    profiles_.push_back({next_id(), std::string(name), {}, 0, {}});
    return save(error);
}

bool ProfileStore::use(std::string_view name, std::string& error) {
    if (find(name) == nullptr && !add(name, error)) return false;
    active_id_ = find(name)->id;
    return save(error);
}

bool ProfileStore::set_character(std::string_view name, std::string_view character, int palette,
                                 std::string& error) {
    auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return same_name(p.name, name); });
    if (it == profiles_.end()) {
        error = "no profile with that name";
        return false;
    }
    const bool valid_character = character.size() <= 32 &&
        std::all_of(character.begin(), character.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        });
    if (!valid_character || palette < 0 || palette > 7) {
        error = "characters are up to 32 of a-z, 0-9 and '-'; colour variants are 0 to 7";
        return false;
    }
    it->character = std::string(character);
    it->palette = palette;
    return save(error);
}

bool ProfileStore::set_coin(std::string_view name, std::string_view coin, std::string& error) {
    auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return same_name(p.name, name); });
    if (it == profiles_.end()) {
        error = "no profile with that name";
        return false;
    }
    const bool valid = coin.size() <= 32 && std::all_of(coin.begin(), coin.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
    if (!valid) {
        error = "coins are up to 32 of a-z, 0-9 and '-'";
        return false;
    }
    it->coin = std::string(coin);
    return save(error);
}

bool ProfileStore::rename(std::string_view name, std::string_view new_name, std::string& error) {
    auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return same_name(p.name, name); });
    if (it == profiles_.end()) {
        error = "no profile with that name";
        return false;
    }
    if (!valid_profile_name(new_name)) {
        error = "profile names are 1 to 24 printable characters";
        return false;
    }
    const auto* other = find(new_name);
    if (other != nullptr && other->id != it->id) {
        error = "a profile with that name already exists";
        return false;
    }
    it->name = std::string(new_name);
    return save(error);
}

bool ProfileStore::remove(std::string_view name, std::string& error) {
    auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return same_name(p.name, name); });
    if (it == profiles_.end()) {
        error = "no profile with that name";
        return false;
    }
    const Profile removed = *it;
    std::error_code code;
    const auto trash = data_directory_ / "deleted";
    std::filesystem::create_directories(trash, code);
    const auto destination = trash / (removed.id + "-" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                       std::chrono::system_clock::now().time_since_epoch()).count()));
    const auto source = directory(removed);
    std::filesystem::create_directories(source, code);
    // Keep the profile's details with its files, for restoring.
    {
        std::ofstream(source / "profile.json", std::ios::binary)
            << nlohmann::json{{"id", removed.id}, {"name", removed.name}, {"character", removed.character},
                              {"palette", removed.palette}, {"coin", removed.coin}}.dump(2) << "\n";
    }
    std::filesystem::rename(source, destination, code);
    if (code) {
        error = "could not move the profile to deleted: " + code.message();
        return false;
    }
    profiles_.erase(it);
    if (profiles_.empty()) profiles_.push_back({next_id(), "Player 1", {}, 0, {}});
    if (std::none_of(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.id == active_id_; }))
        active_id_ = profiles_.front().id;
    return save(error);
}

std::vector<DeletedProfile> ProfileStore::deleted() const {
    std::vector<DeletedProfile> result;
    std::error_code code;
    const auto trash = data_directory_ / "deleted";
    if (!std::filesystem::is_directory(trash, code)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(trash, code)) {
        try {
            std::ifstream in(entry.path() / "profile.json", std::ios::binary);
            const auto document = nlohmann::json::parse(in);
            result.push_back({entry.path().filename().string(),
                              {document.at("id").get<std::string>(), document.at("name").get<std::string>(),
                               document.value("character", std::string{}), document.value("palette", 0),
                               document.value("coin", std::string{})}});
        } catch (const std::exception&) {
        }
    }
    // Newest first (folders end with the deletion time).
    std::sort(result.begin(), result.end(), [](const DeletedProfile& a, const DeletedProfile& b) {
        const auto time = [](const std::string& folder) { return std::stoll(folder.substr(folder.find('-') + 1)); };
        return time(a.folder) > time(b.folder);
    });
    return result;
}

bool ProfileStore::restore(std::string_view name, std::string& error) {
    if (find(name) != nullptr) {
        error = "a profile with that name already exists";
        return false;
    }
    for (const auto& d : deleted()) {
        if (!same_name(d.profile.name, name)) continue;
        Profile restored = d.profile;
        const bool id_taken = std::any_of(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.id == restored.id; });
        if (id_taken) restored.id = next_id();
        std::error_code code;
        std::filesystem::create_directories(data_directory_ / "profiles", code);
        std::filesystem::rename(data_directory_ / "deleted" / d.folder, directory(restored), code);
        if (code) {
            error = "could not restore the profile: " + code.message();
            return false;
        }
        std::filesystem::remove(directory(restored) / "profile.json", code);
        profiles_.push_back(restored);
        return save(error);
    }
    error = "no deleted profile with that name";
    return false;
}

void ProfileStore::purge_deleted(int days) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::error_code code;
    for (const auto& d : deleted()) {
        const auto when = std::stoll(d.folder.substr(d.folder.find('-') + 1));
        if (now - when > static_cast<long long>(days) * 24 * 60 * 60)
            std::filesystem::remove_all(data_directory_ / "deleted" / d.folder, code);
    }
}

bool rotate_backup(const std::filesystem::path& file, const std::filesystem::path& backups, int keep,
                   std::string& error) {
    std::error_code code;
    if (!std::filesystem::exists(file, code)) return true;
    std::filesystem::create_directories(backups, code);
    const auto stem = file.stem().string();
    const auto extension = file.extension().string();
    auto numbered = [&](int n) { return backups / (stem + "." + std::to_string(n) + extension); };
    std::filesystem::remove(numbered(keep), code);
    for (int n = keep - 1; n >= 1; --n) {
        if (std::filesystem::exists(numbered(n), code)) std::filesystem::rename(numbered(n), numbered(n + 1), code);
    }
    std::filesystem::copy_file(file, numbered(1), std::filesystem::copy_options::overwrite_existing, code);
    if (code) {
        error = "could not back up " + file.string() + ": " + code.message();
        return false;
    }
    return true;
}

std::vector<std::string> migrate_legacy_data(const std::filesystem::path& legacy,
                                             const std::filesystem::path& data_directory) {
    std::vector<std::string> copied;
    std::error_code code;
    if (legacy.empty() || !std::filesystem::is_directory(legacy, code)) return copied;
    if (std::filesystem::equivalent(legacy, data_directory, code)) return copied;
    if (std::filesystem::exists(data_directory / list_file, code)) return copied;
    // Shared by the PC: settings, controller mappings and the ROM cache.
    // Per player: the cartridge save and trophies, into the first profile.
    const std::pair<const char*, std::filesystem::path> files[]{
        {"settings.dat", data_directory / "settings.dat"},
        {"gamecontrollerdb.txt", data_directory / "gamecontrollerdb.txt"},
        {"tetrisphere-us-rev0/rom.z64", data_directory / "tetrisphere-us-rev0" / "rom.z64"},
        {"tetrisphere-us-rev0.eep", data_directory / "profiles" / "1" / "cartridge.eep"},
        {"trophies.json", data_directory / "profiles" / "1" / "trophies.json"},
    };
    for (const auto& [name, target] : files) {
        const auto source = legacy / name;
        if (!std::filesystem::is_regular_file(source, code) || std::filesystem::exists(target, code)) continue;
        std::filesystem::create_directories(target.parent_path(), code);
        std::filesystem::copy_file(source, target, code);
        if (!code) copied.emplace_back(name);
    }
    return copied;
}

} // namespace tetrisphere
