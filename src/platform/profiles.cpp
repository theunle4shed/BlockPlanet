#include "tetrisphere/profiles.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace tetrisphere {
namespace {

constexpr int schema_version = 1;
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
            Profile p{entry.at("id").get<std::string>(), entry.at("name").get<std::string>()};
            const bool numeric = !p.id.empty() && std::all_of(p.id.begin(), p.id.end(), [](char c) {
                return c >= '0' && c <= '9';
            });
            if (!numeric || !valid_profile_name(p.name)) {
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
    for (const auto& p : profiles_) list.push_back({{"id", p.id}, {"name", p.name}});
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

bool ProfileStore::use(std::string_view name, std::string& error) {
    if (!valid_profile_name(name)) {
        error = "profile names are 1 to 24 printable characters";
        return false;
    }
    for (const auto& p : profiles_) {
        if (same_name(p.name, name)) {
            active_id_ = p.id;
            return save(error);
        }
    }
    int next = 1;
    for (const auto& p : profiles_) next = std::max(next, std::stoi(p.id) + 1);
    profiles_.push_back({std::to_string(next), std::string(name)});
    active_id_ = profiles_.back().id;
    return save(error);
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
