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

    // Story: the chapters done and the next (Training is on X, not a chapter).
    auto story = story_chapters({}, 15);
    assert(story.size() == 1 && story[0].action == "command:stages:1" && story[0].focus && story[0].note == "next");
    {
        // A chapter's stages: the whole episode, then the stages done and the next.
        auto stages = story_stages(2, {1, 2}, {40.5, 0.0, 0.0});
        assert(stages.size() == 4 && stages[0].action == "command:chapter:2" && stages[0].focus);
        assert(stages[1].label == "Stage 2:1" && stages[1].action == "command:stage:2:1" && stages[1].note == "best 0:40");
        assert(stages[3].label == "Stage 2:3" && stages[3].note == "next");
        // Only the furthest open stage is "next", not every stage not yet played.
        const auto gaps = story_stages(1, {5}, {});
        assert(gaps.size() == 7 && gaps[1].note.empty() && gaps[5].note.empty() && gaps[6].note == "next");
        assert(story_stages(1, {}, {}).size() == 2);
        // Puzzles: the solved ones and the next.
        const auto puzzles = puzzle_list({1, 2}, {12.0}, {2.0});
        assert(puzzles.size() == 3 && puzzles[0].note == "best 0:12 \u00B7 2 moves" && puzzles[1].note == "solved");
        assert(puzzles[2].label == "Puzzle 3" && puzzles[2].note == "next" && puzzles[2].focus &&
               puzzles[2].action == "command:puzzle:3");
        assert(puzzle_list({}, {}, {}).size() == 1);
        // The game's capitals in BlockPlanet's style.
        assert(proper_case("TRY AGAIN") == "Try Again" && proper_case("EXIT") == "Exit");
        assert(proper_case("HIDE&SEEK") == "Hide & Seek" && proper_case("VS CPU") == "VS CPU");
        assert(proper_case("CLOSE GAME") == "Close Game" && proper_case("PLAYER 1") == "Player 1");
        assert(proper_case("YOU MAY WANT TO TRY TRAINING") == "You may want to try training");
        assert(proper_case("ONE. TWO THREE FOUR") == "One. Two three four");
        assert(proper_case("Already fine") == "Already fine" && proper_case("1:23") == "1:23");
        assert(proper_case("DIG WITH THE [A] BUTTON. THEN GO") == "Dig with the [A] button. Then go");
        assert(proper_case("PRESS [x:A] TO GO ON") == "Press [x:A] to go on");
        assert(game_quotes("LET%S GO !") == "LET\u2019S GO!");
        assert(fix_wording("AROUND THE BALL. THE -1BALL%S CORE, BALLOON") == "AROUND THE SPHERE. THE -1SPHERE%S CORE, BALLOON");
        assert(fix_wording("IT TALLYS. YOU CAN NOT! WHY ?? YES !!") == "IT TALLIES. YOU CANNOT! WHY ? YES !");
        assert(game_quotes("YOUR %X% COUNT ??") == "YOUR \u2018X\u2019 COUNT??");
        assert(proper_case(game_quotes("HI! MY NAME IS WHEELS. I WILL SHOW YOU TETRISPHERE. I%M HERE")) ==
               "Hi! My name is Wheels. I will show you Tetrisphere. I\u2019m here");
        assert(proper_case(game_quotes("ONLY FUSE COMBOS GET YOUR %X% COUNT UP")) == "Only fuse combos get your \u2018X\u2019 count up");
    }
    story = story_chapters({0}, 15);
    assert(story.size() == 1 && story[0].focus && story[0].note == "next");
    story = story_chapters({1, 2}, 15);
    assert(story.size() == 3 && story[2].focus && story[2].label == "Chapter 3" && story[0].note == "completed");
    story = story_chapters({0, 15}, 15);
    assert(story.size() == 15 && story[0].focus && story[0].note == "next" && story[1].note.empty() &&
           story[14].note == "completed");

    auto lessons = story_training(false, false);
    assert(lessons.size() == 2 && lessons[0].focus && lessons[0].action == "command:training" &&
           lessons[0].note == "start here" && !lessons[1].focus && lessons[1].action == "command:training_advanced");
    lessons = story_training(true, false);
    assert(!lessons[0].focus && lessons[1].focus && lessons[0].note == "completed");

    auto confirm = delete_confirmation(store, "1");
    assert(confirm.size() == 2 && confirm[0].action == "command:profile_delete_confirmed:1" &&
           confirm[1].action == "back" && confirm[1].focus);
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
