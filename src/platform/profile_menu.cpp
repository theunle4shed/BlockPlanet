#include <cstdio>
#include "tetrisphere/profile_menu.h"

#include <algorithm>
#include <cctype>

namespace tetrisphere {

const Profile* profile_by_id(const ProfileStore& store, const std::string& id) {
    for (const auto& profile : store.profiles())
        if (profile.id == id) return &profile;
    return nullptr;
}

std::vector<blockplanet::ui::ListItem> profile_list(const ProfileStore& store, bool guest, bool picker) {
    std::vector<blockplanet::ui::ListItem> items;
    const auto add = [&](const Profile& profile, bool playing) {
        std::string note = character_label(profile.character);
        if (playing && !picker) note += note.empty() ? "playing now" : " \u00B7 playing now";
        const std::string action = !picker ? "command:profile:" + profile.id
                                   : playing ? "command:profile_picked" : "command:profile_play:" + profile.id;
        items.push_back({"profile" + profile.id, profile.name, note, action, true});
    };
    if (!guest) add(store.active(), true);
    for (const auto& profile : store.profiles())
        if (guest || profile.id != store.active().id) add(profile, false);
    items.push_back({"new", "New profile", "", "command:profile_new", true});
    items.push_back({"guest", "Play as guest", guest && !picker ? "playing now" : "nothing is saved",
                     guest ? (picker ? "command:profile_picked" : "close") : "command:profile_guest", true});
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
    if (const auto* profile = profile_by_id(store, id)) {
        items.push_back({"delete", "Delete " + profile->name, "", "command:profile_delete_confirmed:" + id, true});
        items.push_back({"keep", "Keep it", "", "back", true, 1, true});  // the safe choice is selected
    }
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

std::vector<blockplanet::ui::ListItem> story_chapters(const std::set<int>& completed, int chapters, bool all_open) {
    std::vector<blockplanet::ui::ListItem> items;
    int last = 1;  // the next chapter to play
    for (int chapter : completed)
        if (chapter >= 1) last = std::max(last, chapter + 1);
    last = all_open ? chapters : std::min(last, chapters);  // all_open: --unlock-all (testing)
    bool focused = false;
    for (int chapter = 1; chapter <= last; ++chapter) {
        const bool done = completed.count(chapter) != 0;
        const bool next = !done && !focused;
        items.push_back({"chapter" + std::to_string(chapter), "Chapter " + std::to_string(chapter),
                         done ? "completed" : next ? "next" : "",
                         "command:stages:" + std::to_string(chapter), true, 1, next});
        focused = focused || next;
    }
    return items;
}

std::vector<blockplanet::ui::ListItem> story_stages(int chapter, const std::set<int>& levels_done,
                                                   const std::vector<double>& best_levels, int levels, bool all_open) {
    std::vector<blockplanet::ui::ListItem> items;
    const auto c = std::to_string(chapter);
    items.push_back({"episode", "Whole episode", "", "command:chapter:" + c, true, 1, true});
    int last = 1;
    for (int level : levels_done)
        if (level >= 1) last = std::max(last, level + 1);
    last = all_open ? levels : std::min(last, levels);
    for (int level = 1; level <= last; ++level) {
        const auto i = std::size_t(level - 1);
        std::string note;
        if (i < best_levels.size() && best_levels[i] > 0.0) {
            const int t = static_cast<int>(best_levels[i]);
            char text[32];
            std::snprintf(text, sizeof text, "best %d:%02d", t / 60, t % 60);
            note = text;
        } else if (levels_done.count(level) == 0 && level == last) {
            note = "next";  // only the furthest stage open (others may simply not be played yet)
        }
        items.push_back({"stage" + std::to_string(level), "Stage " + c + ":" + std::to_string(level), note,
                         "command:stage:" + c + ":" + std::to_string(level), true, 1, false});
    }
    return items;
}

std::vector<blockplanet::ui::ListItem> story_training(bool basics_done, bool advanced_done) {
    blockplanet::ui::ListItem basics{"basics", "Basics", basics_done ? "completed" : "start here",
                                     "command:training", true, 1, !basics_done || advanced_done};
    basics.description = "Wheels shows you how to play: the shadow, sliding, dropping, combos and magic.";
    blockplanet::ui::ListItem advanced{"advanced", "Advanced", advanced_done ? "completed" : "",
                                       "command:training_advanced", true, 1, basics_done && !advanced_done};
    advanced.description = "Crystal pieces, the X count, gravity combos and fuse combos.";
    return {basics, advanced};
}

std::string next_profile_name(const ProfileStore& store) {
    for (int number = 1;; ++number) {
        const auto name = "Player " + std::to_string(number);
        if (store.find(name) == nullptr) return name;
    }
}

std::vector<blockplanet::ui::ListItem> puzzle_list(const std::set<int>& solved, const std::vector<double>& best_times,
                                                   const std::vector<double>& best_moves, int count) {
    std::vector<blockplanet::ui::ListItem> items;
    int last = 1;
    for (int n : solved)
        if (n >= 1) last = std::max(last, n + 1);
    last = std::min(last, count);
    for (int n = 1; n <= last; ++n) {
        const auto i = std::size_t(n - 1);
        std::string note;
        if (i < best_times.size() && best_times[i] > 0.0) {
            const int t = static_cast<int>(best_times[i]);
            char text[48];
            std::snprintf(text, sizeof text, "best %d:%02d", t / 60, t % 60);
            note = text;
            if (i < best_moves.size() && best_moves[i] > 0.0)
                note += " \u00B7 " + std::to_string(int(best_moves[i])) + (best_moves[i] == 1.0 ? " move" : " moves");
        } else if (solved.count(n) != 0) {
            note = "solved";
        } else if (n == last) {
            note = "next";
        }
        items.push_back({"puzzle" + std::to_string(n), "Puzzle " + std::to_string(n), note,
                         "command:puzzle:" + std::to_string(n), true, 1, n == last});
    }
    return items;
}

std::string fix_wording(const std::string& text) {
    // The owner's choices (docs/DECISIONS-2026-11-07.md): "sphere", not "ball";
    // spelling and grammar fixed; single ! and ?. On the game's capitals, codes kept.
    std::string s = text;
    const auto word_char = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; };
    const auto replace_word = [&](const std::string& from, const std::string& to) {
        for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos;) {
            const bool starts = at == 0 || !word_char(s[at - 1]) || (at >= 2 && s[at - 2] == '-' && std::isdigit(static_cast<unsigned char>(s[at - 1])));
            const std::size_t after = at + from.size();
            if (starts && (after >= s.size() || !word_char(s[after]))) {
                s.replace(at, from.size(), to);
                at += to.size();
            } else {
                at = after;
            }
        }
    };
    replace_word("BALL", "SPHERE");
    replace_word("PIECES", "BRICKS");  // the owner's word for them
    replace_word("PIECE", "BRICK");
    replace_word("TALLYS", "TALLIES");
    replace_word("CAN NOT", "CANNOT");
    replace_word("WITH LESS THAN", "WITH FEWER THAN");
    for (const char* doubled : {"??", "!!"})
        for (std::size_t at; (at = s.find(doubled)) != std::string::npos;) s.erase(at, 1);
    return s;
}

std::string game_quotes(const std::string& text) {
    // The game's font draws % as its apostrophe and quote mark. Opening when
    // it starts a word, otherwise closing (the apostrophe).
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') { out += text[i]; continue; }
        const bool opens = (i == 0 || text[i - 1] == ' ') && i + 1 < text.size() && text[i + 1] != ' ';
        out += opens ? "\u2018" : "\u2019";
    }
    // No space before ! or ? (the original's typewriter spacing).
    std::string tidy;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == ' ') {
            std::size_t j = i;
            while (j < out.size() && out[j] == ' ') ++j;
            if (j < out.size() && (out[j] == '!' || out[j] == '?') && !tidy.empty() && tidy.back() != ' ') { i = j - 1; continue; }
        }
        tidy += out[i];
    }
    return tidy;
}

std::string proper_case(const std::string& text) {
    bool has_upper = false, in_bracket = false;
    for (char c : text) {
        if (c == '[') in_bracket = true;
        if (c == ']') in_bracket = false;
        if (in_bracket) continue;  // button codes do not count
        if (c >= 'a' && c <= 'z') return text;
        if (c >= 'A' && c <= 'Z') has_upper = true;
    }
    if (!has_upper) return text;
    std::string spaced;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            if (!spaced.empty() && spaced.back() != ' ') spaced += ' ';
            spaced += '&';
            if (i + 1 < text.size() && text[i + 1] != ' ') spaced += ' ';
        } else {
            spaced += text[i];
        }
    }
    std::vector<std::string> words;
    std::string word;
    for (char c : spaced) {
        if (c == ' ') { words.push_back(word); word.clear(); }
        else word += c;
    }
    words.push_back(word);
    int letters_words = 0;
    for (const auto& w : words)
        if (!w.empty()) ++letters_words;
    const bool title = letters_words <= 3;
    static const char* keep[] = {"VS", "CPU", "HUD", "TV", "OK", "N64", "H2O", "TM", "X", "I"};
    // Names keep their capital wherever they fall.
    static const char* names[] = {"WHEELS", "TURBINE", "TETRISPHERE", "NINTENDO", "STORY", "TRAINING"};  // names, and menu names
    std::string out;
    bool sentence_start = true;
    for (std::size_t k = 0; k < words.size(); ++k) {
        if (k > 0) out += ' ';
        const std::string& w = words[k];
        bool kept = false;
        std::string bare;
        for (char c : w)
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) bare += c;
        for (const char* a : keep)
            if (bare == a) kept = true;
        // "I", also as "I'M" / "I'LL" (letters before an apostrophe).
        std::string head;
        for (char c : w) {
            if (c >= 'A' && c <= 'Z') head += c;
            else if (c == '\'' || c == '\xE2') break;
            else if (!head.empty()) break;
        }
        bool name = head == "I" && bare.size() <= 3 && w.find_first_of("'\xE2") != std::string::npos;
        for (const char* a : names)
            if (bare == a) name = true;
        bool first_letter = true;
        bool bracket = false;
        for (char c : w) {
            if (c == '[') bracket = true;
            if (bracket) {  // a button name, as given
                out += c;
                if (c == ']') bracket = false;
                first_letter = false;
                sentence_start = false;
                continue;
            }
            if (c >= 'A' && c <= 'Z') {
                const bool upper = kept || (first_letter && (title || sentence_start || name));
                out += upper ? c : char(c - 'A' + 'a');
                first_letter = false;
                sentence_start = false;
            } else {
                out += c;
                if (c == '.' || c == '!' || c == '?') sentence_start = true;
            }
        }
    }
    return out;
}

} // namespace tetrisphere
