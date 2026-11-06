#include "tetrisphere/developer_records.h"

#include <json.hpp>

#include <fstream>
#include <sstream>

namespace tetrisphere {

double DeveloperRecords::value(const std::string& mode, int chapter, int stage) const {
    const auto m = modes.find(mode);
    if (m == modes.end()) return 0.0;
    const auto c = m->second.find(chapter);
    if (c == m->second.end()) return 0.0;
    const auto i = std::size_t(stage - 1);
    return stage >= 1 && i < c->second.size() ? c->second[i] : 0.0;
}

bool DeveloperRecords::beats(const std::string& mode, double mine, double record) {
    if (mine <= 0.0 || record <= 0.0) return false;
    return mode == "time_attack" ? mine <= record : mine >= record;
}

bool DeveloperRecords::record(const std::string& mode, int chapter, int stage, double v) {
    if (v <= 0.0 || chapter < 1 || chapter > chapters || stage < 1 || stage > stages) return false;
    auto& list = modes[mode][chapter];
    if (list.size() < std::size_t(stages)) list.resize(std::size_t(stages), 0.0);
    double& slot = list[std::size_t(stage - 1)];
    if (slot > 0.0 && !(mode == "time_attack" ? v < slot : v > slot)) return false;
    slot = v;
    return true;
}

bool DeveloperRecords::all_beaten(const std::string& mode, const std::map<int, std::vector<double>>& bests) const {
    for (int chapter = 1; chapter <= chapters; ++chapter) {
        const auto b = bests.find(chapter);
        for (int stage = 1; stage <= stages; ++stage) {
            const double record = value(mode, chapter, stage);
            if (record <= 0.0) return false;  // not all recorded yet
            const auto i = std::size_t(stage - 1);
            const double mine = b != bests.end() && i < b->second.size() ? b->second[i] : 0.0;
            if (!beats(mode, mine, record)) return false;
        }
    }
    return true;
}

bool parse_developer_records(const std::string& text, DeveloperRecords& out, std::string& error) {
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        error = "developer records: not a JSON object";
        return false;
    }
    DeveloperRecords records;
    for (const char* mode : {"time_attack", "score_attack"}) {
        const auto m = json.find(mode);
        if (m == json.end()) continue;
        if (!m->is_object()) {
            error = std::string("developer records: ") + mode + " is not an object";
            return false;
        }
        for (const auto& [key, list] : m->items()) {
            int chapter = 0;
            try { chapter = std::stoi(key); } catch (...) { chapter = 0; }
            if (chapter < 1 || chapter > DeveloperRecords::chapters || !list.is_array()) {
                error = std::string("developer records: bad chapter ") + key + " in " + mode;
                return false;
            }
            auto& values = records.modes[mode][chapter];
            for (const auto& v : list) values.push_back(v.is_number() ? v.get<double>() : 0.0);
            values.resize(std::size_t(DeveloperRecords::stages), 0.0);
        }
    }
    out = std::move(records);
    return true;
}

std::string developer_records_json(const DeveloperRecords& records) {
    nlohmann::ordered_json json;
    json["schema_version"] = 1;
    for (const char* mode : {"time_attack", "score_attack"}) {
        nlohmann::ordered_json m = nlohmann::ordered_json::object();
        if (const auto it = records.modes.find(mode); it != records.modes.end())
            for (const auto& [chapter, values] : it->second) m[std::to_string(chapter)] = values;
        json[mode] = m;
    }
    return json.dump(2) + "\n";
}

bool load_developer_records(const std::filesystem::path& path, DeveloperRecords& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "developer records: cannot open " + path.string();
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse_developer_records(text.str(), out, error);
}

bool save_developer_records(const std::filesystem::path& path, const DeveloperRecords& records, std::string& error) {
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out || !(out << developer_records_json(records))) {
            error = "developer records: cannot write " + temporary;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "developer records: " + ec.message();
        return false;
    }
    return true;
}

} // namespace tetrisphere
