#include "tetrisphere/profile_menu.h"

namespace tetrisphere {

const Profile* profile_by_id(const ProfileStore& store, const std::string& id) {
    for (const auto& profile : store.profiles())
        if (profile.id == id) return &profile;
    return nullptr;
}

std::vector<blockplanet::ui::ListItem> profile_list(const ProfileStore& store, bool guest) {
    std::vector<blockplanet::ui::ListItem> items;
    const auto add = [&](const Profile& profile, bool playing) {
        items.push_back({"profile" + profile.id, profile.name, playing ? "playing now" : "",
                         "command:profile:" + profile.id, true});
    };
    if (!guest) add(store.active(), true);
    for (const auto& profile : store.profiles())
        if (guest || profile.id != store.active().id) add(profile, false);
    items.push_back({"new", "New profile", "", "command:profile_new", true});
    items.push_back({"guest", "Play as guest", guest ? "playing now" : "nothing is saved",
                     guest ? "close" : "command:profile_guest", true});
    return items;
}

std::vector<blockplanet::ui::ListItem> profile_actions(const ProfileStore& store, const std::string& id, bool guest) {
    std::vector<blockplanet::ui::ListItem> items;
    const auto* profile = profile_by_id(store, id);
    if (profile == nullptr) return items;
    if (!guest && profile->id == store.active().id)
        items.push_back({"play", "Keep playing as " + profile->name, "", "close", true});
    else
        items.push_back({"play", "Play as " + profile->name, "", "command:profile_play:" + id, true});
    items.push_back({"delete", "Delete " + profile->name, "", "command:profile_delete:" + id, true});
    return items;
}

std::vector<blockplanet::ui::ListItem> delete_confirmation(const ProfileStore& store, const std::string& id) {
    std::vector<blockplanet::ui::ListItem> items;
    if (const auto* profile = profile_by_id(store, id))
        items.push_back({"delete", "Delete " + profile->name, "", "command:profile_delete_confirmed:" + id, true});
    return items;
}

std::string next_profile_name(const ProfileStore& store) {
    for (int number = 1;; ++number) {
        const auto name = "Player " + std::to_string(number);
        if (store.find(name) == nullptr) return name;
    }
}

} // namespace tetrisphere
