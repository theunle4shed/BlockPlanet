#include "tetrisphere/profile_menu.h"
#include "tetrisphere/relaunch.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

using namespace tetrisphere;

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto data = std::filesystem::path(argv[1]) /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string error;
    ProfileStore store(data);
    assert(store.load(error));
    assert(next_profile_name(store) == "Player 2");
    assert(store.add("Sam", error) && store.add("Player 3", error));
    assert(next_profile_name(store) == "Player 2");
    assert(store.use("Sam", error));
    const auto sam = store.active().id;

    // The playing profile comes first, then the others, New and Guest.
    auto list = profile_list(store, false);
    assert(list.size() == 5);
    assert(list[0].label == "Sam" && list[0].note == "playing now" && list[0].action == "command:profile:" + sam);
    assert(list[1].label == "Player 1" && list[1].note.empty());
    assert(list[3].action == "command:profile_new");
    assert(list[4].action == "command:profile_guest");
    // As a guest no profile is playing; Guest just closes the menus.
    list = profile_list(store, true);
    assert(list.size() == 5 && list[0].label == "Player 1" && list[4].note == "playing now" && list[4].action == "close");

    // The playing profile can be kept; another one restarts the game.
    auto actions = profile_actions(store, sam, false);
    assert(actions.size() == 2 && actions[0].action == "close" && actions[1].action == "command:profile_delete:" + sam);
    actions = profile_actions(store, "1", false);
    assert(actions[0].label == "Play as Player 1" && actions[0].action == "command:profile_play:1");
    actions = profile_actions(store, sam, true);
    assert(actions[0].action == "command:profile_play:" + sam);
    assert(profile_actions(store, "99", false).empty());

    auto confirm = delete_confirmation(store, "1");
    assert(confirm.size() == 1 && confirm[0].action == "command:profile_delete_confirmed:1");
    assert(delete_confirmation(store, "99").empty());

    // Restart arguments: the old choice is replaced, everything else kept.
    const std::vector<std::string> original{"--profile=Old", "--sphere=original", "--guest", "rom.z64"};
    auto args = relaunch_arguments(original, std::string("Sam"), false);
    assert((args == std::vector<std::string>{"--profile=Sam", "--sphere=original", "rom.z64"}));
    args = relaunch_arguments(original, std::nullopt, true);
    assert((args == std::vector<std::string>{"--guest", "--sphere=original", "rom.z64"}));
    const char* argv_in[] = {"game", "--profile=A", "x"};
    set_launch_arguments(3, argv_in);
    assert((launch_arguments() == std::vector<std::string>{"--profile=A", "x"}));

    std::filesystem::remove_all(data);
    return 0;
}
