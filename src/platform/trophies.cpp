#include "tetrisphere/trophies.h"

#include <json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace tetrisphere {
namespace {

using json = nlohmann::json;

bool parse_value(const json& node, TrophyValue& value) {
    if (node.is_number_integer()) { value = node.get<std::int64_t>(); return true; }
    if (node.is_string()) { value = node.get<std::string>(); return true; }
    return false;
}

// "where": {"size": {"at_least": 10}, "mode": "rescue"}
bool parse_conditions(const json& node, std::vector<TrophyCondition>& out,
                      std::string& error, const std::string& context) {
    if (node.is_null()) return true;
    if (!node.is_object()) { error = context + ": where must be an object"; return false; }
    for (const auto& [field, spec] : node.items()) {
        TrophyCondition condition;
        condition.field = field;
        if (spec.is_object()) {
            if (spec.size() != 1) {
                error = context + ": condition on " + field + " needs exactly one operator";
                return false;
            }
            const auto& [op, operand] = *spec.items().begin();
            if (op == "equals") condition.op = TrophyCondition::Op::Equal;
            else if (op == "not") condition.op = TrophyCondition::Op::NotEqual;
            else if (op == "at_least") condition.op = TrophyCondition::Op::AtLeast;
            else if (op == "at_most") condition.op = TrophyCondition::Op::AtMost;
            else { error = context + ": unknown operator " + op; return false; }
            if (!parse_value(operand, condition.value)) {
                error = context + ": condition value must be an integer or string";
                return false;
            }
            if ((condition.op == TrophyCondition::Op::AtLeast ||
                 condition.op == TrophyCondition::Op::AtMost) &&
                !std::holds_alternative<std::int64_t>(condition.value)) {
                error = context + ": at_least and at_most need integers";
                return false;
            }
        } else if (!parse_value(spec, condition.value)) {
            error = context + ": condition value must be an integer or string";
            return false;
        }
        out.push_back(std::move(condition));
    }
    return true;
}

bool matches(const GameEvent& event, const std::vector<TrophyCondition>& where) {
    for (const auto& condition : where) {
        const auto it = event.fields.find(condition.field);
        if (it == event.fields.end()) return false;
        const auto& actual = it->second;
        switch (condition.op) {
            case TrophyCondition::Op::Equal:
                if (actual != condition.value) return false;
                break;
            case TrophyCondition::Op::NotEqual:
                if (actual == condition.value) return false;
                break;
            case TrophyCondition::Op::AtLeast:
            case TrophyCondition::Op::AtMost: {
                if (!std::holds_alternative<std::int64_t>(actual)) return false;
                const auto a = std::get<std::int64_t>(actual);
                const auto b = std::get<std::int64_t>(condition.value);
                if (condition.op == TrophyCondition::Op::AtLeast ? a < b : a > b) return false;
                break;
            }
        }
    }
    return true;
}

bool valid_id(const std::string& id) {
    return !id.empty() && id.size() <= 64 &&
        std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        });
}

} // namespace

const char* trophy_tier_name(TrophyTier tier) {
    switch (tier) {
        case TrophyTier::Bronze: return "bronze";
        case TrophyTier::Silver: return "silver";
        case TrophyTier::Gold: return "gold";
        case TrophyTier::Platinum: return "platinum";
    }
    return "bronze";
}

bool TrophyEngine::load_definitions(const std::string& text, std::string& error) {
    std::vector<TrophyStatDefinition> stats;
    std::vector<TrophyDefinition> trophies;
    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception& e) {
        error = std::string("trophy definitions are not valid JSON: ") + e.what();
        return false;
    }
    if (!root.is_object() || root.value("schema_version", 0) != 1) {
        error = "trophy definitions need schema_version 1";
        return false;
    }
    std::set<std::string> stat_ids;
    for (const auto& node : root.value("stats", json::array())) {
        TrophyStatDefinition stat;
        stat.id = node.value("id", "");
        stat.event = node.value("event", "");
        const std::string context = "stat " + stat.id;
        if (!valid_id(stat.id) || stat.event.empty()) {
            error = context + ": needs an id (a-z, 0-9, _) and an event";
            return false;
        }
        if (!stat_ids.insert(stat.id).second) { error = context + ": duplicate id"; return false; }
        if (!parse_conditions(node.value("where", json()), stat.where, error, context)) return false;
        stat.add = node.value("add", std::int64_t{1});
        stat.add_field = node.value("add_field", "");
        const std::string combine = node.value("combine", "sum");
        if (combine == "max") stat.keep_max = true;
        else if (combine != "sum") { error = context + ": combine must be sum or max"; return false; }
        stats.push_back(std::move(stat));
    }
    std::set<std::string> trophy_ids;
    for (const auto& node : root.value("trophies", json::array())) {
        TrophyDefinition trophy;
        trophy.id = node.value("id", "");
        const std::string context = "trophy " + trophy.id;
        trophy.name = node.value("name", "");
        trophy.description = node.value("description", "");
        if (!valid_id(trophy.id) || trophy.name.empty()) {
            error = context + ": needs an id (a-z, 0-9, _) and a name";
            return false;
        }
        if (!trophy_ids.insert(trophy.id).second) { error = context + ": duplicate id"; return false; }
        const std::string tier = node.value("tier", "bronze");
        if (tier == "bronze") trophy.tier = TrophyTier::Bronze;
        else if (tier == "silver") trophy.tier = TrophyTier::Silver;
        else if (tier == "gold") trophy.tier = TrophyTier::Gold;
        else if (tier == "platinum") trophy.tier = TrophyTier::Platinum;
        else { error = context + ": unknown tier " + tier; return false; }
        trophy.hidden = node.value("hidden", false);
        const auto trigger = node.value("trigger", json::object());
        if (trigger.contains("event")) {
            trophy.trigger.kind = TrophyTrigger::Kind::Event;
            trophy.trigger.event = trigger.value("event", "");
            if (!parse_conditions(trigger.value("where", json()), trophy.trigger.where,
                                  error, context)) return false;
        } else if (trigger.contains("stat")) {
            trophy.trigger.kind = TrophyTrigger::Kind::StatAtLeast;
            trophy.trigger.stat = trigger.value("stat", "");
            trophy.trigger.target = trigger.value("at_least", std::int64_t{0});
            if (!stat_ids.count(trophy.trigger.stat) || trophy.trigger.target <= 0) {
                error = context + ": stat trigger needs a defined stat and at_least > 0";
                return false;
            }
        } else if (trigger.contains("all_of")) {
            trophy.trigger.kind = TrophyTrigger::Kind::AllOf;
            for (const auto& id : trigger["all_of"]) trophy.trigger.trophies.push_back(id.get<std::string>());
        } else {
            error = context + ": trigger needs event, stat or all_of";
            return false;
        }
        trophies.push_back(std::move(trophy));
    }
    for (const auto& trophy : trophies) {
        for (const auto& id : trophy.trigger.trophies) {
            if (!trophy_ids.count(id) || id == trophy.id) {
                error = "trophy " + trophy.id + ": all_of names an unknown trophy " + id;
                return false;
            }
        }
    }
    const std::lock_guard lock(mutex_);
    stats_ = std::move(stats);
    trophies_ = std::move(trophies);
    error.clear();
    return true;
}

std::optional<TrophyDefinition> TrophyEngine::find(const std::string& id) const {
    const std::lock_guard lock(mutex_);
    for (const auto& trophy : trophies_) if (trophy.id == id) return trophy;
    return std::nullopt;
}

void TrophyEngine::set_progress(TrophyProgress progress) {
    const std::lock_guard lock(mutex_);
    progress_ = std::move(progress);
}

TrophyProgress TrophyEngine::progress() const {
    const std::lock_guard lock(mutex_);
    return progress_;
}

void TrophyEngine::set_awards_enabled(bool enabled) {
    const std::lock_guard lock(mutex_);
    awards_enabled_ = enabled;
}

bool TrophyEngine::awards_enabled() const {
    const std::lock_guard lock(mutex_);
    return awards_enabled_;
}

void TrophyEngine::add_sink(TrophySink* sink) {
    const std::lock_guard lock(mutex_);
    if (sink != nullptr) sinks_.push_back(sink);
}

bool TrophyEngine::unlock_locked(const TrophyDefinition& trophy, std::int64_t now,
                                 std::vector<std::string>& unlocked) {
    if (progress_.unlocked.count(trophy.id)) return false;
    progress_.unlocked[trophy.id] = now;
    unlocked.push_back(trophy.id);
    return true;
}

std::vector<std::string> TrophyEngine::post(const GameEvent& event, std::int64_t now) {
    std::vector<std::string> unlocked;
    std::vector<TrophyDefinition> notify;
    std::vector<TrophySink*> sinks;
    {
        const std::lock_guard lock(mutex_);
        if (!awards_enabled_) return unlocked;
        for (const auto& stat : stats_) {
            if (stat.event != event.name || !matches(event, stat.where)) continue;
            std::int64_t amount = stat.add;
            if (!stat.add_field.empty()) {
                const auto it = event.fields.find(stat.add_field);
                if (it == event.fields.end() || !std::holds_alternative<std::int64_t>(it->second)) continue;
                amount = std::get<std::int64_t>(it->second);
            }
            auto& value = progress_.stats[stat.id];
            value = stat.keep_max ? std::max(value, amount) : value + amount;
        }
        for (const auto& trophy : trophies_) {
            const auto& trigger = trophy.trigger;
            if (trigger.kind == TrophyTrigger::Kind::Event && trigger.event == event.name &&
                matches(event, trigger.where)) {
                unlock_locked(trophy, now, unlocked);
            } else if (trigger.kind == TrophyTrigger::Kind::StatAtLeast) {
                const auto it = progress_.stats.find(trigger.stat);
                if (it != progress_.stats.end() && it->second >= trigger.target)
                    unlock_locked(trophy, now, unlocked);
            }
        }
        // Meta trophies can complete in a chain, so repeat until nothing changes.
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& trophy : trophies_) {
                if (trophy.trigger.kind != TrophyTrigger::Kind::AllOf ||
                    progress_.unlocked.count(trophy.id)) continue;
                bool complete = true;
                if (trophy.trigger.trophies.empty()) {
                    for (const auto& other : trophies_)
                        if (other.id != trophy.id && !progress_.unlocked.count(other.id)) complete = false;
                } else {
                    for (const auto& id : trophy.trigger.trophies)
                        if (!progress_.unlocked.count(id)) complete = false;
                }
                if (complete) changed |= unlock_locked(trophy, now, unlocked);
            }
        }
        for (const auto& id : unlocked)
            for (const auto& trophy : trophies_) if (trophy.id == id) notify.push_back(trophy);
        sinks = sinks_;
    }
    for (const auto& trophy : notify)
        for (auto* sink : sinks) sink->trophy_unlocked(trophy);
    return unlocked;
}

std::string trophy_progress_to_json(const TrophyProgress& progress) {
    json root{{"schema_version", 1}, {"unlocked", json::object()}, {"stats", json::object()}};
    for (const auto& [id, time] : progress.unlocked) root["unlocked"][id] = time;
    for (const auto& [id, value] : progress.stats) root["stats"][id] = value;
    return root.dump(2);
}

bool trophy_progress_from_json(const std::string& text, TrophyProgress& progress,
                               std::string& error) {
    try {
        const auto root = json::parse(text);
        if (root.value("schema_version", 0) != 1) { error = "unsupported trophy progress version"; return false; }
        TrophyProgress result;
        for (const auto& [id, time] : root.at("unlocked").items()) result.unlocked[id] = time.get<std::int64_t>();
        for (const auto& [id, value] : root.at("stats").items()) result.stats[id] = value.get<std::int64_t>();
        progress = std::move(result);
        error.clear();
        return true;
    } catch (const json::exception& e) {
        error = std::string("invalid trophy progress: ") + e.what();
        return false;
    }
}

TrophyStore::TrophyStore(std::filesystem::path path) : path_(std::move(path)) {}

namespace {
bool read_text(const std::filesystem::path& path, std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    text = buffer.str();
    return static_cast<bool>(input) || input.eof();
}
} // namespace

bool TrophyStore::load(TrophyProgress& progress, std::string& error) const {
    error.clear();
    const auto backup = std::filesystem::path(path_.string() + ".bak");
    std::string text, ignored;
    if (read_text(path_, text) && trophy_progress_from_json(text, progress, ignored)) return true;
    if (read_text(backup, text) && trophy_progress_from_json(text, progress, ignored)) return true;
    std::error_code ec;
    if (std::filesystem::exists(path_, ec) || std::filesystem::exists(backup, ec)) {
        error = "trophy progress and its backup are unreadable";
        return false;
    }
    progress = TrophyProgress{};
    return true;
}

bool TrophyStore::save(const TrophyProgress& progress, std::string& error) const {
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) { error = "cannot create trophy directory: " + ec.message(); return false; }
    const auto temporary = std::filesystem::path(path_.string() + ".tmp");
    const auto backup = std::filesystem::path(path_.string() + ".bak");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << trophy_progress_to_json(progress);
        output.flush();
        if (!output) { error = "cannot write trophy progress"; return false; }
    }
    if (std::filesystem::exists(path_, ec)) {
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(path_, backup, ec);
        if (ec) { error = "cannot keep trophy backup: " + ec.message(); return false; }
    }
    std::filesystem::rename(temporary, path_, ec);
    if (ec) { error = "cannot commit trophy progress: " + ec.message(); return false; }
    return true;
}

} // namespace tetrisphere
