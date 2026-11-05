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
    assert(actions.size() == 4 && actions[0].action == "close" && actions[3].action == "command:profile_delete:" + sam);
    actions = profile_actions(store, "1", false);
    assert(actions[0].label == "Play as Player 1" && actions[0].action == "command:profile_play:1");
    actions = profile_actions(store, sam, true);
    assert(actions[0].action == "command:profile_play:" + sam);
    assert(profile_actions(store, "99", false).empty());

    assert(actions.size() == 4 && actions[1].action == "command:profile_rename:" + sam &&
           actions[2].action == "command:profile_character:" + sam && actions[2].note.empty());

    // Characters: None, then the six robots; the chosen one is marked and
    // shown in the profile list.
    auto choices = character_choices(store, sam);
    assert(choices.size() == 7 && choices[0].note == "chosen" &&
           choices[0].action == "command:profile_set_character:" + sam + ":none");
    assert(store.set_character("Sam", "gyro", 3, error));
    choices = character_choices(store, sam);
    assert(choices[0].note.empty() && choices[3].label == "Gyro" && choices[3].note == "chosen");
    assert(profile_list(store, false)[0].note == "Gyro \u00B7 playing now");
    assert(profile_actions(store, sam, false)[2].note == "Gyro");
    assert(character_label("nobody").empty() && colour_names().size() == 8);

    // Names: on-screen keys (shift lasts one letter), typing, limits.
    NameEditor editor;
    editor.start("new", "");
    auto keys = editor.keyboard();
    assert(keys.size() == 44 && keys[0].label == "1" && keys[10].label == "Q" && keys[20].label == "A" &&
           keys[29].id == "keyquote" && keys[30].label == "Z");
    int spans = 0;
    for (std::size_t i = 40; i < keys.size(); ++i) spans += keys[i].span;
    assert(spans == keyboard_columns && keys[41].id == "keyspace" && keys[43].action == "command:name_done");
    assert(keys[40].label == "SHIFT");
    assert(editor.press("A") && editor.keyboard()[20].label == "a" && editor.keyboard()[40].label == "Shift");
    assert(!editor.press("B") && editor.text() == "Ab");
    editor.press("space");
    editor.press("space");  // no double spaces
    editor.press("7");
    editor.press("delete");
    editor.press("delete");
    assert(editor.text() == "Ab");
    editor.type("c\x01 Dé!");
    assert(editor.text() == "Abc D!");
    editor.type(std::string(40, 'x'));
    assert(editor.text().size() == 24);
    editor.start("1", "Sam");
    assert(editor.target() == "1" && editor.keyboard()[10].label == "q");
    editor.press("shift");
    editor.press("Q");
    assert(editor.text() == "SamQ");

    // Story: Chapter 0 (Training) first; a new player starts on it with
    // Chapter 1 as the skip; afterwards the focus is on the next chapter.
    auto story = story_chapters({}, 15);
    assert(story.size() == 2 && story[0].action == "command:chapter:0" && story[0].focus &&
           story[0].note == "start here" && story[1].note == "skip training" && !story[1].focus);
    story = story_chapters({0}, 15);
    assert(story.size() == 2 && story[0].note == "completed" && story[1].focus && story[1].note == "next");
    story = story_chapters({1, 2}, 15);
    assert(story.size() == 4 && story[0].note == "start here" && !story[0].focus && story[3].focus &&
           story[3].label == "Chapter 3");
    story = story_chapters({0, 15}, 15);
    assert(story.size() == 16 && story[1].focus && story[1].note == "next" && story[2].note.empty() &&
           story[15].note == "completed");

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
