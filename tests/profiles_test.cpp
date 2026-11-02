#include "tetrisphere/profiles.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace tetrisphere;

namespace {

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string error;

    // A new data folder starts with one profile, "Player 1", in folder 1.
    const auto data = root / "data";
    {
        ProfileStore store(data);
        assert(store.load(error));
        assert(store.profiles().size() == 1 && store.active().name == "Player 1");
        assert(store.directory(store.active()) == data / "profiles" / "1");
        assert(std::filesystem::exists(data / "profiles.json"));
    }
    // Using a name creates a profile and makes it active; it is remembered,
    // and names match regardless of case.
    {
        ProfileStore store(data);
        assert(store.load(error));
        assert(store.use("Sam", error) && store.active().id == "2");
        assert(!store.use("", error) && !store.use(std::string(25, 'x'), error));
    }
    {
        ProfileStore store(data);
        assert(store.load(error) && store.active().name == "Sam" && store.profiles().size() == 2);
        assert(store.use("player 1", error) && store.active().id == "1" && store.profiles().size() == 2);
    }
    // Damaged lists are reported, not replaced.
    write(root / "bad" / "profiles.json", "{not json");
    {
        ProfileStore store(root / "bad");
        assert(!store.load(error) && !error.empty());
    }

    // Backups keep the newest copies, numbered from 1.
    const auto save = root / "backup" / "cartridge.eep";
    for (int session = 1; session <= 5; ++session) {
        write(save, "session " + std::to_string(session));
        assert(rotate_backup(save, root / "backup" / "backups", 3, error));
    }
    assert(read(root / "backup" / "backups" / "cartridge.1.eep") == "session 5");
    assert(read(root / "backup" / "backups" / "cartridge.3.eep") == "session 3");
    assert(!std::filesystem::exists(root / "backup" / "backups" / "cartridge.4.eep"));
    assert(rotate_backup(root / "backup" / "missing.eep", root / "backup" / "backups", 3, error));

    // Earlier builds' data beside the executable is copied once: shared files
    // to the data folder, the save and trophies to the first profile.
    const auto legacy = root / "game" / "data";
    write(legacy / "settings.dat", "settings");
    write(legacy / "tetrisphere-us-rev0.eep", "cartridge");
    write(legacy / "trophies.json", "{}");
    write(legacy / "tetrisphere-us-rev0" / "rom.z64", "rom");
    const auto fresh = root / "user";
    const auto copied = migrate_legacy_data(legacy, fresh);
    assert(copied.size() == 4);
    assert(read(fresh / "profiles" / "1" / "cartridge.eep") == "cartridge");
    assert(read(fresh / "tetrisphere-us-rev0" / "rom.z64") == "rom");
    assert(std::filesystem::exists(legacy / "tetrisphere-us-rev0.eep"));  // originals stay
    {
        ProfileStore store(fresh);
        assert(store.load(error));
    }
    write(legacy / "tetrisphere-us-rev0.eep", "newer");
    assert(migrate_legacy_data(legacy, fresh).empty());  // only once
    assert(read(fresh / "profiles" / "1" / "cartridge.eep") == "cartridge");
    assert(migrate_legacy_data(legacy, legacy).empty());  // portable: same folder
    std::filesystem::remove_all(root);
    return 0;
}
