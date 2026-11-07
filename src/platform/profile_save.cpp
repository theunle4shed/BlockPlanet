#include "tetrisphere/profile_save.h"

#include <json.hpp>

#include <fstream>
#include <sstream>
#include <variant>

namespace tetrisphere {
namespace {

constexpr int schema_version = 1;

bool valid_family(const std::string& value) {
    return value == "auto" || value == "keyboard" || value == "xbox" || value == "playstation" ||
           value == "nintendo-switch";
}

std::int64_t number(const GameEvent& event, const char* key, std::int64_t fallback = 0) {
    const auto it = event.fields.find(key);
    if (it == event.fields.end() || !std::holds_alternative<std::int64_t>(it->second)) return fallback;
    return std::get<std::int64_t>(it->second);
}

std::string text(const GameEvent& event, const char* key) {
    const auto it = event.fields.find(key);
    if (it == event.fields.end() || !std::holds_alternative<std::string>(it->second)) return {};
    return std::get<std::string>(it->second);
}

nlohmann::json to_json(const ProfileSaveData& data) {
    nlohmann::json preferences = nlohmann::json::object();
    if (data.preferences.controls) {
        const auto& c = *data.preferences.controls;
        preferences["controls"] = {
            {"family_override", c.family_override},
            {"bindings", {static_cast<int>(c.bindings.confirm), static_cast<int>(c.bindings.cancel),
                          static_cast<int>(c.bindings.face_left), static_cast<int>(c.bindings.face_up)}},
            {"magic", static_cast<int>(c.magic)}};
    }
    if (data.preferences.audio) {
        const auto& a = *data.preferences.audio;
        preferences["audio"] = {{"music", a.music}, {"effects", a.effects},
                                {"random_music", a.random_music}};
    }
    if (data.preferences.hud_distance) preferences["hud"] = {{"distance", *data.preferences.hud_distance}};
    nlohmann::json levels = nlohmann::json::object();
    for (const auto& [mode, chapters] : data.progress.levels_completed) {
        nlohmann::json by_chapter = nlohmann::json::object();
        for (const auto& [chapter, done] : chapters) by_chapter[std::to_string(chapter)] = done;
        levels[mode] = by_chapter;
    }
    nlohmann::json chapters = nlohmann::json::object();
    for (const auto& [mode, done] : data.progress.chapters_completed) chapters[mode] = done;
    nlohmann::json records = nlohmann::json::object();
    for (const auto& [mode, chapters] : data.progress.episode_records) {
        nlohmann::json by_chapter = nlohmann::json::object();
        for (const auto& [chapter, r] : chapters)
            by_chapter[std::to_string(chapter)] = {{"best_levels", r.best_levels}, {"best_total", r.best_total}};
        records[mode] = by_chapter;
    }
    const auto& s = data.progress.statistics;
    return {{"schema_version", schema_version},
            {"preferences", preferences},
            {"progress",
             {{"levels_completed", levels},
              {"chapters_completed", chapters},
              {"episode_records", records},
              {"score_attack", {{"best_score", data.progress.score_attack_best_score},
                               {"best_seconds", data.progress.score_attack_best_seconds},
                               {"best_stages", data.progress.score_attack_best_stages}}},
              {"puzzles_solved", data.progress.puzzles_solved},
              {"last_play", {{"submode", data.progress.last_play.submode},
                             {"chapter", data.progress.last_play.chapter},
                             {"level", data.progress.last_play.level},
                             {"score", data.progress.last_play.score},
                             {"hearts", data.progress.last_play.hearts},
                             {"magic", data.progress.last_play.magic}}},
              {"statistics",
               {{"games_started", s.games_started},
                {"games_over", s.games_over},
                {"pieces_placed", s.pieces_placed},
                {"lives_lost", s.lives_lost},
                {"levels_completed", s.levels_completed},
                {"chapters_completed", s.chapters_completed},
                {"puzzles_solved", s.puzzles_solved},
                {"vs_rounds_won", s.vs_rounds_won},
                {"vs_rounds_lost", s.vs_rounds_lost},
                {"seconds_played", s.seconds_played}}}}}};
}

bool from_json(const std::string& text, ProfileSaveData& data) {
    try {
        const auto document = nlohmann::json::parse(text);
        if (document.at("schema_version").get<int>() > schema_version) return false;
        ProfileSaveData result;
        // value() returns copies; keep them in variables that outlive the loops.
        const nlohmann::json preferences = document.value("preferences", nlohmann::json::object());
        if (preferences.contains("controls")) {
            const auto& c = preferences.at("controls");
            ControlPreferences controls;
            controls.family_override = c.at("family_override").get<std::string>();
            const auto buttons = c.at("bindings").get<std::array<int, 4>>();
            const int magic = c.at("magic").get<int>();
            if (!valid_family(controls.family_override) || magic < 0 || magic > 7) return false;
            for (int b : buttons) {
                if (b < 0 || b > 3) return false;
            }
            controls.bindings = {static_cast<PhysicalButton>(buttons[0]), static_cast<PhysicalButton>(buttons[1]),
                                 static_cast<PhysicalButton>(buttons[2]), static_cast<PhysicalButton>(buttons[3])};
            controls.magic = static_cast<MagicControl>(magic);
            result.preferences.controls = controls;
        }
        if (preferences.contains("audio")) {
            const auto& a = preferences.at("audio");
            AudioPreferences audio;
            audio.music = a.value("music", 100);
            audio.effects = a.value("effects", 100);
            audio.random_music = a.value("random_music", false);
            const auto percent = [](int v) { return v >= 0 && v <= 100; };
            if (!percent(audio.music) || !percent(audio.effects)) return false;
            result.preferences.audio = audio;
        }
        if (preferences.contains("hud")) {
            const auto distance = preferences.at("hud").value("distance", std::string("near"));
            if (distance != "near" && distance != "edges" && distance != "follow") return false;
            result.preferences.hud_distance = distance;
        }
        const nlohmann::json progress = document.value("progress", nlohmann::json::object());
        const nlohmann::json levels = progress.value("levels_completed", nlohmann::json::object());
        for (const auto& [mode, chapters] : levels.items()) {
            for (const auto& [chapter, done] : chapters.items())
                result.progress.levels_completed[mode][std::stoi(chapter)] = done.get<std::set<int>>();
        }
        const nlohmann::json chapters_done = progress.value("chapters_completed", nlohmann::json::object());
        for (const auto& [mode, done] : chapters_done.items())
            result.progress.chapters_completed[mode] = done.get<std::set<int>>();
        result.progress.puzzles_solved = progress.value("puzzles_solved", std::set<int>{});
        const nlohmann::json attack = progress.value("score_attack", nlohmann::json::object());
        result.progress.score_attack_best_score = attack.value("best_score", std::int64_t{0});
        result.progress.score_attack_best_seconds = attack.value("best_seconds", 0.0);
        result.progress.score_attack_best_stages = attack.value("best_stages", std::vector<std::int64_t>{});
        const nlohmann::json last = progress.value("last_play", nlohmann::json::object());
        result.progress.last_play.submode = last.value("submode", std::string{});
        result.progress.last_play.chapter = last.value("chapter", 0);
        result.progress.last_play.level = last.value("level", 0);
        result.progress.last_play.score = last.value("score", std::int64_t{0});
        result.progress.last_play.hearts = last.value("hearts", 3);
        result.progress.last_play.magic = last.value("magic", -1);
        const nlohmann::json records = progress.value("episode_records", nlohmann::json::object());
        for (const auto& [mode, chapters] : records.items()) {
            for (const auto& [chapter, r] : chapters.items()) {
                ProfileProgress::EpisodeRecord record;
                record.best_levels = r.value("best_levels", std::vector<double>{});
                record.best_total = r.value("best_total", 0.0);
                result.progress.episode_records[mode][std::stoi(chapter)] = record;
            }
        }
        const nlohmann::json st = progress.value("statistics", nlohmann::json::object());
        auto& s = result.progress.statistics;
        s.games_started = st.value("games_started", std::int64_t{0});
        s.games_over = st.value("games_over", std::int64_t{0});
        s.pieces_placed = st.value("pieces_placed", std::int64_t{0});
        s.lives_lost = st.value("lives_lost", std::int64_t{0});
        s.levels_completed = st.value("levels_completed", std::int64_t{0});
        s.chapters_completed = st.value("chapters_completed", std::int64_t{0});
        s.puzzles_solved = st.value("puzzles_solved", std::int64_t{0});
        s.vs_rounds_won = st.value("vs_rounds_won", std::int64_t{0});
        s.vs_rounds_lost = st.value("vs_rounds_lost", std::int64_t{0});
        s.seconds_played = st.value("seconds_played", std::int64_t{0});
        data = std::move(result);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool read_file(const std::filesystem::path& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

} // namespace

bool record_game_event(ProfileProgress& progress, const GameEvent& event) {
    auto& s = progress.statistics;
    const std::string mode = text(event, "mode");
    // Training's lessons take lives and play demos on purpose; they are not
    // play, so they do not count in the statistics.
    if (mode == "training") return false;
    if (event.name == "mode_started") {
        ++s.games_started;
    } else if (event.name == "game_over") {
        ++s.games_over;
    } else if (event.name == "piece_placed") {
        ++s.pieces_placed;
    } else if (event.name == "life_lost") {
        ++s.lives_lost;
    } else if (event.name == "vs_round") {
        const auto result = text(event, "result");
        if (result == "win") ++s.vs_rounds_won;
        if (result == "loss") ++s.vs_rounds_lost;
    } else if (event.name == "level_completed") {
        ++s.levels_completed;
        if (!mode.empty())
            progress.levels_completed[mode][static_cast<int>(number(event, "chapter"))].insert(
                static_cast<int>(number(event, "level")));
        return true;
    } else if (event.name == "chapter_completed") {
        ++s.chapters_completed;
        if (!mode.empty()) progress.chapters_completed[mode].insert(static_cast<int>(number(event, "chapter")));
        return true;
    } else if (event.name == "training_completed") {
        // Story's Chapter 0 is Training Basics.
        if (text(event, "lesson") == "basics") progress.chapters_completed["rescue"].insert(0);
        // Puzzle's Chapter 0 is Training Puzzle.
        if (text(event, "lesson") == "puzzle") progress.chapters_completed["puzzle"].insert(0);
        // Training Advanced has no chapter of its own.
        if (text(event, "lesson") == "advanced") progress.chapters_completed["training_advanced"].insert(0);
        return true;
    } else if (event.name == "puzzle_solved") {
        ++s.puzzles_solved;
        progress.puzzles_solved.insert(static_cast<int>(number(event, "puzzle")));
        return true;
    }
    return false;
}

ProfileSave::ProfileSave(std::filesystem::path path) : path_(std::move(path)) {}

bool ProfileSave::load(ProfileSaveData& data, std::string& error) const {
    error.clear();
    std::string content;
    const auto backup = std::filesystem::path(path_.string() + ".bak");
    if (read_file(path_, content) && from_json(content, data)) return true;
    if (read_file(backup, content) && from_json(content, data)) return true;
    std::error_code code;
    if (std::filesystem::exists(path_, code) || std::filesystem::exists(backup, code)) {
        error = "the profile save and its backup are damaged: " + path_.string();
        return false;
    }
    data = ProfileSaveData{};
    return true;
}

bool ProfileSave::save(const ProfileSaveData& data, std::string& error) const {
    std::error_code code;
    std::filesystem::create_directories(path_.parent_path(), code);
    const auto temporary = std::filesystem::path(path_.string() + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << to_json(data).dump(2) << '\n';
        if (!out.flush()) {
            error = "could not write " + temporary.string();
            return false;
        }
    }
    if (std::filesystem::exists(path_, code))
        std::filesystem::copy_file(path_, path_.string() + ".bak", std::filesystem::copy_options::overwrite_existing,
                                   code);
    std::filesystem::rename(temporary, path_, code);
    if (code) {
        error = "could not replace " + path_.string() + ": " + code.message();
        return false;
    }
    return true;
}

} // namespace tetrisphere
