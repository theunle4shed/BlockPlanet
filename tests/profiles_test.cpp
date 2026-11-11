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
    // "Guest" is kept for guest play, whatever its case and spaces.
    assert(reserved_profile_name("Guest"));
    assert(reserved_profile_name("guest"));
    assert(reserved_profile_name(" GUEST "));
    assert(!reserved_profile_name("Guests"));
    assert(!reserved_profile_name("Gues t"));
    assert(!reserved_profile_name("Player 1"));
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
    // Characters: chosen per profile, with a colour variant.
    {
        ProfileStore store(data);
        assert(store.load(error));
        assert(store.set_character("Sam", "rescue-3", 2, error));
        assert(!store.set_character("Sam", "Bad Name", 0, error));
        assert(!store.set_character("Sam", "rescue-3", 8, error));
        assert(!store.set_character("Nobody", "rescue-3", 0, error));
    }
    {
        ProfileStore store(data);
        assert(store.load(error));
        assert(store.find("sam")->character == "rescue-3" && store.find("sam")->palette == 2);
        assert(!store.add("SAM", error));  // names are unique regardless of case
    }
    // Renaming keeps the folder; clearing the character is allowed.
    {
        ProfileStore store(data);
        assert(store.load(error));
        const auto id = store.find("Sam")->id;
        assert(store.rename("Sam", "Sammy", error) && store.find("sammy")->id == id);
        assert(!store.rename("Sammy", "player 1", error));  // taken
        assert(!store.rename("Sammy", "", error) && !store.rename("Nobody", "X", error));
        assert(store.rename("Sammy", "SAMMY", error));  // case only
        assert(store.rename("SAMMY", "Sam", error));
        assert(store.set_character("Sam", "", 0, error) && store.find("Sam")->character.empty());
        assert(store.set_character("Sam", "rescue-3", 2, error));
        // Coins: the same form as characters; kept across a reload.
        assert(store.set_coin("Sam", "rocket", error) && !store.set_coin("Sam", "Bad Coin", error));
        assert(!store.set_coin("Nobody", "rocket", error));
    }
    {
        ProfileStore store(data);
        assert(store.load(error) && store.find("Sam")->coin == "rocket");
    }
    // Deleting moves the profile aside; restoring brings it and its files back.
    {
        ProfileStore store(data);
        assert(store.load(error));
        write(store.directory(*store.find("Sam")) / "cartridge.eep", "sam's save");
        assert(store.use("Sam", error));
        assert(store.remove("Sam", error));
        assert(store.find("Sam") == nullptr && store.active().id == "1");
        assert(store.deleted().size() == 1 && store.deleted().front().profile.name == "Sam");
        assert(!std::filesystem::exists(data / "profiles" / "2"));
        assert(store.add("Alex", error) && store.find("Alex")->id == "3");  // id 2 is still held
        assert(store.restore("sam", error));
        assert(store.find("Sam")->id == "2" && store.find("Sam")->character == "rescue-3" &&
               store.find("Sam")->coin == "rocket");
        assert(read(data / "profiles" / "2" / "cartridge.eep") == "sam's save");
        assert(store.deleted().empty() && !store.restore("Sam", error));
    }
    // Deleting the last profile leaves a fresh "Player 1".
    {
        ProfileStore store(root / "single");
        assert(store.load(error));
        assert(store.remove("Player 1", error));
        assert(store.profiles().size() == 1 && store.active().name == "Player 1" && store.active().id == "2");
        store.purge_deleted(30);
        assert(store.deleted().size() == 1);  // recent deletions are kept
        store.purge_deleted(-1);
        assert(store.deleted().empty());
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
