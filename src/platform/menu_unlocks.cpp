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
        {"solo", "Solo", "", "open:solo", "Play on your own.", Status::Viable, 0},
        {"training", "Training", "", "command:training_story", "Wheels shows you how to play, step by step.", Status::Viable, 0},
        {"local", "Local", "", "open:local", "Play with friends on this PC.", Status::InProgress, -1},
        {"online", "Online", "", "open:online", "Play with friends and others over the internet.", Status::NotStarted, -1},
        {"profiles", "Profiles", "", "command:profiles", "Choose who is playing, or add a profile.", Status::Viable, 0},
        {"media", "Media Player", "", "command:media_player", "", Status::NotStarted, -1},
        {"trophies", "Trophies", "", "command:trophies", "", Status::InProgress, -1},
        {"options", "Options", "", "open:options", "Graphics, sound, controls and more.", Status::Viable, 0},
        {"credits", "Credits", "", "command:credits", "The people who made Tetrisphere.", Status::Viable, 0},
        {"exit", "Exit to Desktop", "", "command:exit", "", Status::None, 0},
    };
    auto items = build(entries, u);
    for (auto& i : items) i.focus = false;  // the main menu keeps its own focus (Continue, when shown, comes first)
    return items;
}

std::vector<blockplanet::ui::ListItem> solo_menu_items(const MenuUnlocks& u) {
    static const std::vector<Entry> entries{
        {"story", "Rescue", "", "command:open_story",
         "Break through each sphere to its core and rescue the friend inside, episode by episode.", Status::Viable, 0},
        {"adventure", "Adventure", "", "command:adventure",
         "A journey across the sphere that introduces new game modes one at a time.", Status::NotStarted, 5},
        {"bonus", "Bonus", "", "command:bonus", "The game modes Adventure has introduced, to play on their own.", Status::NotStarted, -1},
        {"hide_and_seek", "Hide & Seek", "", "command:hide_and_seek", "Dig away the blocks to find the pictures hidden in each sphere.",
         Status::Viable, -1},
        {"puzzle", "Puzzle", "", "command:puzzle", "Clear set spheres with the pieces you are given. Think before you drop.", Status::Viable, -1,
         "command:puzzle_chapter:0", "Puzzle Training"},
        {"lines", "Lines", "", "command:lines", "A different game on the sphere, with its own levels.", Status::Viable, -1},
        {"minefield", "Minefield", "", "command:minefield", "Uncover the grid without setting off a mine: each number counts the mines around it.",
         Status::InProgress, -1},
        {"multiphasic", "Multiphasic", "", "command:multiphasic", "One long run: each core you open becomes the next sphere. How far can you go?",
         Status::NotStarted, -1},
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

} // namespace tetrisphere
