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
    // From the start: Solo, Training, Profiles, Options, Credits, Exit; Solo is Rescue alone.
    auto main_items = main_menu_items(fresh);
    assert(has(main_items, "solo") && has(main_items, "training") && has(main_items, "options") && has(main_items, "profiles"));
    assert(!has(main_items, "local") && !has(main_items, "online") && !has(main_items, "media") && !has(main_items, "trophies"));
    auto solo = solo_menu_items(fresh);
    assert(solo.size() == 1 && solo[0].id == "story" && solo[0].note.empty());
    assert(!rescue_menu_open(fresh) && rescue_menu_items(fresh).empty());
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
    // The other modes never show on their own (they come through Adventure).
    assert(!has(solo_menu_items(MenuUnlocks{false, {1, 2, 3, 4, 5, 6, 7, 8}}), "lines"));
    // The main build: everything, each with its status.
    MenuUnlocks all{true, {}};
    solo = solo_menu_items(all);
    assert(has(solo, "lines") && has(solo, "puzzle") && has(solo, "bonus") && has(solo, "minefield"));
    for (const auto& i : solo) assert(!i.note.empty() && i.classes.rfind("rag-", 0) == 0);
    assert(has(main_menu_items(all), "online") && rescue_menu_open(all) && has(rescue_menu_items(all), "time_attack"));
    return 0;
}
