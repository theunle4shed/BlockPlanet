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
        std::string note = character_label(profile.character);
        if (playing) note += note.empty() ? "playing now" : " \u00B7 playing now";
        items.push_back({"profile" + profile.id, profile.name, note, "command:profile:" + profile.id, true});
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
    items.push_back({"rename", "Rename", "", "command:profile_rename:" + id, true});
    items.push_back({"character", "Character", character_label(profile->character),
                     "command:profile_character:" + id, true});
    items.push_back({"delete", "Delete " + profile->name, "", "command:profile_delete:" + id, true});
    return items;
}

std::vector<blockplanet::ui::ListItem> delete_confirmation(const ProfileStore& store, const std::string& id) {
    std::vector<blockplanet::ui::ListItem> items;
    if (const auto* profile = profile_by_id(store, id))
        items.push_back({"delete", "Delete " + profile->name, "", "command:profile_delete_confirmed:" + id, true});
    return items;
}

const std::vector<Character>& characters() {
    static const std::vector<Character> list{{"rocket", "Rocket"}, {"gear", "Gear"},     {"gyro", "Gyro"},
                                             {"stomp", "Stomp"},   {"wheels", "Wheels"}, {"turbine", "Turbine"}};
    return list;
}

std::string character_label(const std::string& id) {
    for (const auto& character : characters())
        if (id == character.id) return character.label;
    return {};
}

const std::vector<std::string>& colour_names() {
    static const std::vector<std::string> names{"Original", "Crimson", "Amber", "Lime",
                                                "Teal",     "Sky",     "Violet", "Rose"};
    return names;
}

std::vector<blockplanet::ui::ListItem> character_choices(const ProfileStore& store, const std::string& id) {
    std::vector<blockplanet::ui::ListItem> items;
    const auto* profile = profile_by_id(store, id);
    if (profile == nullptr) return items;
    const std::string prefix = "command:profile_set_character:" + id + ":";
    items.push_back({"none", "None", profile->character.empty() ? "chosen" : "", prefix + "none", true});
    for (const auto& character : characters())
        items.push_back({character.id, character.label, profile->character == character.id ? "chosen" : "",
                         prefix + character.id, true});
    return items;
}

namespace {

constexpr std::size_t name_limit = 24;
// The on-screen keyboard: QWERTY with the numbers on top, ten to a row.
const std::string keys = "1234567890QWERTYUIOPASDFGHJKL'ZXCVBNM-._";

char shown(char key, bool shift) {
    return shift ? key : static_cast<char>(key >= 'A' && key <= 'Z' ? key - 'A' + 'a' : key);
}

} // namespace

void NameEditor::start(std::string target, std::string text) {
    target_ = std::move(target);
    text_ = std::move(text);
    shift_ = text_.empty();  // a new name starts with a capital
}

bool NameEditor::press(const std::string& key) {
    if (key == "shift") {
        shift_ = !shift_;
        return true;
    }
    if (key == "delete") {
        if (!text_.empty()) text_.pop_back();
        return false;
    }
    if (key == "space") {
        type(" ");
        return false;
    }
    if (key.size() == 1 && keys.find(key[0]) != std::string::npos) {
        const bool was_shift = shift_;
        type(std::string(1, shown(key[0], shift_)));
        // Like a phone keyboard: shift lasts one letter.
        if (was_shift && key[0] >= 'A' && key[0] <= 'Z') shift_ = false;
        return was_shift != shift_;
    }
    return false;
}

void NameEditor::type(const std::string& text) {
    for (char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u >= 0x7F || text_.size() >= name_limit) continue;
        if (c == ' ' && (text_.empty() || text_.back() == ' ')) continue;  // no leading or double spaces
        text_ += c;
    }
}

std::vector<blockplanet::ui::ListItem> NameEditor::keyboard() const {
    std::vector<blockplanet::ui::ListItem> items;
    for (char key : keys) {
        std::string id = "key";
        switch (key) {
        case '\'': id += "quote"; break;
        case '-': id += "dash"; break;
        case '.': id += "dot"; break;
        case '_': id += "under"; break;
        default: id += key;
        }
        items.push_back({id, std::string(1, shown(key, shift_)), "", "command:name_key:" + std::string(1, key), true});
    }
    // The bottom row, under the keys (spans add up to keyboard_columns).
    items.push_back({"shift", shift_ ? "SHIFT" : "Shift", "", "command:name_key:shift", true, 2});
    items.push_back({"keyspace", "Space", "", "command:name_key:space", true, 4});
    items.push_back({"delete", "Delete", "", "command:name_key:delete", true, 2});
    items.push_back({"done", "Done", "", "command:name_done", true, 2});
    return items;
}

std::string next_profile_name(const ProfileStore& store) {
    for (int number = 1;; ++number) {
        const auto name = "Player " + std::to_string(number);
        if (store.find(name) == nullptr) return name;
    }
}

} // namespace tetrisphere
