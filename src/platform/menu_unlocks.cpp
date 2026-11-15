#include "tetrisphere/menu_unlocks.h"

#include <string>

namespace tetrisphere {
namespace {

enum class Status { NotStarted, InProgress, Viable, None };

struct Entry {
    const char* id;
    const char* label;
    const char* icon;  // a character of the menus' icon font (Lucide), UTF-8
    const char* action;
    const char* description;
    Status status;
    int episodes_needed;  // Rescue episodes to complete first; -1: never on its own (comes later, through Adventure)
    const char* alt = "";
    const char* alt_label = "";
};

blockplanet::ui::ListItem item(const Entry& e, const MenuUnlocks& u) {
    blockplanet::ui::ListItem i;
    i.id = e.id;
    i.label = e.label;
    i.icon = e.icon;
    i.action = e.action;
    i.description = e.description;
    i.alt = e.alt;
    i.alt_label = e.alt_label;
    // The development status, in the main build only (menu.rcss colours it).
    if (u.everything && e.status != Status::None) {
        i.note = e.status == Status::Viable ? "(Viable)" : e.status == Status::InProgress ? "(In progress)" : "(Not started)";
        i.classes = e.status == Status::Viable ? "rag-viable" : e.status == Status::InProgress ? "rag-in-progress" : "rag-not-started";
    }
    return i;
}

bool unlocked(const Entry& e, const MenuUnlocks& u) {
    if (u.everything) return true;
    if (e.episodes_needed < 0) return false;
    for (int n = 1; n <= e.episodes_needed; ++n)
        if (u.episodes.count(n) == 0) return false;
    return true;
}

std::vector<blockplanet::ui::ListItem> build(const std::vector<Entry>& entries, const MenuUnlocks& u) {
    std::vector<blockplanet::ui::ListItem> items;
    for (const auto& e : entries)
        if (unlocked(e, u)) items.push_back(item(e, u));
    if (!items.empty()) items.front().focus = true;
    return items;
}

} // namespace

std::vector<blockplanet::ui::ListItem> main_menu_items(const MenuUnlocks& u) {
    static const std::vector<Entry> entries{
        {"solo", "Solo", "\uE19F", "open:solo", "Play on your own.", Status::Viable, 0},
        {"training", "Training", "\uE234", "command:training_story", "Wheels shows you how to play, step by step.", Status::Viable, 0},
        {"local", "Local", "\uE1A4", "open:local", "Play with friends on this PC.", Status::InProgress, -1},
        {"online", "Online", "\uE0EB", "open:online", "Play with friends and others over the internet.", Status::NotStarted, -1},
        {"credits", "Credits", "\uE464", "command:credits", "The people who made Tetrisphere.", Status::Viable, 0},
    };
    auto items = build(entries, u);
    for (auto& i : items) i.focus = false;  // the main menu keeps its own focus (Continue, when shown, comes first)
    return items;
}

std::vector<blockplanet::ui::ListItem> main_icon_items(const MenuUnlocks& u) {
    // Icons only, in a row (the owner's layout, 2026-11-14); the description
    // names each one.
    static const std::vector<Entry> entries{
        {"profiles", "", "\uE466", "command:profiles", "Profiles: choose who is playing, or add a profile.", Status::None, 0},
        {"trophies", "", "\uE377", "command:trophies", "Trophies (in progress).", Status::None, -1},
        {"media", "", "\uE126", "command:media_player", "Media Player: the game's music and your own (not started).", Status::None, -1},
        {"options", "", "\uE158", "open:options", "Options: graphics, sound, controls and more.", Status::None, 0},
        {"exit", "", "\uE112", "command:exit", "Exit to the desktop.", Status::None, 0},
    };
    auto items = build(entries, u);
    for (auto& i : items) i.focus = false;
    return items;
}

std::vector<blockplanet::ui::ListItem> solo_menu_items(const MenuUnlocks& u) {
    static const std::vector<Entry> entries{
        {"story", "Rescue", "\uE063", "command:open_story",
         "Break through each sphere to its core and rescue the friend inside, episode by episode.", Status::Viable, 0},
        {"adventure", "Adventure", "\uE114", "command:adventure",
         "A journey across the sphere that introduces new game modes one at a time.", Status::NotStarted, 5},
        {"bonus", "Bonus", "\uE0E4", "open:bonus", "The game modes Adventure has introduced, to play on their own.", Status::InProgress, -1},
    };
    return build(entries, u);
}

std::vector<blockplanet::ui::ListItem> solo_bonus_items(const MenuUnlocks& u) {
    static const std::vector<Entry> entries{
        {"hide_and_seek", "Hide & Seek", "\uE155", "command:hide_and_seek", "Dig away the blocks to find the pictures hidden in each sphere.",
         Status::Viable, -1},
        {"puzzle", "Puzzle", "\uE29C", "command:puzzle", "Clear set spheres with the pieces you are given. Think before you drop.", Status::Viable, -1,
         "command:puzzle_chapter:0", "Puzzle Training"},
        {"lines", "Lines", "\uE58F", "command:open_lines", "A different game on the sphere: line up three bricks to remove them. Five episodes.", Status::Viable, -1},
        {"minefield", "Minefield", "\uE0EC", "command:minefield", "Uncover the grid without setting off a mine: each number counts the mines around it.",
         Status::InProgress, -1},
        {"multiphasic", "Multiphasic", "\uE104", "command:multiphasic", "One long run: each core you open becomes the next sphere. How far can you go?",
         Status::NotStarted, -1},
        {"escape", "Escape", "\uE3DA", "command:escape", "The view from inside the sphere: break your way out through a hole.", Status::NotStarted, -1},
    };
    return build(entries, u);
}

std::vector<blockplanet::ui::ListItem> local_menu_items(const MenuUnlocks& u) {
    static const std::vector<Entry> entries{
        {"story", "Rescue", "\uE063", "open:local_story", "Rescue's spheres for two: race each other to the core, or work together on one sphere.",
         Status::InProgress, -1},
        {"bonus", "Bonus", "\uE0E4", "open:local_bonus", "The game modes Adventure has introduced, for two.", Status::InProgress, -1},
    };
    return build(entries, u);
}

std::vector<blockplanet::ui::ListItem> local_bonus_items(const MenuUnlocks& u) {
    static const std::vector<Entry> entries{
        {"lines", "Lines", "\uE58F", "open:local_lines", "Lines for two.", Status::NotStarted, -1},
        {"custom", "Custom", "\uE29A", "command:multiplayer_custom", "Your own rules for a game with friends.", Status::NotStarted, -1},
    };
    return build(entries, u);
}

std::vector<blockplanet::ui::ListItem> rescue_menu_items(const MenuUnlocks& u) {
    static const std::vector<Entry> entries{
        {"campaign", "Campaign", "", "command:campaign", "Carry on from your next stage, episode by episode.", Status::Viable, 1},
        {"score_attack", "Score Attack", "", "command:score_attack",
         "As many points as you can on Rescue's stages, against the clock. Combos add time, and time left becomes points.", Status::Viable, 1},
        {"time_attack", "Time Attack", "", "command:time_attack",
         "Rescue's episodes as fast as you can, against your best time for every stage. A whole episode or one stage.", Status::Viable, 3},
        {"casual", "Casual", "", "command:casual", "Rescue without a game over: lost hearts come back. No records or trophies.", Status::Viable, 1},
    };
    return build(entries, u);
}

bool rescue_menu_open(const MenuUnlocks& u) { return u.everything || u.episodes.count(1) != 0; }

std::vector<blockplanet::ui::ListItem> lines_menu_items(const MenuUnlocks& u) {
    // Lines as Rescue (the owner, 2026-11-15): Campaign, Time Attack and
    // Score Attack on its five episodes.
    static const std::vector<Entry> entries{
        {"lines_campaign", "Campaign", "", "command:lines_campaign", "Carry on from your next stage, episode by episode.", Status::Viable, 0},
        {"lines_time_attack", "Time Attack", "", "command:lines_time_attack",
         "Lines' episodes as fast as you can, against your best time for every stage. A whole episode or one stage.", Status::InProgress, 0},
        {"lines_score_attack", "Score Attack", "", "command:lines_score_attack",
         "As many points as you can on Lines' stages, against the clock.", Status::InProgress, 0},
    };
    return build(entries, u);
}

} // namespace tetrisphere
