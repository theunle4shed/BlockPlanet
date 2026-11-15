#include "tetrisphere/menu_unlocks.h"

#include <algorithm>
#include <cassert>
#include <string>

using namespace tetrisphere;

namespace {
bool has(const std::vector<blockplanet::ui::ListItem>& items, const std::string& id) {
    return std::any_of(items.begin(), items.end(), [&](const auto& i) { return i.id == id; });
}
} // namespace

int main() {
    MenuUnlocks fresh;
    // From the start: Solo, Training, Credits; Profiles, Options and Exit as icons. Solo is Rescue alone.
    auto main_items = main_menu_items(fresh);
    assert(has(main_items, "solo") && has(main_items, "training") && has(main_items, "credits"));
    assert(!has(main_items, "local") && !has(main_items, "online"));
    auto icons = main_icon_items(fresh);
    assert(has(icons, "profiles") && has(icons, "options") && has(icons, "exit") && !has(icons, "trophies"));
    for (const auto& i : icons) assert(i.label.empty() && !i.icon.empty() && !i.description.empty());
    auto solo = solo_menu_items(fresh);
    assert(solo.size() == 1 && solo[0].id == "story" && solo[0].note.empty());
    assert(!rescue_menu_open(fresh) && rescue_menu_items(fresh).empty() && solo_bonus_items(fresh).empty());
    // Episode 1: Campaign, Score Attack and Casual.
    MenuUnlocks one{false, {1}};
    assert(rescue_menu_open(one));
    auto rescue = rescue_menu_items(one);
    assert(has(rescue, "campaign") && has(rescue, "score_attack") && has(rescue, "casual") && !has(rescue, "time_attack"));
    assert(!has(solo_menu_items(one), "adventure"));
    // Episode 3: Time Attack; Episode 5: Adventure (episodes must all be done).
    assert(has(rescue_menu_items(MenuUnlocks{false, {1, 2, 3}}), "time_attack"));
    assert(!has(rescue_menu_items(MenuUnlocks{false, {1, 3}}), "time_attack"));
    assert(has(solo_menu_items(MenuUnlocks{false, {1, 2, 3, 4, 5}}), "adventure"));
    // Bonus and the other modes never show on their own yet (they come through Adventure).
    MenuUnlocks done{false, {1, 2, 3, 4, 5, 6, 7, 8}};
    assert(!has(solo_menu_items(done), "bonus") && solo_bonus_items(done).empty());
    // The main build: everything, each with its status; Bonus holds the modes and the Media Player.
    MenuUnlocks all{true, {}};
    solo = solo_menu_items(all);
    assert(has(solo, "story") && has(solo, "adventure") && has(solo, "bonus") && !has(solo, "lines"));
    auto bonus = solo_bonus_items(all);
    assert(has(bonus, "lines") && has(bonus, "puzzle") && has(bonus, "hide_and_seek") && has(bonus, "minefield") && !has(bonus, "media"));
    for (const auto& i : bonus) assert(!i.note.empty() && i.classes.rfind("rag-", 0) == 0);
    assert(has(local_menu_items(all), "bonus") && !has(local_bonus_items(all), "media"));
    assert(has(main_icon_items(all), "media") && !has(main_icon_items(fresh), "media"));
    assert(has(main_menu_items(all), "online") && has(main_icon_items(all), "trophies"));
    assert(rescue_menu_open(all) && has(rescue_menu_items(all), "time_attack"));
    return 0;
}
