#include "tetrisphere/linux_platform.h"
#include "tetrisphere/data_directory.h"
#include "tetrisphere/audio_length_pacer.h"
#include "tetrisphere/controller_ports.h"
#include "tetrisphere/controller_mappings.h"
#include "tetrisphere/keyboard_state.h"
#include "tetrisphere/port_input.h"
#include "tetrisphere/qa_virtual_controllers.h"
#include "tetrisphere/audio_output.h"
#include "tetrisphere/diagnostic_log.h"
#include "tetrisphere/perf_metrics.h"
#include "tetrisphere/trophy_service.h"
#include "tetrisphere/game_event_watcher.h"
#include "tetrisphere/game_options.h"
#include "tetrisphere/close_game_menu.h"
#include "tetrisphere/crash_report.h"
#include "tetrisphere/text_rewrites.h"

#include "tetrisphere/rt64_context.h"
#include "tetrisphere/run_options.h"
#include "tetrisphere/run_identity.h"
#include "tetrisphere/runtime.h"
#include "tetrisphere/settings_store.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/profiles.h"
#include "tetrisphere/profile_menu.h"
#include "tetrisphere/minefield_sphere.h"
#include "tetrisphere/mouse_play.h"
#include "tetrisphere/game_over_shell.h"
#include "tetrisphere/menu_unlocks.h"
#include "tetrisphere/pause_bridge.h"
#include "tetrisphere/brick_sky.h"
#include "tetrisphere/pause_draw.h"
#include "tetrisphere/story_hud.h"
#include "tetrisphere/developer_records.h"
#include "tetrisphere/magic_model.h"
#include "tetrisphere/audio_settings.h"
#include "tetrisphere/relaunch.h"
#include "tetrisphere/menu_input.h"
#include "tetrisphere/mode_launcher.h"
#include "tetrisphere/direct_start.h"
#include "ui_system.h"
#include "tetrisphere/box_art.h"
#include "tetrisphere/profile_save.h"
#include "tetrisphere/sphere_mesh.h"
#include "tetrisphere/wav_capture.h"
#include "tetrisphere/window_mode_controller.h"
#include "tetrisphere/window_placement.h"
#include "tetrisphere/pc_settings_hook.h"
#include "tetrisphere/controls_sdl.h"

#include "librecomp/rsp.hpp"
#include "ultramodern/input.hpp"
#include "ultramodern/ultramodern.hpp"
#define XXH_INLINE_ALL
#include "xxhash.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#include <timeapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif
#endif

#include <array>
#include <atomic>
#include <optional>
#include <random>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <cstring>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <vector>

RspExitReason tetrisphere_audio_rsp(std::uint8_t*, std::uint32_t);
extern std::atomic_bool exited;
extern std::atomic<std::uint64_t> tetrisphere_ai_attempts;
extern std::atomic<std::uint64_t> tetrisphere_ai_fifo_rejections;
extern std::atomic<std::uint64_t> tetrisphere_ai_fifo_wait_events;
extern std::atomic<std::uint64_t> tetrisphere_ai_fifo_waits_ms;
extern std::atomic<std::uint64_t> tetrisphere_ai_length_caps;
extern moodycamel::LightweightSemaphore graphics_shutdown_ready;

namespace tetrisphere {
namespace {

SDL_Window* window = nullptr;
ControllerPorts controller_ports;
QaVirtualControllers qa_virtual_controllers;
bool qa_controllers_active = false;
KeyboardState keyboard_state;
std::mutex input_snapshot_mutex;
tetrisphere::MousePlay mouse_play;  // the mouse in play, turned into N64 buttons
std::array<ControllerPortSnapshot, 2> input_snapshot{};
std::uint16_t keyboard_snapshot = 0;
AudioOutput audio_output;
std::mutex audio_mutex;
WavCapture audio_capture;
std::atomic<std::uint64_t> observed_vis{0};
std::atomic<std::uint64_t> input_polls{0};
std::atomic<std::uint64_t> audio_rsp_tasks{0};
std::atomic<std::uint64_t> audio_rsp_total_us{0};
std::atomic<std::uint64_t> audio_rsp_max_us{0};
bool perf_log_enabled = false;
const std::uint8_t* guest_rdram = nullptr;
GameEventWatcher game_event_watcher;
std::unique_ptr<GameOptionApplier> game_option_applier;
bool log_game_events = false;

// Every Story episode and stage open in the menus (--unlock-all, testing).
bool unlock_all_testing = false;
// Start-up screens (the owner's choice, 2026-11-08): title, then who is
// playing (P1), then the main menu; Back goes back through them.
bool profile_picked = false;  // P1 chosen this session (or given on the command line)
// The loading cover over a mode's start (set in launch_mode).
bool loading_active = false;
// The menus a game was started from (the owner's choice, 2026-11-09): every
// way out of it (finished, game over, Quit to menu) goes back there.
std::vector<std::pair<std::string, std::size_t>> launch_path;
int launch_stages_chapter = 0;  // the stage list's chapter in that path, to refresh its bests
std::string stages_title_now;     // the stage list's heading as last set ("Episode 2", "Puzzles")
std::string launch_place;         // that heading when a game started from the stage list, for "Exit to ..."
// Opens the launch path again (once a second at most: the request takes a
// frame or two), refreshing the stage list's bests first. The path is kept:
// the menus may close once more as play ends.
void restore_launch_path() {
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(1)) return;
    last = now;
    for (const auto& [screen, focus] : launch_path)
        if (screen == "stages" && launch_stages_chapter > 0)
            blockplanet::ui::send_command("stages:" + std::to_string(launch_stages_chapter) + ":refresh");
    blockplanet::ui::request_restore(launch_path);
}
std::string loading_title;
// Quit to menu was chosen from the pause: the menu grid stays up until the
// menus open again (docs/UI.md, Menu grid).
std::optional<std::chrono::steady_clock::time_point> grid_quit_at;
// The game's end menus under BlockPlanet's panel (EndMenuBridge): while they
// (or their fade) are up the grid shows in red, and after Exit too.
bool end_menu_active = false;
bool end_menu_cleared = false;  // the end menu is an episode's end (not a game over)
// An episode's last stage just cleared in a whole-episode Rescue run (1), or
// a stage before the last in play (-1): told to the end menu bridge.
int episode_end_expected = 0;
bool lines_time_reset = false;  // a launch: Lines' stage time starts again
// The episode-end panel's line: the episode's time against the best (a run
// from stage 1 only).
std::string episode_end_line;
// Hide & Seek's episode (30 levels): the time of the levels cleared in this
// run of it, whether the run began at its level 1, and the level in play's
// time, for the results panel at the episode's end.
double hs_run_total = 0.0, hs_level_time = 0.0;
bool hs_run_from_first = false;
// Exit chosen on Hide & Seek's episode-end panel: close the game's card,
// then pause the next level and quit through the pause.
std::optional<std::chrono::steady_clock::time_point> hs_exit_since;
bool hs_exit_paused = false;
// The start card (the shared pattern, docs/MODE_MATRIX.md): a Rescue run
// launched from the menus (not a restart) waits on a card with its goal.
// Pending from the launch until play starts; then BlockPlanet opens the
// game's pause and shows the card in place of the pause screen.
std::optional<std::chrono::steady_clock::time_point> start_card_pending;
int start_card_mode = 1;  // the game mode the card is for (1 Rescue, 4 Puzzle)
std::optional<std::chrono::steady_clock::time_point> start_card_requested;
bool start_card_showing = false;
bool grid_quit_red = false;
std::chrono::steady_clock::time_point loading_since;
bool credits_pending = false; // --credits: play them when the title shows

// The active profile's save file (progress and preferences); none for a
// guest. Progress is not recorded with --start-chapter (a testing option).
std::unique_ptr<ProfileSave> profile_save;
// The profile list, for the profile screens; and whether this is guest play.
std::unique_ptr<ProfileStore> profile_store;
bool guest_session = false;
// The name screen's text, and the profile the character screen is for.
NameEditor name_editor;
std::string character_profile_id;
// The coin screen's colours (random each time it opens, the owner's choice).
std::vector<int> coin_screen_colours;
ProfileSaveData profile_data;
// The Sky test's "Play test stage" (brick sky research): the profile as it
// was before, put back afterwards, and nothing saved meanwhile, so the run
// leaves no progress, records or Continue checkpoint.
std::optional<ProfileSaveData> sky_test_profile;
bool sky_test_seen_play = false;
int sky_test_variants[2] = {0, 0};  // play_variant and menu_variant before the run, put back after
bool profile_recording = false;
bool profile_dirty = false;
// A Chapter 0 (a Training lesson) started from a chapter list: when it is
// finished, carry on into that mode's next part (Story Chapter 1, or the
// puzzles).
std::string chapter_zero_lesson;  // "basics" or "puzzle"
// A lesson chosen in Training's menu (lesson:T:L): when the game moves on to
// the next lesson, BlockPlanet leaves through the pause, back to the menu.
std::optional<int> single_lesson;
// A lost field under its shell: the sphere can no longer be turned (the owner's design).
std::atomic<bool> minefield_frozen{false};
std::atomic<bool> game_over_frozen{false};  // the shell closing before the game's game over
std::atomic<bool> hold_shell_on{false};     // the shell drawn while the game over is held
float card_stick = 0.0f;  // the right stick's vertical position, scrolling the card panel
// A mode to start once the game is back at its menu: after a Chapter 0, or
// for Restart level. `start` is the chapter and level (1-based) to begin at.
struct PendingLaunch {
    LaunchMode mode;
    std::optional<std::pair<int, int>> start;
    bool keep_music = false;  // keep the pause menu's track override (a restart)
};
std::optional<PendingLaunch> continue_mode;
// A single Story stage being played on its own (Story > chapter > stage):
// chapter and level; after it is completed the game returns to the menu.
std::optional<std::pair<int, int>> single_stage;
// Story's episodes played three ways (docs/HUD_DESIGN.md): the story, Time
// Attack (as fast as possible) and Score Attack (as many points as possible).
// All run the game's Story mode; this picks the HUD and the records.
// Casual is Story without a game over (no records or trophies).
enum class PlayVariant { Story, TimeAttack, ScoreAttack, Casual };
PlayVariant play_variant = PlayVariant::Story;  // the run in progress
// What Rescue's run asks of the player, for the start card and the pause:
// the core square to uncover (the stage's, +0x23E4) and the variant's rule.
std::string rescue_goal(int square) {
    std::string goal = "Dig down to the core: uncover a " + std::to_string(square) + "\u00A0\u00D7\u00A0" + std::to_string(square) +
                       " square of it to rescue the friend inside.";
    if (play_variant == PlayVariant::Casual) goal += " Lost hearts come back: no game over.";
    else if (play_variant == PlayVariant::TimeAttack) goal += " Against the clock: each stage's time is set against your best.";
    else if (play_variant == PlayVariant::ScoreAttack) goal += " Score all you can before the clock runs out; clearing blocks adds time.";
    return goal;
}
// The start card's key for the run being played (one card per game type).
const char* start_card_key(PlayVariant v) {
    return v == PlayVariant::Casual ? "casual" : v == PlayVariant::TimeAttack ? "time_attack"
           : v == PlayVariant::ScoreAttack ? "score_attack" : "rescue";
}
PlayVariant menu_variant = PlayVariant::Story;  // the chapter list on screen
// Lines' menus (the owner, 2026-11-15: Lines as Rescue, with Campaign, Time
// Attack and Score Attack): the episode and stage lists on screen are Lines'
// (menu_lines), and the run in play is Lines (play_lines).
bool menu_lines = false, play_lines = false;
constexpr int lines_chapters = 5;  // the game's final Lines ending follows episode 5
const char* variant_records(PlayVariant v) {
    return v == PlayVariant::TimeAttack ? "time_attack" : v == PlayVariant::ScoreAttack ? "score_attack" : "rescue";
}
const char* variant_title(PlayVariant v) {
    return v == PlayVariant::TimeAttack ? "Time Attack" : v == PlayVariant::ScoreAttack ? "Score Attack"
           : v == PlayVariant::Casual ? "Casual" : "Rescue";
}
bool variant_competes(PlayVariant v) { return v == PlayVariant::TimeAttack || v == PlayVariant::ScoreAttack; }
struct SingleStageResult { double time; double best_before; };
std::optional<SingleStageResult> single_stage_result;
// A single stage just cleared, for the results screen shown once the menus
// are back (the shared pattern, docs/MODE_MATRIX.md).
struct StageResults {
    int chapter = 0, level = 0;
    bool points = false;  // Score Attack: points, not a time
    double value = 0.0, best_before = 0.0;
    std::string where;
};
std::optional<StageResults> pending_results;
std::optional<std::chrono::steady_clock::time_point> single_stage_quit_at;
bool single_stage_quitting = false;
// After leaving play from BlockPlanet (a single stage done, or Quit to menu),
// open BlockPlanet's menus once the game is back at its own menu: at the
// stage list for a single stage (0: the main menu).
std::optional<int> return_to_menu_chapter;
// Continue: the checkpoint to put back once its episode starts.
std::optional<tetrisphere::ProfileProgress::LastPlay> pending_restore;
// The PC's default controls from settings.dat, for new profiles and guests.
ControlPreferences pc_controls;
std::chrono::steady_clock::time_point last_play_poll{};

ControlPreferences controls_of(const Settings& settings) {
    return {settings.family_override, settings.bindings, settings.magic};
}

void apply_controls(Settings& settings, const ControlPreferences& controls) {
    settings.family_override = controls.family_override;
    settings.bindings = controls.bindings;
    settings.magic = controls.magic;
}

void publish_menus();
void save_profile() {
    if (!profile_save || !profile_dirty || sky_test_profile) return;
    std::string error;
    if (profile_save->save(profile_data, error)) profile_dirty = false;
    else std::fprintf(stderr, "Could not save the profile: %s\n", error.c_str());
    publish_menus();
}

// BlockPlanet's HUD (docs/HUD_DESIGN.md): in Story play the original HUD's 2D
// parts and piece queue are hidden and BlockPlanet's own HUD is drawn
// (placeholder art). BLOCKPLANET_ORIGINAL_HUD=1 keeps the original.
std::string hud_material_colour(int material) {
    switch (material) {
    case 32: return "#e8c020";  // yellow
    case 33: return "#3050d8";  // blue
    case 34: return "#30b048";  // green
    case 35: return "#c838c0";  // magenta
    case 36: return "#9880e0";  // pale lavender
    case 37: return "#d83838";  // red
    default: return "#a0a0a0";
    }
}

// Cell layouts for the sprites, by shape ID (docs/research/blockplanet-game-state.md,
// the piece queue), matched against the original's queue: 1 the bar, 2 the
// square, 5 the S, 6 the T. Others (later stages) are drawn as their cell
// count in rows of two until seen.
std::vector<std::pair<int, int>> hud_piece_cells(int shape, int cells) {
    if (shape == 1) return {{0, 0}, {1, 0}, {2, 0}};
    if (shape == 2) return {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    if (shape == 5) return {{1, 0}, {1, 1}, {0, 1}, {0, 2}};
    if (shape == 6) return {{0, 0}, {1, 0}, {2, 0}, {1, 1}};
    std::vector<std::pair<int, int>> out;
    for (int i = 0; i < std::clamp(cells, 1, 9); ++i) out.push_back({i % 2, i / 2});
    return out;
}

std::string hud_time(double seconds) {
    const int total = static_cast<int>(seconds);
    char text[32];
    if (total >= 3600)
        std::snprintf(text, sizeof text, "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    else
        std::snprintf(text, sizeof text, "%d:%02d", total / 60, total % 60);
    return text;
}

// A number with thousands separators: "12,340".
std::string hud_number(long long value) {
    std::string digits = std::to_string(value < 0 ? -value : value), out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (value < 0 ? "-" : "") + out;
}

// A difference against a best time: "-3.4" or "+1:03".
std::string hud_delta(double seconds) {
    const double a = std::abs(seconds);
    char text[32];
    const char* sign = seconds < 0.0 ? "\u2212" : "+";
    if (a < 60.0)
        std::snprintf(text, sizeof text, "%s%.1f", sign, a);
    else
        std::snprintf(text, sizeof text, "%s%s", sign, hud_time(a).c_str());
    return text;
}

// The results screen of a stage played on its own, over the stage list:
// the time (or points) against the best before, then Next Stage, Try Again
// and Back.
void show_stage_results() {
    if (!pending_results) return;
    const auto r = *pending_results;
    pending_results.reset();
    std::string line;
    if (r.points) {
        line = hud_number(static_cast<long long>(r.value)) + " points";
        if (r.best_before <= 0.0 || r.value > r.best_before) line += "  \u00B7  New best";
        else line += "  \u00B7  Best " + hud_number(static_cast<long long>(r.best_before));
    } else {
        line = hud_time(r.value);
        if (r.best_before <= 0.0 || r.value < r.best_before) line += "  \u00B7  New best";
        else line += "  \u00B7  Best " + hud_time(r.best_before) + " (" + hud_delta(r.value - r.best_before) + ")";
    }
    blockplanet::ui::set_text("results_title", "Stage Cleared");
    blockplanet::ui::set_text("results_where", r.where);
    blockplanet::ui::set_text("results_line", line);
    std::vector<blockplanet::ui::ListItem> items;
    if (r.level < 10)
        items.push_back({"next", "Next Stage", "Stage " + std::to_string(r.chapter) + ":" + std::to_string(r.level + 1),
                         "command:stage:" + std::to_string(r.chapter) + ":" + std::to_string(r.level + 1), true, 1, true});
    items.push_back({"again", "Try Again", "", "command:stage:" + std::to_string(r.chapter) + ":" + std::to_string(r.level), true, 1,
                     r.level >= 10});
    items.push_back({"back", "Back to Episode " + std::to_string(r.chapter), "", "back", true});
    blockplanet::ui::set_list("results", items);
    blockplanet::ui::request_show("results");
}

// The developer's records (assets/trophies/developer_records.json). With
// BLOCKPLANET_RECORD_DEVELOPER=<file>, the owner's finished attack stages are
// written to that file when they are the first or better (docs/HUD_DESIGN.md).
tetrisphere::DeveloperRecords developer_records;
std::filesystem::path developer_record_path;

void load_developer_records_at_start(const std::filesystem::path& shipped) {
    std::string error;
    if (const char* path = std::getenv("BLOCKPLANET_RECORD_DEVELOPER"); path != nullptr && *path != '\0') {
        developer_record_path = std::filesystem::u8path(path);
        if (std::filesystem::exists(developer_record_path)) {
            if (!tetrisphere::load_developer_records(developer_record_path, developer_records, error))
                std::fprintf(stdout, "{\"event\":\"developer_records\",\"error\":\"%s\"}\n", error.c_str());
            return;
        }
    }
    if (!tetrisphere::load_developer_records(shipped, developer_records, error))
        std::fprintf(stdout, "{\"event\":\"developer_records\",\"error\":\"%s\"}\n", error.c_str());
}

// A stage finished in Time Attack or Score Attack: the developer's record,
// and the trophies' events.
void attack_stage_done(const std::string& mode, int chapter, int stage, double value) {
    value = std::round(value * 1000.0) / 1000.0;  // milliseconds (points stay whole)
    const double developer = developer_records.value(mode, chapter, stage);
    if (!developer_record_path.empty() && developer_records.record(mode, chapter, stage, value)) {
        std::string error;
        if (!tetrisphere::save_developer_records(developer_record_path, developer_records, error))
            std::fprintf(stdout, "{\"event\":\"developer_records\",\"error\":\"%s\"}\n", error.c_str());
        std::fprintf(stdout, "{\"event\":\"developer_record\",\"mode\":\"%s\",\"chapter\":%d,\"stage\":%d,\"value\":%.3f}\n",
                     mode.c_str(), chapter, stage, value);
        std::fflush(stdout);
    }
    tetrisphere::GameEvent event{"attack_stage", {}};
    event.fields["mode"] = mode;
    event.fields["chapter"] = std::int64_t{chapter};
    event.fields["stage"] = std::int64_t{stage};
    event.fields["developer_beaten"] = std::int64_t{tetrisphere::DeveloperRecords::beats(mode, value, developer) ? 1 : 0};
    post_game_event(event);
    std::map<int, std::vector<double>> bests;
    if (const auto m = profile_data.progress.episode_records.find(mode); m != profile_data.progress.episode_records.end())
        for (const auto& [c, record] : m->second) bests[c] = record.best_levels;
    if (developer_records.all_beaten(mode, bests)) {
        tetrisphere::GameEvent all{"developer_all_beaten", {}};
        all.fields["mode"] = mode;
        post_game_event(all);
    }
}

void attack_episode_done(const std::string& mode, int chapter) {
    tetrisphere::GameEvent event{"attack_episode", {}};
    event.fields["mode"] = mode;
    event.fields["chapter"] = std::int64_t{chapter};
    event.fields["final"] = std::int64_t{chapter == tetrisphere::DeveloperRecords::chapters ? 1 : 0};
    post_game_event(event);
}

// The main menu's Continue (docs/REDESIGN.md): offered while a Story or
// Casual game was left unfinished (not after a game over).
// The main menu, with the first-launch question on top for a new profile
// (not yet asked, nothing played) or a guest's first visit this session (the
// owner's plan, docs/MENU_STRUCTURE.md): "Do you want to go through basic
// training?" Yes starts Training (Basics); No goes to the main menu.
bool guest_training_asked = false;
void open_main_menu() {
    blockplanet::ui::request_open("main");
    bool ask = false;
    if (guest_session) {
        ask = !guest_training_asked;
        guest_training_asked = true;
    } else if (profile_save && !profile_data.progress.training_offered) {
        const auto found = profile_data.progress.chapters_completed.find("rescue");
        const bool played = found != profile_data.progress.chapters_completed.end() && !found->second.empty();
        profile_data.progress.training_offered = true;
        profile_dirty = true;
        save_profile();
        ask = !played;
    }
    if (ask) blockplanet::ui::request_show("first_training");
}

// The main, Solo and Rescue menus as the player's progress opens them up
// (docs/MENU_STRUCTURE.md; in the main build everything, with statuses).
tetrisphere::MenuUnlocks menu_unlocks_now() {
    tetrisphere::MenuUnlocks u;
    u.everything = unlock_all_testing;
    if (const auto found = profile_data.progress.chapters_completed.find("rescue"); found != profile_data.progress.chapters_completed.end())
        for (int chapter : found->second)
            if (chapter >= 1) u.episodes.insert(chapter);
    return u;
}

void publish_menus() {
    const auto u = menu_unlocks_now();
    blockplanet::ui::set_list("main_items", tetrisphere::main_menu_items(u));
    blockplanet::ui::set_list("main_icons", tetrisphere::main_icon_items(u));
    blockplanet::ui::set_list("solo_items", tetrisphere::solo_menu_items(u));
    blockplanet::ui::set_list("solo_bonus_items", tetrisphere::solo_bonus_items(u));
    blockplanet::ui::set_list("local_items", tetrisphere::local_menu_items(u));
    blockplanet::ui::set_list("local_bonus_items", tetrisphere::local_bonus_items(u));
    blockplanet::ui::set_list("rescue_items", tetrisphere::rescue_menu_items(u));
    blockplanet::ui::set_list("lines_items", tetrisphere::lines_menu_items(u));
    // Developer items in Options (main build only, for testing the unlocks).
    std::vector<blockplanet::ui::ListItem> developer;
    if (unlock_all_testing && profile_save) {
        blockplanet::ui::ListItem complete{"dev_complete_rescue", "Complete Every Rescue Episode", "(Developer)",
                                           "command:dev_complete_rescue", true};
        complete.description = "Marks every Rescue episode and stage completed for this profile, to test what unlocks. "
                                "It cannot be undone.";
        developer.push_back(complete);
    }
    blockplanet::ui::set_list("developer_items", std::move(developer));
}

void refresh_continue() {
    const auto& last = profile_data.progress.last_play;
    std::vector<blockplanet::ui::ListItem> items;
    if (!last.submode.empty() && last.chapter >= 1 && last.level >= 1) {
        blockplanet::ui::ListItem item;
        item.id = "continue";
        item.label = "Continue";
        item.note = std::string(last.submode == "casual" ? "Casual" : "Rescue") + ", episode " + std::to_string(last.chapter);
        item.action = "command:continue";
        item.description = "Carry on from the start of episode " + std::to_string(last.chapter) +
                           ", with the score and hearts you had when it began.";
        item.focus = true;
        items.push_back(std::move(item));
    }
    blockplanet::ui::set_list("continue", std::move(items));
    publish_menus();
}

// Hide & Seek's goal (0x800E44A4), in BlockPlanet's own words
// (docs/research/blockplanet-game-state.md, Hide & Seek goals).
std::string hide_and_seek_goal(int goal) {
    switch (goal) {
    case 1: return "Find any picture";
    case 2: return "Under a drill";
    case 3: return "Open the core";
    case 4: return "Four parts";
    case 5: return "One shape";
    case 7: return "Drop a brick";
    case 9: return "Connect";
    case 10: return "Around the tower";
    case 11: return "Crystal tower";
    default: return "";
    }
}

void update_magic_box();
void place_hud(blockplanet::ui::HudView& view, const tetrisphere::StoryHudState& state);
std::optional<blockplanet::ui::HudView> last_hud_view;  // what update_hud last showed (nullopt: none)
// The last HUD shown in play, kept for the Game Over panel's line (the HUD
// itself is hidden once the game is over).
std::optional<blockplanet::ui::HudView> last_play_hud;
void show_hud(std::optional<blockplanet::ui::HudView> view) {
    if (view) last_play_hud = view;
    last_hud_view = std::move(view);
}
// The Game Over panel's line (the shared results panel): the time, the score
// and the best as the HUD last showed them, e.g. "Time 3:21 · Score 12,300".
std::string game_over_line(bool time_only = false) {
    if (!last_play_hud) return {};
    const auto& v = *last_play_hud;
    std::vector<std::string> parts;
    if (!v.time.empty()) parts.push_back((v.time_label.empty() ? std::string("Time") : v.time_label) + " " + v.time);
    if (time_only) return parts.empty() ? std::string() : parts.front();
    if (!v.story_score.empty()) parts.push_back("Score " + v.story_score);
    else if (!v.score.empty()) parts.push_back((v.score_label.empty() ? std::string("Score") : v.score_label) + " " + v.score);
    if (!v.best.empty()) parts.push_back(v.best);
    std::string line;
    for (const auto& p : parts) line += (line.empty() ? "" : "  \u00B7  ") + p;
    return line;
}
void publish_game_text();
// Graphics > Text size, as a scale for the card and speech text.
float card_text_scale() {
    const auto size = profile_data.preferences.text_size.value_or("default");
    return size == "small" ? 0.85f : size == "large" ? 1.2f : size == "larger" ? 1.4f : 1.0f;
}

// A puzzle just solved, for the results panel shown as the next one starts.
struct PuzzleResult { int number = 0; double time = 0.0, best_before = 0.0; int moves = 0; double best_moves_before = 0.0; };
std::optional<PuzzleResult> puzzle_result;
int& last_drops_seen() { static int v = 0; return v; }
int& last_slides_seen() { static int v = 0; return v; }

// Puzzle's HUD: drops and slides left (context +0x25C6, +0x25C8), the puzzle
// number, its time (the game's clock, +0x2B0C, counting up in frames) against
// the best for that puzzle, and the fewest moves (drops + slides) it has been
// solved in. Bests in records "puzzle" (times) and "puzzle_moves", chapter 0,
// index = puzzle number - 1.
void update_puzzle_hud() {
    constexpr std::uint32_t p1 = 0x80110220u;
    const int number = guest::load16(guest_rdram, p1 + 0x23BA) + 1;
    const int drops = guest::load16(guest_rdram, p1 + 0x25C6);
    const int slides = guest::load16(guest_rdram, p1 + 0x25C8);
    const double clock = static_cast<std::int32_t>(guest::load32(guest_rdram, p1 + 0x2B0C)) / 60.0;
    static int puzzle = -1, start_drops = 0, start_slides = 0;
    // The game's clock runs on from one puzzle to the next: a puzzle's time
    // counts from its start.
    static double clock_at_start = 0.0;
    if (number != puzzle) clock_at_start = puzzle >= 1 && number == puzzle + 1 ? clock : 0.0;
    if (clock < clock_at_start) clock_at_start = 0.0;  // started again
    const double seconds = clock - clock_at_start;
    static double last_seconds = 0.0;
    auto& times = profile_data.progress.episode_records["puzzle"][0].best_levels;
    auto& moves = profile_data.progress.episode_records["puzzle_moves"][0].best_levels;
    if (number != puzzle) {
        // Solved (the next puzzle has started): keep the best time and moves.
        if (puzzle >= 1 && number == puzzle + 1 && last_seconds > 0.5) {
            const auto i = std::size_t(puzzle - 1);
            if (times.size() <= i) times.resize(i + 1, 0.0);
            if (moves.size() <= i) moves.resize(i + 1, 0.0);
            const double used = double(start_drops - last_drops_seen() + start_slides - last_slides_seen());
            bool changed = false;
            puzzle_result = PuzzleResult{puzzle, last_seconds, times[i], int(used), moves[i]};
            if (times[i] <= 0.0 || last_seconds < times[i]) times[i] = last_seconds, changed = true;
            if (used > 0.0 && (moves[i] <= 0.0 || used < moves[i])) moves[i] = used, changed = true;
            if (changed) {
                profile_dirty = true;
                save_profile();
            }
        }
        puzzle = number;
        start_drops = drops;
        start_slides = slides;
    }
    // The counts at a puzzle's start are its allowance (a restart sets them again).
    start_drops = std::max(start_drops, drops);
    start_slides = std::max(start_slides, slides);
    last_drops_seen() = drops;
    last_slides_seen() = slides;
    last_seconds = seconds;

    blockplanet::ui::HudView view;
    view.drops_left = std::max(0, drops);
    view.slides_left = std::max(0, slides);
    view.mode = "Puzzle";
    view.stage = std::to_string(number);
    view.time = hud_time(seconds);
    const auto i = std::size_t(number - 1);
    const double best_time = i < times.size() ? times[i] : 0.0;
    const double best_moves = i < moves.size() ? moves[i] : 0.0;
    if (best_time > 0.0) {
        view.time_delta = hud_delta(seconds - best_time);
        view.time_ahead = seconds < best_time;
        view.best = "Best " + hud_time(best_time);
        if (best_moves > 0.0)
            view.best += " \u00B7 " + std::to_string(int(best_moves)) + (best_moves == 1.0 ? " move" : " moves");
    }
    tetrisphere::StoryHudState state;
    place_hud(view, state);
    show_hud(view);
}

bool update_title_overlay();
extern "C" bool blockplanet_title_recent(std::uint32_t* image);
extern "C" float blockplanet_title_notice_opacity();

// Training (mode 3, docs/TRAINING_REDESIGN.md): Story's HUD as one row along
// the bottom, each part shown once Wheels has introduced it, found from the
// page he is on (0x80103958, the start of his current page, which only moves
// forward through a lesson's text). A part glows for a few seconds when it
// first appears. The original HUD is hidden.
void update_training_hud(const tetrisphere::StoryHudState& state) {
    const int type = guest::load16(guest_rdram, 0x800E44AC);  // 1 Basics, 2 Advanced, 3 Puzzle
    const std::uint32_t page = guest::load32(guest_rdram, 0x80103958);
    // A page "has reached" a topic when it starts at (or just before: its
    // codes) the text that introduces it.
    const auto reached = [&](std::uint32_t text) { return page + 8 >= text; };
    blockplanet::ui::HudView view;
    view.bottom_row = true;
    view.show_next = view.show_lives = view.show_magic = false;
    if (type == 1) {
        view.show_next = reached(0x800EE8FC);   // "notice that the shadow is in the shape of the current piece"
        view.show_lives = reached(0x800EEC5C);  // "Lives:"
        if (reached(0x800EED70)) view.progress = state.progress;  // "Game objective:"
        view.show_magic = reached(0x800EF198);  // "Magic:"
    } else if (type == 2) {
        view.show_next = view.show_lives = view.show_magic = true;  // taught in Basics
        view.progress = state.progress;
        if (reached(0x800EF44C)) {  // "X count:"
            constexpr std::uint32_t p1 = 0x80110220u;
            view.multiplier = guest::load16(guest_rdram, p1 + 0x2AE6) + guest::load16(guest_rdram, p1 + 0x2AE8) + 1;
        }
    } else if (type == 3 && reached(0x800EFAAB)) {  // "remove every piece ... slides and drops allowed"
        constexpr std::uint32_t p1 = 0x80110220u;
        view.drops_left = std::max<int>(0, guest::load16(guest_rdram, p1 + 0x25C6));
        view.slides_left = std::max<int>(0, guest::load16(guest_rdram, p1 + 0x25C8));
    }
    // The glow: from when each part first appears in this lesson.
    static int glow_type = 0;
    static std::uint32_t glow_page = 0;
    static std::map<std::string, std::chrono::steady_clock::time_point> since;
    if (type != glow_type || page < glow_page) since.clear();  // a new or restarted lesson
    glow_type = type;
    glow_page = page;
    const auto now = std::chrono::steady_clock::now();
    const std::pair<const char*, bool> parts[] = {{"next", view.show_next}, {"lives", view.show_lives}, {"magic", view.show_magic},
                                                  {"progress", view.progress >= 0.0f}, {"xcount", view.multiplier >= 1},
                                                  {"moves", view.drops_left >= 0}};
    for (const auto& [name, shown] : parts) {
        if (!shown) continue;
        // Parts shown from the lesson's start (taught earlier) do not glow.
        const auto it = since.try_emplace(name, page + 8 < 0x800EE7B0u || (type == 2 && std::string(name) != "xcount")
                                                    ? now - std::chrono::seconds(10) : now).first;
        if (now - it->second < std::chrono::milliseconds(3000)) view.intro.push_back(name);
    }
    view.hearts = state.hearts;
    view.drop_left = state.drop_left;
    view.magic = state.magic;
    view.magic_model = view.show_magic && blockplanet::magic_box_active();
    view.meter = state.meter;
    for (const auto& piece : state.next) {
        blockplanet::ui::HudPieceView p;
        p.random_block = piece.random_block;
        p.colour = hud_material_colour(piece.material);
        p.cells = hud_piece_cells(piece.shape, piece.cells);
        view.next.push_back(std::move(p));
    }
    blockplanet::set_original_hud_hidden(true);
    if (view.show_magic) update_magic_box();
    else blockplanet::set_magic_box(false, NAN, NAN, 1.0f, NAN, 0.0f);
    place_hud(view, state);
    show_hud(view);
}

// Training's sphere, whole: the game draws it through a ready-made display
// list that clips to the left half of the screen (a scissor 0..160 of 320, to
// keep it clear of the original speech bubble), which slices its right edge
// flat. BlockPlanet's speech panel no longer sits there, so in Training the
// scissor is widened to the whole screen, and put back for every other mode
// (VS's split screen uses it). Found by its value near 0x80165148 (the game's
// table of viewports and scissors), as the address may move.
void update_training_scissor(bool training) {
    static std::uint32_t found = 0;
    static bool widened = false;
    if (guest_rdram == nullptr || std::getenv("BLOCKPLANET_ORIGINAL_TRAINING_CLIP") != nullptr) return;
    auto* rdram = const_cast<std::uint8_t*>(guest_rdram);
    constexpr std::uint32_t half = 0x002803C0u, whole = 0x005003C0u;  // lower right: x 160 or 320, y 240 (quarter pixels)
    const auto valid = [&](std::uint32_t a) {
        const std::uint32_t w1 = guest::load32(guest_rdram, a + 4);
        return guest::load32(guest_rdram, a) == 0xED000000u && (w1 == half || w1 == whole);
    };
    if (found == 0 || !valid(found)) {
        found = 0;
        for (std::uint32_t a = 0x80164000u; a < 0x80167000u; a += 8)
            if (guest::load32(guest_rdram, a) == 0xED000000u && guest::load32(guest_rdram, a + 4) == half) {
                found = a;
                break;
            }
        widened = false;
    }
    if (found == 0) return;
    if (training && !widened) {
        guest::store32(rdram, found + 4, whole);
        widened = true;
    } else if (!training && widened) {
        guest::store32(rdram, found + 4, half);
        widened = false;
    }
}

void update_hud() {
    static const bool original = std::getenv("BLOCKPLANET_ORIGINAL_HUD") != nullptr;
    if (update_title_overlay()) return;
    static tetrisphere::ProgressTracker progress;
    // The episode's splits (game time per level, LiveSplit style): counted
    // while playing (not paused); a new chapter, Try Again or a new game
    // (the score going back down) begins the run again.
    static tetrisphere::EpisodeSplits splits;
    static std::uint32_t last_score = 0;
    static auto last_tick = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_tick).count();
    last_tick = now;

    tetrisphere::StoryHudState state;
    const bool story = !original && tetrisphere::read_story_hud(guest_rdram, progress, state);
    // Floating numbers: finished combos, aged here (the HUD only draws them).
    struct LivePopup { blockplanet::ui::HudView::Popup popup; float cell_dx, cell_dy; };
    static std::vector<LivePopup> popups;
    static tetrisphere::ComboTracker combos;
    static float combo_dx = 0.0f, combo_dy = 0.0f;
    static int combo_blocks_pending = 0;
    // Story shows no score pop-ups (decided 2026-11-06); BLOCKPLANET_POPUPS=1
    // shows points for testing (score modes may use them later).
    static const bool popups_on = std::getenv("BLOCKPLANET_POPUPS") != nullptr;
    const bool paused_now = story && guest::load16(guest_rdram, 0x800E1228) >= 0;
    if (story && !paused_now) {
        const auto make = [&](const std::string& text, const std::string& note, float dx, float dy) {
            if (std::getenv("BLOCKPLANET_POPUP_LOG")) std::fprintf(stdout, "{\"event\":\"popup\",\"text\":\"%s\"}\n", text.c_str());
            LivePopup live;
            live.popup.text = text;
            live.popup.note = note;
            live.cell_dx = dx;
            live.cell_dy = dy;
            popups.push_back(live);
        };
        if (const auto combo = combos.update(state)) {
            combo_blocks_pending += combo->blocks;  // Score Attack's clock gains time
            combo_dx = combo->dx, combo_dy = combo->dy;  // where its "+N s" rises from
            // For trophies (Story, Time Attack and Score Attack): the blocks a
            // whole combo cleared, as measured from the board.
            if (combo->blocks > 0 && state.mode == 1) {
                tetrisphere::GameEvent event{"story_combo", {}};
                event.fields["mode"] = std::string(variant_records(play_variant) == std::string("rescue") ? "story" : variant_records(play_variant));
                event.fields["blocks"] = std::int64_t{combo->blocks};
                event.fields["score"] = std::int64_t{combo->points};
                post_game_event(event);
            }
            if (popups_on && combo->points > 0) {
                char text[32];
                const long long points = combo->points;
                if (points >= 1000) std::snprintf(text, sizeof text, "+%lld,%03lld", points / 1000, points % 1000);
                else std::snprintf(text, sizeof text, "+%lld", points);
                make(text, combo->blocks >= 3 ? std::to_string(combo->blocks) + " blocks" : "", combo->dx, combo->dy);
            }
        }
        for (auto& p : popups) p.popup.age += float(std::min(elapsed, 0.1));
        popups.erase(std::remove_if(popups.begin(), popups.end(), [](const LivePopup& p) { return p.popup.age > 1.4f; }),
                     popups.end());
    }
    if (!story) popups.clear();
    // Continue (the main menu): a checkpoint as each Story or Casual episode
    // starts (its first stage), with the score and hearts then; a
    // stage left part-way is not resumed (it cannot be restored exactly).
    // Cleared at a game over.
    {
        auto& last = profile_data.progress.last_play;
        const bool in_play = guest::load16(guest_rdram, 0x800E121C) == -1 && guest::load16(guest_rdram, 0x800E44A8) == 1;
        // (Minefield on the sphere plays in Casual 1:1 but is not Casual.)
        const bool campaign = story && state.mode == 1 && state.chapter >= 1 && !single_stage &&
                              !tetrisphere::minefield_sphere::armed() &&
                              (play_variant == PlayVariant::Story || play_variant == PlayVariant::Casual);
        if (in_play && guest::load16(guest_rdram, 0x80110220u + 0x2D7E) < 0 && !last.submode.empty()) {
            last = {};
            profile_dirty = true;
            save_profile();
            refresh_continue();
        } else if (campaign && pending_restore && state.chapter == pending_restore->chapter && state.level == 1) {
            // Continuing: put back the score and hearts of the checkpoint. (Not
            // the magic: writing the held magic skips the game's own set-up for
            // it, and its model came out broken.)
            auto* rdram = const_cast<std::uint8_t*>(guest_rdram);
            guest::store32(rdram, 0x80110220u + 0x2AF8, std::uint32_t(pending_restore->score / 10));
            guest::store16(rdram, 0x80110220u + 0x2D7E, std::int16_t(std::clamp(pending_restore->hearts, 1, 3)));
            pending_restore.reset();
        } else if (campaign && !pending_restore && state.level == 1) {
            const std::string submode = play_variant == PlayVariant::Casual ? "casual" : "campaign";
            if (last.submode != submode || last.chapter != state.chapter) {
                last = {submode, state.chapter, 1, static_cast<std::int64_t>(state.score) * 10, state.hearts, state.magic};
                profile_dirty = true;
                save_profile();
                refresh_continue();
            }
        }
    }
    update_training_scissor(story && state.mode == 3);
    if (story && state.mode == 3) {  // Training
        update_training_hud(state);
        return;
    }
    // VS CPU (mode 6): a column per player (docs/HUD_DESIGN.md, VS).
    if (!story && !original && guest::load16(guest_rdram, 0x800E121C) == -1 &&
        guest::load16(guest_rdram, 0x800E44A8) == 6) {
        // The rules card shows in BlockPlanet's panel (kept from the hidden HUD);
        // with the original text, the original HUD shows while it is up.
        const bool card = std::getenv("BLOCKPLANET_ORIGINAL_TEXT") != nullptr && guest::load16(guest_rdram, 0x801033AC) != 0;
        blockplanet::set_original_hud_hidden(!card);
        blockplanet::set_magic_box(false, NAN, NAN, 1.0f, NAN, 0.0f);
        if (card) {
            show_hud(std::nullopt);
            return;
        }
        blockplanet::ui::HudView view;
        view.versus = true;
        view.distance_mode = 1;  // each column at its half's outer edge
        for (int i = 0; i < 2; ++i) {
            const std::uint32_t ctx = i == 0 ? 0x80110220u : 0x80113488u;
            auto& p = view.players[i];
            p.hearts = std::clamp<int>(guest::load16(guest_rdram, ctx + 0x2D7E), 0, 3);
            p.drop_left = std::clamp(1.0f - float(guest::load16(guest_rdram, ctx + 0x2B10)) / 600.0f, 0.0f, 1.0f);
            p.multiplier = guest::load16(guest_rdram, ctx + 0x2AE6) + guest::load16(guest_rdram, ctx + 0x2AE8) + 1;
            p.magic = static_cast<std::int8_t>(guest::load8(guest_rdram, ctx + 0x702));
            if (p.magic > 5) p.magic = 5;
            const int full = guest::load16(guest_rdram, ctx + 0x2AEE);
            p.meter = full > 0 ? std::clamp(float(static_cast<std::uint16_t>(guest::load16(guest_rdram, ctx + 0x2AEC))) / float(full), 0.0f, 1.0f) : 0.0f;
            for (int slot = 0; slot < 2; ++slot) {
                const int index = guest::load16(guest_rdram, ctx + 0x2374 + 2u * slot);
                if (index < 0 || index >= 48) continue;
                blockplanet::ui::HudPieceView piece;
                piece.random_block = index == 22;
                const std::uint32_t record = guest::load32(guest_rdram, 0x8014CBC8u + 4u * index);
                if (record >= 0x80000000u && record < 0x80800000u) {
                    piece.colour = hud_material_colour(guest::load16(guest_rdram, record + 2));
                    piece.cells = hud_piece_cells(guest::load8(guest_rdram, record + 8), guest::load16(guest_rdram, record + 4));
                }
                p.next.push_back(std::move(piece));
            }
        }
        tetrisphere::StoryHudState state;
        place_hud(view, state);
        view.distance_mode = 1;  // VS: always at the outer edges
        show_hud(view);
        return;
    }
    // Puzzle (mode 4): its own HUD (docs/HUD_DESIGN.md, Puzzle).
    if (!story && !original && guest::load16(guest_rdram, 0x800E121C) == -1 &&
        guest::load16(guest_rdram, 0x800E44A8) == 4) {
        blockplanet::set_original_hud_hidden(true);
        blockplanet::set_magic_box(false, NAN, NAN, 1.0f, NAN, 0.0f);
        update_puzzle_hud();
        return;
    }
    // A card of the game's own (Lines' rules, Hide & Seek's goal) is drawn by
    // its HUD routine; BlockPlanet shows it in its own panel (the card's text,
    // box and prompt are kept from the hidden HUD), so our HUD stays up. With
    // the original text (BLOCKPLANET_ORIGINAL_TEXT=1) the original HUD shows instead.
    static const bool original_text = std::getenv("BLOCKPLANET_ORIGINAL_TEXT") != nullptr;
    if (story && original_text && guest::load16(guest_rdram, 0x801033AC) != 0) {
        blockplanet::set_original_hud_hidden(false);
        show_hud(std::nullopt);
        blockplanet::set_magic_box(false, NAN, NAN, 1.0f, NAN, 0.0f);
        return;
    }
    // After the last heart (hearts -1) the game plays its GAME OVER scene in
    // play: the original HUD stays hidden there too (it flashed up, the
    // owner's report), with BlockPlanet's HUD gone.
    const bool game_over = !original && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                           std::int16_t(guest::load16(guest_rdram, 0x80110220u + 0x2D7E)) < 0;
    blockplanet::set_original_hud_hidden(story || game_over);
    // The GAME OVER scene's blocks are queued models: keep drawing them (the
    // owner's report: they vanished mid-flight once the HUD was hidden there).
    // From 0.6 s in: before that the old HUD's next pieces (also queued
    // models) are still drawn for a moment.
    static std::optional<std::chrono::steady_clock::time_point> game_over_since;
    if (!game_over) game_over_since.reset();
    else if (!game_over_since) game_over_since = std::chrono::steady_clock::now();
    // (Not under BlockPlanet's shell: its bricks are hidden there, and the
    // original queue's models would show beside the sphere.)
    blockplanet::set_original_hud_keep_models(game_over_since && !tetrisphere::sphere::game_over_shell_wanted(guest_rdram) &&
                                             std::chrono::steady_clock::now() - *game_over_since > std::chrono::milliseconds(600));
    // The next episode's card after an episode's end (the pause row at
    // 100): the game's own full-screen card, with no HUD over it.
    if (!story || guest::load16(guest_rdram, 0x800E1228) >= 100) {
        show_hud(std::nullopt);
        blockplanet::set_magic_box(false, NAN, NAN, 1.0f, NAN, 0.0f);
        return;
    }
    update_magic_box();
    if (state.score < last_score || state.mode != 1) splits.reset();
    last_score = state.score;
    const bool paused = guest::load16(guest_rdram, 0x800E1228) >= 0;
    auto& episode = profile_data.progress.episode_records[variant_records(play_variant == PlayVariant::ScoreAttack ? PlayVariant::Story : play_variant)];
    const auto best_of = [&](int chapter) {
        const auto it = episode.find(chapter);
        return it == episode.end() ? std::vector<double>{} : it->second.best_levels;
    };
    if (state.mode != 1) {
    } else if (const auto finished = splits.update(state.chapter, state.level, !paused && elapsed < 1.0 ? elapsed : 0.0,
                                            best_of(state.chapter))) {
        auto& record = episode[finished->chapter];
        if (record.best_levels.size() < 10) record.best_levels.resize(10, 0.0);
        const auto i = std::size_t(std::clamp(finished->level, 1, 10) - 1);
        const double best_before = record.best_levels[i];
        const double best_total_before = record.best_total;
        bool changed = false;
        const bool keeps_records = play_variant != PlayVariant::Casual;  // Casual sets no records
        if (keeps_records && finished->time > 0.5 && (record.best_levels[i] <= 0.0 || finished->time < record.best_levels[i]))
            record.best_levels[i] = finished->time, changed = true;
        if (keeps_records && finished->episode_done && (record.best_total <= 0.0 || finished->episode_total < record.best_total))
            record.best_total = finished->episode_total, changed = true;
        if (changed) {
            profile_dirty = true;
            save_profile();
        }
        if (finished->episode_done && finished->episode_total > 0.5) {
            episode_end_line = "Episode time " + hud_time(finished->episode_total);
            if (!keeps_records) {
            } else if (best_total_before <= 0.0 || finished->episode_total < best_total_before) {
                episode_end_line += "  \u00B7  New best";
            } else {
                episode_end_line += "  \u00B7  Best " + hud_time(best_total_before) + " (" +
                                    hud_delta(finished->episode_total - best_total_before) + ")";
            }
        }
        if (play_variant == PlayVariant::TimeAttack && finished->time > 0.5)
            attack_stage_done("time_attack", finished->chapter, finished->level, finished->time);
        // A single stage: done; back to the menu shortly.
        if (play_variant != PlayVariant::ScoreAttack && single_stage && finished->chapter == single_stage->first &&
            finished->level == single_stage->second) {
            single_stage_result = SingleStageResult{finished->time, best_before};
            single_stage_quit_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        }
    }

    blockplanet::ui::HudView view;
    view.hearts = state.hearts;
    view.drop_left = state.drop_left;
    view.magic = state.magic;
    view.magic_model = blockplanet::magic_box_active();
    view.meter = state.meter;
    for (const auto& piece : state.next) {
        blockplanet::ui::HudPieceView p;
        p.random_block = piece.random_block;
        p.colour = hud_material_colour(piece.material);
        p.cells = hud_piece_cells(piece.shape, piece.cells);
        view.next.push_back(std::move(p));
    }
    if (state.mode == 9) {
        view.show_next = false;  // Lines shows no queue
        view.mode = "Lines";
        view.stage = std::to_string(state.chapter) + ":" + std::to_string(state.level);
    } else if (state.mode == 5) {
        view.mode = "Hide & Seek";
        view.stage = std::to_string(state.level);  // its own chapter is not found yet
        view.goal = hide_and_seek_goal(guest::load16(guest_rdram, 0x800E44A4));
    } else {
        view.mode = variant_title(play_variant);
        view.stage = std::to_string(state.chapter) + ":" + std::to_string(state.level);
    }
    const bool single = single_stage && state.mode == 1 && state.chapter == single_stage->first;
    // A whole episode in an attack mode is done when its last stage is
    // cleared (the core opened; the final episode has no next one to start).
    // Rescue's episode-end menu follows the last stage of a whole-episode
    // run (Story and Casual; the attack modes leave by themselves).
    if (state.mode == 1 && !single_stage && (play_variant == PlayVariant::Story || play_variant == PlayVariant::Casual)) {
        static bool last_cleared = false;  // told once, as it becomes so
        const bool last_cleared_now = state.level >= 10 && state.progress >= 1.0f;
        if (last_cleared_now && !last_cleared) episode_end_expected = 1;
        else if (state.level < 10 && last_cleared) episode_end_expected = -1, episode_end_line.clear();
        last_cleared = last_cleared_now;
    }
    {
        static int cleared_episode_key = -1;
        if (variant_competes(play_variant) && !single_stage && state.level >= 10 && state.progress >= 1.0f &&
            cleared_episode_key != state.chapter) {
            cleared_episode_key = state.chapter;
            attack_episode_done(variant_records(play_variant), state.chapter);
        }
        if (state.level < 10) cleared_episode_key = -1;
    }
    if (state.mode == 1 && play_variant == PlayVariant::ScoreAttack) {
        // Score Attack on Story's episodes: points per stage against the best
        // points of each stage (records "score_attack", stored as numbers).
        static tetrisphere::ScoreSplits score_splits;
        static tetrisphere::AttackClock clock;
        static int score_chapter = -1, score_level = 0;
        static std::uint32_t last_score_attack = 0;
        static bool timed_out = false, run_over = false;
        auto& records = profile_data.progress.episode_records["score_attack"];
        const auto save_stage = [&](int chapter, const tetrisphere::ScoreSplits::Finished& f) {
            auto& r = records[chapter];
            if (r.best_levels.size() < 10) r.best_levels.resize(10, 0.0);
            const auto i = std::size_t(std::clamp(f.stage, 1, 10) - 1);
            const double before = r.best_levels[i];
            if (double(f.points) > before) {
                r.best_levels[i] = double(f.points);
                profile_dirty = true;
                save_profile();
            }
            if (f.points > 0) attack_stage_done("score_attack", chapter, f.stage, double(f.points));
            if (single_stage && chapter == single_stage->first && f.stage == single_stage->second) {
                single_stage_result = SingleStageResult{double(f.points), before};
                single_stage_quit_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
            }
        };
        // Bonus points for the time left: into the game's score, credited to
        // the stage just finished.
        const auto bank = [&](bool next_stage) {
            const std::int64_t bonus = clock.clear(next_stage);
            if (bonus <= 0) return std::int64_t{0};
            guest::store32(const_cast<std::uint8_t*>(guest_rdram), 0x80110220u + 0x2AF8,
                           guest::load32(guest_rdram, 0x80110220u + 0x2AF8) + std::uint32_t(bonus / 10));
            score_splits.credit_finished(bonus);
            return bonus;
        };
        const bool new_run = state.chapter != score_chapter || state.score < last_score_attack || state.level < score_level;
        if (new_run && score_chapter >= 0 && state.chapter == score_chapter + 1 && state.level == 1 && score_level >= 10) {
            // The episode's last stage is done: bank its time, and the run is over.
            if (auto f = score_splits.finish()) {
                f->points += bank(false);
                save_stage(score_chapter, *f);
            }
            if (!single_stage) single_stage_quit_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
            run_over = true;
        } else if (new_run) {
            score_splits.reset();
            clock.start(state.layers > 0 ? state.layers : 1);
            timed_out = run_over = false;
            combo_blocks_pending = 0;
        }
        score_chapter = state.chapter;
        score_level = state.level;
        last_score_attack = state.score;
        std::vector<std::int64_t> bests;
        if (const auto r = records.find(state.chapter); r != records.end())
            for (double v : r->second.best_levels) bests.push_back(static_cast<std::int64_t>(v));
        const long long score = static_cast<long long>(state.score) * 10;
        if (auto f = run_over ? std::nullopt : score_splits.update(state.level, score, bests)) {
            const bool next = !(single_stage && f->stage == single_stage->second) && f->stage < 10;
            f->points += bank(next);
            save_stage(state.chapter, *f);
        }
        // The clock runs in play, stops once the stage is cleared, and gains
        // time for combos; at zero the run ends (a game over, as in Story).
        const bool cleared = state.progress >= 1.0f;
        // The stage's layers load as it starts: its own time (2:00 for one
        // layer, 10 s more a layer), set in its first seconds.
        if (state.layers > 0 && clock.played() < 3.0 && !cleared) clock.set_layers(state.layers);
        if (!paused && !cleared && !timed_out && !run_over && elapsed < 1.0) clock.tick(elapsed);
        if (combo_blocks_pending > 0) {
            // The time a combo adds rises from its bricks over the sphere
            // (the owner, 2026-11-15: "+4 s"), then fades.
            const double added = tetrisphere::AttackClock::combo_seconds(combo_blocks_pending, clock.played());
            clock.add_combo(combo_blocks_pending), combo_blocks_pending = 0;
            const int whole = int(std::lround(added));
            if (whole >= 1 && !cleared) {
                LivePopup live;
                live.popup.text = "+" + std::to_string(whole) + " s";
                live.cell_dx = combo_dx;
                live.cell_dy = combo_dy;
                popups.push_back(live);
            }
        }
        // A trophy: two minutes past the stage's own time played in one
        // stage (it takes the time combos add).
        static bool overtime_posted = false;
        if (clock.played() < 1.0) overtime_posted = false;
        if (!overtime_posted && clock.played() >= clock.stage() + tetrisphere::AttackClock::overtime_seconds && !clock.out()) {
            overtime_posted = true;
            post_game_event(tetrisphere::GameEvent{"score_attack_overtime", {}});
        }
        if (clock.out() && !cleared && !timed_out && !run_over) {
            timed_out = true;
            guest::store16(const_cast<std::uint8_t*>(guest_rdram), 0x80110220u + 0x2D7E, 0);  // the game's own game over
        }
        {
            const int left = int(std::ceil(clock.left()));
            view.clock = std::to_string(left / 60) + ":" + (left % 60 < 10 ? "0" : "") + std::to_string(left % 60);
            view.clock_low = left <= 30;
        }
        const auto sv = score_splits.view();
        const auto signed_points = [](long long d) { return (d < 0 ? std::string("\u2212") : std::string("+")) + hud_number(d < 0 ? -d : d); };
        if (single) {
            const auto i = std::size_t(single_stage->second - 1);
            const double b = single_stage_result ? single_stage_result->best_before : i < bests.size() ? double(bests[i]) : 0.0;
            const long long mine = single_stage_result ? static_cast<long long>(single_stage_result->time)
                                                       : sv.rows.empty() ? 0 : sv.rows.back().points;
            view.time_label = single_stage_result ? "Your score" : "Stage score";
            view.time = hud_number(mine);
            if (b > 0.0) {
                view.time_delta = signed_points(mine - static_cast<long long>(b));
                view.time_ahead = mine >= static_cast<long long>(b);
                view.best = "Best " + hud_number(static_cast<long long>(b));
            } else {
                view.best = single_stage_result ? "First score" : "No best yet";
            }
        } else {
            view.time_label = "Score";
            view.time = hud_number(sv.total);
            if (sv.has_delta) view.time_delta = signed_points(sv.net_delta), view.time_ahead = sv.net_delta >= 0;
            for (const auto& row : sv.rows) {
                blockplanet::ui::HudView::Split split;
                split.label = std::to_string(state.chapter) + ":" + std::to_string(row.stage);
                split.time = hud_number(row.points);
                split.current = !row.done;
                if (row.done && row.has_best) split.delta = signed_points(row.delta), split.ahead = row.delta >= 0;
                view.splits.push_back(split);
            }
        }
    } else if (single && play_variant == PlayVariant::TimeAttack) {
        // One stage: your time, the best, and the difference (live).
        const auto sv = splits.view();
        std::vector<double> best = best_of(state.chapter);
        const auto i = std::size_t(single_stage->second - 1);
        const double b = single_stage_result ? single_stage_result->best_before : i < best.size() ? best[i] : 0.0;
        const double mine = single_stage_result ? single_stage_result->time : sv.rows.empty() ? 0.0 : sv.rows.front().time;
        view.time_label = single_stage_result ? "Your time" : "Stage time";
        view.time = hud_time(mine);
        if (b > 0.0) {
            view.time_delta = hud_delta(mine - b);
            view.time_ahead = mine < b;
            view.best = "Best " + hud_time(b);
        } else {
            view.best = single_stage_result ? "First time" : "No best yet";
        }
    } else if (state.mode == 1 && play_variant == PlayVariant::TimeAttack) {
        const auto sv = splits.view();
        view.time = hud_time(sv.total);
        view.time_delta = sv.has_delta ? hud_delta(sv.net_delta) : "";
        view.time_ahead = sv.net_delta < 0.0;
        for (const auto& row : sv.rows) {
            blockplanet::ui::HudView::Split split;
            split.label = std::to_string(state.chapter) + ":" + std::to_string(row.level);
            split.time = hud_time(row.time);
            split.current = !row.done;
            if (row.done && row.has_best) split.delta = hud_delta(row.delta), split.ahead = row.delta < 0.0;
            view.splits.push_back(split);
        }
    } else if (state.mode == 1) {
        // Story: the episode's time, and under it the score with the last
        // five points added (the owner's choice, 2026-11-08).
        view.time = hud_time(splits.view().total);
        // The combo counter: +0x2AE0 counts up as bricks clear, +0x2B14
        // keeps the last combo, +0x2C78 the stage's best.
        const int running = guest::load16(guest_rdram, 0x80110220u + 0x2AE0);
        const int last_combo = guest::load16(guest_rdram, 0x80110220u + 0x2B14);
        view.combo_live = running > 0;
        view.combo = running > 0 ? running : last_combo;
        view.combo_best = guest::load16(guest_rdram, 0x80110220u + 0x2C78);
        // The points added, newest first (five at most). A combo is one entry,
        // its total with its block count ("+30,100 ×21"), growing while it runs;
        // points scored outside a combo show on their own (the owner, 2026-11-15).
        // Special combos, from the game's counts (docs/research): gravity
        // (+0x2AE6, a piece falling into a match mid-combo, func_8009423C) and
        // fuse (+0x2AE8, a piece dropped into a combo still running); magic
        // from a combo of 20 or more (the held magic, +0x702, changing).
        struct Gain { long long points = 0; int blocks = 0; bool live = false; bool gravity = false, fuse = false, magic = false; };
        static int last_gravity = -1, last_fuse = -1, last_magic = -2;
        const int gravity_now = guest::load16(guest_rdram, 0x80110220u + 0x2AE6);
        const int fuse_now = guest::load16(guest_rdram, 0x80110220u + 0x2AE8);
        const int magic_now = std::int8_t(guest::load8(guest_rdram, 0x80110220u + 0x702));
        static long long last_score = -1;
        static std::deque<Gain> gains;
        static bool was_running = false;
        static auto combo_ended = std::chrono::steady_clock::time_point{};
        const auto now_g = std::chrono::steady_clock::now();
        const long long score = static_cast<long long>(state.score) * 10;  // the game keeps tens
        const long long added = last_score >= 0 && score > last_score ? score - last_score : 0;
        if (last_score < 0 || score < last_score) gains.clear(), was_running = false;  // a new game
        if (running > 0) {
            if (gains.empty() || !gains.front().live) gains.push_front({0, 0, true});
            gains.front().points += added;
            gains.front().blocks = running;
            if (last_gravity >= 0 && gravity_now > last_gravity) gains.front().gravity = true;
            if (last_fuse >= 0 && fuse_now > last_fuse) gains.front().fuse = true;
            if (last_magic > -2 && magic_now >= 0 && magic_now != last_magic) gains.front().magic = true;
        } else {
            if (was_running && !gains.empty() && gains.front().live) {
                gains.front().live = false;
                gains.front().blocks = std::max(gains.front().blocks, last_combo);
                combo_ended = now_g;
            }
            if (added > 0) {
                // The combo's own points can land just after it ends: still its.
                if (!gains.empty() && gains.front().blocks > 0 && now_g - combo_ended < std::chrono::milliseconds(600))
                    gains.front().points += added;
                else
                    gains.push_front({added, 0, false});
            }
        }
        was_running = running > 0;
        last_gravity = gravity_now, last_fuse = fuse_now, last_magic = magic_now;
        while (gains.size() > 5) gains.pop_back();
        last_score = score;
        view.story_score = hud_number(score);
        for (const auto& g : gains) {
            if (g.points <= 0 && !g.live) continue;
            std::string line = "+" + hud_number(g.points) + (g.blocks >= 2 ? " \u00D7" + std::to_string(g.blocks) : "");
            // The combo's kind on a smaller line beneath (after a tab).
            std::string kind;
            if (g.gravity) kind = "Gravity combo";
            if (g.fuse) kind += std::string(kind.empty() ? "" : " \u00B7 ") + "Fuse combo";
            if (g.magic) kind += std::string(kind.empty() ? "" : " \u00B7 ") + "+ Magic";
            if (std::getenv("BLOCKPLANET_GAIN_SAMPLE")) line = "+30,100 \u00D721", kind = "Gravity combo \u00B7 + Magic";  // research: the longest
            if (!kind.empty()) line += "\t" + kind;
            view.story_gains.push_back(line);
        }
    } else if (state.mode == 9) {
        // Lines: the stage's play time against the best for it (records
        // "lines", by chapter).
        static int key_now = -1;
        static double stage_time = 0.0;
        if (lines_time_reset) lines_time_reset = false, key_now = -1, stage_time = 0.0;
        auto& record = profile_data.progress.episode_records["lines"][state.chapter];
        const int key = state.chapter * 100 + state.level;
        if (key != key_now) {
            // Cleared: the next stage, or the next chapter's first after stage 10
            // (whose best was never saved before).
            const bool next_chapter = key_now >= 0 && key_now % 100 == 10 && state.level == 1 && state.chapter == key_now / 100 + 1;
            if (key_now >= 0 && (key == key_now + 1 || next_chapter) && stage_time > 0.5) {
                auto& record = profile_data.progress.episode_records["lines"][key_now / 100];
                const int done = key_now % 100;
                // A stage played on its own (Lines' menus): done; back to the menu shortly.
                if (single_stage && play_lines && key_now / 100 == single_stage->first && done == single_stage->second) {
                    const auto i = std::size_t(done - 1);
                    single_stage_result = SingleStageResult{stage_time, i < record.best_levels.size() ? record.best_levels[i] : 0.0};
                    single_stage_quit_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
                }
                if (record.best_levels.size() < std::size_t(done)) record.best_levels.resize(std::size_t(done), 0.0);
                double& best = record.best_levels[std::size_t(done - 1)];
                if (best <= 0.0 || stage_time < best) {
                    best = stage_time;
                    profile_dirty = true;
                    save_profile();
                }
            }
            key_now = key;
            stage_time = 0.0;
        }
        if (!paused && elapsed < 1.0 && state.progress < 1.0f) stage_time += elapsed;
        // Lines' episode ends as Rescue's (CONTINUE, EXIT on the pause row,
        // then the next episode's card): the end menu bridge is told.
        {
            static bool last_cleared = false;
            const bool last_cleared_now = state.level >= 10 && state.progress >= 1.0f;
            if (last_cleared_now && !last_cleared) episode_end_expected = 1;
            else if (state.level < 10 && last_cleared) episode_end_expected = -1;
            last_cleared = last_cleared_now;
        }
        view.time = hud_time(stage_time);
        const auto i = std::size_t(state.level - 1);
        const double best = i < record.best_levels.size() ? record.best_levels[i] : 0.0;
        if (best > 0.0) {
            view.time_delta = hud_delta(stage_time - best);
            view.time_ahead = stage_time < best;
            view.best = "Best " + hud_time(best);
        }
    } else if (state.mode == 5) {
        // Hide & Seek: the level's time against the best for it (-/+), no
        // progress bar (it would hint where the pictures are). Bests per
        // level in records "hide_and_seek" (chapter key 0 until its own
        // chapter is found).
        static int level_now = -1, chapter_now = 0;
        double& level_time = hs_level_time;
        // The game's own Hide & Seek clock (+0x2B0C counts up in frames), which
        // stands still while the goal card is shown.
        const double clock_now = static_cast<std::int32_t>(guest::load32(guest_rdram, 0x80110220u + 0x2B0C)) / 60.0;
        // Bests per episode (its chapter, 0x800E44A0, 1-based here); those
        // kept before the chapter was known (key 0) are episode 1's.
        auto& hs_records = profile_data.progress.episode_records["hide_and_seek"];
        if (const auto old = hs_records.find(0); old != hs_records.end()) {
            if (hs_records.find(1) == hs_records.end()) hs_records[1] = old->second;
            hs_records.erase(old);
            profile_dirty = true;
        }
        const int chapter = guest::load16(guest_rdram, 0x800E44A0) + 1;
        if (state.level != level_now || chapter != chapter_now) {
            // A level cleared: the next one, or level 1 of the next episode after 30.
            const bool cleared = level_now >= 1 && (state.level == level_now + 1 || (level_now == 30 && state.level == 1));
            if (cleared && level_time > 0.5) {
                auto& done = hs_records[chapter_now];
                if (done.best_levels.size() < std::size_t(level_now)) done.best_levels.resize(std::size_t(level_now), 0.0);
                double& best = done.best_levels[std::size_t(level_now - 1)];
                if (best <= 0.0 || level_time < best) {
                    best = level_time;
                    profile_dirty = true;
                }
                hs_run_total += level_time;
                if (level_now == 30 && hs_run_from_first && (done.best_total <= 0.0 || hs_run_total < done.best_total)) {
                    done.best_total = hs_run_total;
                    profile_dirty = true;
                }
                if (profile_dirty) save_profile();
            }
            // An episode's level 1 begins its time; starting part-way, no episode time.
            if (state.level == 1) hs_run_total = 0.0, hs_run_from_first = true;
            else if (!cleared) hs_run_total = 0.0, hs_run_from_first = false;
            level_now = state.level;
            chapter_now = chapter;
        }
        auto& record = hs_records[chapter];
        level_time = std::max(0.0, clock_now);
        view.time_label = "Time";
        view.time = hud_time(level_time);
        const auto i = std::size_t(state.level - 1);
        const double best = i < record.best_levels.size() ? record.best_levels[i] : 0.0;
        if (best > 0.0) {
            view.time_delta = hud_delta(level_time - best);
            view.time_ahead = level_time < best;
            view.best = "Best " + hud_time(best);
        }
    }
    view.progress = state.progress;
    place_hud(view, state);
    // Each popup rises from its cells' place on the sphere: the cursor faces
    // the camera, so a cell dx cells away sits at radius x sin(dx x 2pi/32).
    for (const auto& p : popups) {
        auto popup = p.popup;
        constexpr float step = 6.2831853f / 32.0f;
        const float a = std::clamp(p.cell_dx * step, -1.4f, 1.4f), b = std::clamp(p.cell_dy * step, -1.4f, 1.4f);
        popup.x = view.sphere_radius * std::sin(a);
        popup.y = -view.sphere_radius * std::sin(b);  // the board's rows run upwards on screen
        view.popups.push_back(popup);
    }
    // Minefield on the sphere: the panel counts the mines left instead of
    // the time and progress, and each cell's mark is placed as above.
    if (const auto mines = tetrisphere::minefield_sphere::counter(); !mines.empty()) {
        // Minefield on the sphere: the mines flagged of all of them, and no
        // left panel (no health, magic or pieces to show; the owner's call).
        // The arena's name as the stage, the mines (as the score) and the play
        // time against the arena's best (the owner's requests).
        view.show_left = false;
        view.mode = "Minefield";
        view.score_label = "Mines";
        view.score = mines;
        view.score_best.clear();
        view.time_label = "Time";
        view.time = "0:00";
        view.time_delta.clear();
        view.best.clear();
        if (const auto play = tetrisphere::minefield_sphere::progress()) {
            const auto& arena = tetrisphere::minefield_sphere::arenas()[std::size_t(play->arena)];
            view.stage = arena.name;  // the arena in place of the stage (the owner's request)
            auto& record = profile_data.progress.episode_records["minefield"][play->arena];
            const double best = record.best_total;
            // A win that beats the record (or the first) is kept, once.
            static bool recorded = false, new_best = false;
            if (!play->won && !play->lost) recorded = new_best = false;
            if (play->won && !recorded && !guest_session) {
                recorded = true;
                if (best <= 0.0 || play->seconds < best) {
                    record.best_total = play->seconds;
                    profile_dirty = true;
                    save_profile();
                    new_best = true;
                }
            }
            view.time = hud_time(play->seconds);
            if (new_best) {
                view.time_delta = "New best";
                view.time_ahead = true;
            } else if (best > 0.0) {
                if (play->started) {
                    view.time_delta = hud_delta(play->seconds - best);
                    view.time_ahead = play->seconds <= best;
                }
                view.best = "Best " + hud_time(best);
            } else {
                view.best = "No best yet";
            }
        }
        view.splits.clear();
        view.progress = -1.0f;
        view.story_score.clear();
        view.story_gains.clear();
        view.combo = 0;
    }
    for (const auto& m : tetrisphere::minefield_sphere::marks(guest_rdram)) {
        constexpr float step = 6.2831853f / 32.0f;
        blockplanet::ui::HudView::Mark mark;
        mark.text = m.text;
        mark.kind = m.kind;
        // On the ball: longitude dx, latitude dy (a cell is 1/32 of a turn).
        mark.x = view.sphere_radius * std::cos(m.dy * step) * std::sin(m.dx * step);
        mark.y = -view.sphere_radius * std::sin(m.dy * step);
        view.marks.push_back(std::move(mark));
    }
    show_hud(std::move(view));
}

// Research aid: BLOCKPLANET_WATCH="0x800E44A8:2,0x80103958:4" logs each
// guest value (1, 2 or 4 bytes) when it changes, as a watch event.
void poll_watches() {
    struct Watch {
        std::uint32_t address;
        int size;
        std::int64_t last;
    };
    static std::vector<Watch> watches = [] {
        std::vector<Watch> list;
        const char* spec = std::getenv("BLOCKPLANET_WATCH");
        if (spec == nullptr) return list;
        std::string text(spec);
        std::size_t position = 0;
        while (position < text.size()) {
            const auto comma = std::min(text.find(',', position), text.size());
            const auto item = text.substr(position, comma - position);
            const auto colon = item.find(':');
            const auto address = static_cast<std::uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
            const int size = colon == std::string::npos ? 4 : std::atoi(item.c_str() + colon + 1);
            if (address != 0 && (size == 1 || size == 2 || size == 4)) list.push_back({address, size, INT64_MIN});
            position = comma + 1;
        }
        return list;
    }();
    for (auto& watch : watches) {
        const std::int64_t value = watch.size == 1 ? guest::load8(guest_rdram, watch.address)
                                   : watch.size == 2 ? guest::load16(guest_rdram, watch.address)
                                                     : guest::load32(guest_rdram, watch.address);
        if (value == watch.last) continue;
        watch.last = value;
        static const auto started = std::chrono::steady_clock::now();
        std::fprintf(stdout, "{\"event\":\"watch\",\"address\":\"0x%08X\",\"value\":%lld,\"ms\":%lld}\n", watch.address,
                     static_cast<long long>(value),
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()));
        std::fflush(stdout);
    }
}

// Experiment (docs/research/blockplanet-game-state.md, puzzle border): with
// BLOCKPLANET_TEST_TYPE47=1, the first time Rescue is in play the blocks in
// every other 4-column stripe become type 47 (value 423), Puzzle's border.
void poll_type47_experiment() {
    static const bool enabled = [] {
        const char* value = std::getenv("BLOCKPLANET_TEST_TYPE47");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    static bool done = false;
    if (!enabled || done) return;
    auto* rdram = const_cast<std::uint8_t*>(guest_rdram);
    if (guest::load16(rdram, 0x800E121C) != -1 || guest::load16(rdram, 0x800E44A8) != 1) return;
    const std::uint32_t values = guest::load32(rdram, 0x800E44D0);
    if (values < 0x80000000u) return;
    int changed = 0;
    for (int layer = 1; layer < 9; ++layer)
        for (int cell = 0; cell < 1024; ++cell) {
            if (((cell & 31) / 4) % 2 != 0) continue;
            const std::uint32_t address = values + 2u * static_cast<std::uint32_t>(layer * 1024 + cell);
            if (guest::load16(rdram, address) == 0) continue;
            guest::store16(rdram, address, 423);
            ++changed;
        }
    done = true;
    std::fprintf(stdout, "{\"event\":\"type47_experiment\",\"cells\":%d}\n", changed);
    std::fflush(stdout);
}

// Research aid: with BLOCKPLANET_POKE_FILE set, lines "0xADDRESS:SIZE=VALUE"
// written to that file are stored into guest memory, and lines "cmd:NAME" run
// a menu command, then the file is removed.
void poll_pokes() {
    static const char* path = std::getenv("BLOCKPLANET_POKE_FILE");
    if (path == nullptr) return;
    std::ifstream in(path);
    if (!in) return;
    auto* rdram = const_cast<std::uint8_t*>(guest_rdram);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("cmd:", 0) == 0) {  // a menu command, as if chosen
            blockplanet::ui::send_command(line.substr(4));
            continue;
        }
        unsigned long address = 0, size = 0;
        long value = 0;
        if (std::sscanf(line.c_str(), "%lx:%lu=%ld", &address, &size, &value) != 3) continue;
        if (size == 1) guest::store8(rdram, static_cast<std::uint32_t>(address), static_cast<std::uint8_t>(value));
        else if (size == 2) guest::store16(rdram, static_cast<std::uint32_t>(address), static_cast<std::int16_t>(value));
        else if (size == 4) guest::store32(rdram, static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(value));
        std::fprintf(stdout, "{\"event\":\"poke\",\"address\":\"0x%08lX\",\"value\":%ld}\n", address, value);
    }
    in.close();
    std::remove(path);
    std::fflush(stdout);
}

// Research: BLOCKPLANET_RESEARCH_HOLE=n, n seconds into a Story stage,
// uncovers an 8 x 8 square of the core round the cursor (every layer of the
// cells cleared, the core's face shown), as digging down does, to see what
// the game draws in the core when the friend is reached.
void research_hole(std::uint8_t* rdram) {
    static const char* setting = std::getenv("BLOCKPLANET_RESEARCH_HOLE");
    if (setting == nullptr || rdram == nullptr) return;
    static auto since = std::chrono::steady_clock::time_point{};
    static bool done = false;
    const bool playing = guest::load16(rdram, 0x800E121C) == -1 && guest::load16(rdram, 0x800E44A8) == 1;
    if (!playing) {
        since = {};
        return;
    }
    if (since == std::chrono::steady_clock::time_point{}) since = std::chrono::steady_clock::now();
    // After the hole: the core values round it, every second for 15 seconds.
    static int logged = 0;
    static auto last_log = std::chrono::steady_clock::time_point{};
    static int hx = 0, hy = 0;
    if (done && logged < 15 && std::chrono::steady_clock::now() - last_log > std::chrono::seconds(1)) {
        last_log = std::chrono::steady_clock::now();
        ++logged;
        const std::uint32_t v = guest::load32(rdram, 0x800E44D0u);
        std::string rows;
        for (int dy = -6; dy < 6; ++dy) {
            for (int dx = -6; dx < 6; ++dx)
                rows += std::to_string(guest::load16(rdram, v + 2u * std::uint32_t(((hy + dy) & 31) * 32 + ((hx + dx) & 31)))) + ",";
            rows += "|";
        }
        std::fprintf(stdout, "{\"event\":\"research_core\",\"n\":%d,\"level\":%d,\"values\":\"%s\"}\n", logged,
                     guest::load16(rdram, 0x800E4498), rows.c_str());
        std::fflush(stdout);
    }
    if (done || std::chrono::steady_clock::now() - since < std::chrono::seconds(std::atoi(setting))) return;
    done = true;
    hx = 8 + guest::load16(rdram, 0x80110220u + 0x2B2A), hy = 8 + guest::load16(rdram, 0x80110220u + 0x2B2E);
    const std::uint32_t values = guest::load32(rdram, 0x800E44D0u), masks = guest::load32(rdram, 0x800E44D4u),
                        entries = guest::load32(rdram, 0x800E44D8u), columns = guest::load32(rdram, 0x800E44DCu);
    const int cx = 8 + guest::load16(rdram, 0x80110220u + 0x2B2A), cy = 8 + guest::load16(rdram, 0x80110220u + 0x2B2E);
    for (int dy = -8; dy < 8; ++dy)
        for (int dx = -8; dx < 8; ++dx) {
            const int c = ((cy + dy) & 31) * 32 + ((cx + dx) & 31);
            for (int layer = 1; layer < 9; ++layer) {
                guest::store16(rdram, values + 2u * std::uint32_t(layer * 1024 + c), 0);
                guest::store8(rdram, masks + std::uint32_t(layer * 1024 + c), 0);
                guest::store32(rdram, entries + 4u * std::uint32_t(layer * 1024 + c), 0);
            }
            guest::store8(rdram, masks + std::uint32_t(c), 80);
            guest::store8(rdram, columns + std::uint32_t(c), 0);
        }
    std::fprintf(stdout, "{\"event\":\"research_hole\",\"at\":[%d,%d]}\n", cx, cy);
    std::fflush(stdout);
}

// Research (the test harness): a memory diff around an event.
// "research_diff_start" learns which bytes change by themselves (for 2 s);
// "research_diff_mark" then logs, for 3 s, each other byte the first time it
// changes (address, old, new, ms after the mark). Ranges: player 1's context
// and the game's globals.
std::optional<std::chrono::steady_clock::time_point> diff_start, diff_mark;
void research_diff() {
    if (!diff_start || guest_rdram == nullptr) return;
    struct Range { std::uint32_t from, to; };
    static const Range ranges[] = {{0x80110220u, 0x80113800u}, {0x800DF000u, 0x800E5000u}, {0x8013C000u, 0x80140000u}, {0x80100000u, 0x80104000u}};
    static std::vector<std::uint8_t> last, noisy, logged;
    std::size_t total = 0;
    for (const auto& r : ranges) total += r.to - r.from;
    static std::optional<std::chrono::steady_clock::time_point> seeded_for;
    if (last.size() != total) last.assign(total, 0), noisy.assign(total, 0), logged.assign(total, 0);
    if (seeded_for != diff_start) {
        // A new start: take the current memory as the baseline first.
        seeded_for = diff_start;
        std::fill(noisy.begin(), noisy.end(), 0);
        std::fill(logged.begin(), logged.end(), 0);
        std::size_t k = 0;
        for (const auto& r : ranges)
            for (std::uint32_t a = r.from; a < r.to; ++a, ++k) last[k] = guest::load8(guest_rdram, a);
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool learning = !diff_mark && now - *diff_start < std::chrono::seconds(2);
    const bool watching = diff_mark && now - *diff_mark < std::chrono::seconds(3);
    std::size_t i = 0;
    for (const auto& r : ranges)
        for (std::uint32_t a = r.from; a < r.to; ++a, ++i) {
            const std::uint8_t v = guest::load8(guest_rdram, a);
            if (v != last[i]) {
                if (learning) noisy[i] = 1;
                else if (watching && !noisy[i] && !logged[i]) {
                    logged[i] = 1;
                    std::fprintf(stderr, "DIFF %lld %08X %02X->%02X\n",
                                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(now - *diff_mark).count(), a, last[i], v);
                }
            }
            last[i] = v;
        }
    if (diff_mark && !watching) diff_start.reset(), diff_mark.reset();
}

void poll_game_events() {
    if (guest_rdram == nullptr) return;
    research_diff();
    poll_watches();
    poll_pokes();
    poll_type47_experiment();
    if (game_option_applier) game_option_applier->apply(const_cast<std::uint8_t*>(guest_rdram));
    tetrisphere::minefield_sphere::poll(const_cast<std::uint8_t*>(guest_rdram));
    research_hole(const_cast<std::uint8_t*>(guest_rdram));
    const auto state = read_game_state(guest_rdram);
    const auto now = std::chrono::steady_clock::now();
    if (profile_recording && state.scene == -1 && last_play_poll != std::chrono::steady_clock::time_point{}) {
        // Time in play, in whole seconds (the remainder carries over).
        static std::chrono::steady_clock::duration carried{};
        carried += now - last_play_poll;
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(carried);
        if (seconds.count() > 0) {
            profile_data.progress.statistics.seconds_played += seconds.count();
            carried -= seconds;
            profile_dirty = true;
        }
    }
    last_play_poll = now;
    for (const auto& event : game_event_watcher.update(state)) {
        if (log_game_events) {
            std::string fields;
            for (const auto& [key, value] : event.fields) {
                fields += ",\"" + key + "\":";
                fields += std::holds_alternative<std::int64_t>(value)
                    ? std::to_string(std::get<std::int64_t>(value))
                    : "\"" + std::get<std::string>(value) + "\"";
            }
            std::fprintf(stdout, "{\"event\":\"game_event\",\"name\":\"%s\"%s}\n",
                         event.name.c_str(), fields.c_str());
            std::fflush(stdout);
        }
        if (event.name == "training_completed" && !chapter_zero_lesson.empty() &&
            std::get<std::string>(event.fields.at("lesson")) == chapter_zero_lesson) {
            continue_mode = chapter_zero_lesson == "basics" ? PendingLaunch{LaunchMode::Rescue, std::pair{1, 1}}
                                                            : PendingLaunch{LaunchMode::Puzzle, std::nullopt};
            chapter_zero_lesson.clear();
        }
        post_game_event(event);
        // Minefield's games leave no Story or Casual progress behind.
        if (profile_recording && !tetrisphere::minefield_sphere::armed()) {
            profile_dirty = true;
            if (record_game_event(profile_data.progress, event)) save_profile();  // milestones at once
        }
    }
}
const char* perf_present_mode = "console";

std::atomic<ControllerFamily> active_family{ControllerFamily::Keyboard};
std::atomic<ControllerFamily> active_p2_family{ControllerFamily::Keyboard};
Settings active_settings{};

// The game's picture in the window (pillarboxed or letterboxed by the
// Picture setting): position and size in window pixels.
struct PictureFrame {
    float window_w = 0, window_h = 0;
    float x = 0, y = 0, w = 0, h = 0;
    float aspect = 4.0f / 3.0f;
};
std::optional<PictureFrame> picture_frame() {
    int width = 0, height = 0;
    if (window != nullptr) SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) return std::nullopt;
    PictureFrame f;
    f.window_w = float(width);
    f.window_h = float(height);
    const float window_aspect = f.window_w / f.window_h;
    switch (active_settings.graphics.aspect) {
    case GraphicsAspect::Expand16x9: f.aspect = 16.0f / 9.0f; break;
    case GraphicsAspect::Original4x3: f.aspect = 4.0f / 3.0f; break;
    default: f.aspect = std::max(window_aspect, 4.0f / 3.0f); break;
    }
    f.h = window_aspect >= f.aspect ? f.window_h : f.window_w / f.aspect;
    f.w = f.h * f.aspect;
    f.x = (f.window_w - f.w) / 2.0f;
    f.y = (f.window_h - f.h) / 2.0f;
    return f;
}

// The game's text in BlockPlanet's font (pause_draw_hooks.cpp keeps the
// strings the game drew): placed where the game placed them in its 4:3 frame,
// joined to whatever HUD is shown, then handed to the UI.
// Training's pages (docs/TRAINING_REDESIGN.md, step 2). A lesson's text is a
// string in the game's memory; the instructor's current page starts at
// 0x80103958. Codes: -0 a line break, -1 the next word in yellow, -2 a page
// break (waiting for A), -5 a demonstration ("watch", then on by itself), -3
// and -4 a scripted action as the page opens (an arrow, Wheels moving).
// The game's key terms (the owner's choice), highlighted the same everywhere:
// a phrase of words (as the game writes them, in capitals) and its term.
// Longer phrases first, so "combo counter" wins over "combo".
const std::vector<std::pair<std::vector<std::string>, std::string>>& glossary_table() {
    static const std::vector<std::pair<std::vector<std::string>, std::string>> table = [] {

    std::vector<std::pair<std::vector<std::string>, std::string>> g;
    const auto add = [&](std::initializer_list<const char*> phrase, const char* term) {
        g.push_back({std::vector<std::string>(phrase.begin(), phrase.end()), term});
    };
    // Longer phrases first, so "combo counter" wins over "combo".
    add({"COMBO", "COUNTER"}, "combo counter"); add({"POWER", "COMBO"}, "power combo");
    add({"POWER", "COMBOS"}, "power combo"); add({"GRAVITY", "COMBO"}, "gravity combo");
    add({"GRAVITY", "COMBOS"}, "gravity combo"); add({"FUSE", "COMBO"}, "fuse combo");
    add({"FUSE", "COMBOS"}, "fuse combo"); add({"POWER", "BRICK"}, "power brick");
    add({"POWER", "BRICKS"}, "power brick"); add({"CRYSTAL", "BRICK"}, "crystal brick");
    add({"CRYSTAL", "BRICKS"}, "crystal brick"); add({"X", "COUNT"}, "x count");
    add({"MAGIC", "BOX"}, "magic box");
    add({"SHADOW"}, "shadow"); add({"SLIDE"}, "slide"); add({"SLIDING"}, "slide");
    add({"COMBO"}, "combo"); add({"COMBOS"}, "combo"); add({"FUSE"}, "fuse");
    add({"MAGIC"}, "magic"); add({"WILDCARD"}, "wildcard"); add({"HEARTS"}, "hearts");
    add({"LIVES"}, "lives"); add({"CORE"}, "core");
    add({"DROPS"}, "drops"); add({"SLIDES"}, "slides"); add({"DRILL"}, "drill"); add({"DRILLS"}, "drill");
    return g;
    }();
    return table;
}

// A term's heading, over the page that introduces it.
std::string glossary_heading(const std::string& term) {
    static const std::map<std::string, std::string> names = {
        {"shadow", "Your Shadow"}, {"slide", "Sliding"}, {"combo", "Combos"}, {"combo counter", "Combo Counter"},
        {"power combo", "Power Combos"}, {"gravity combo", "Gravity Combos"}, {"fuse combo", "Fuse Combos"},
        {"fuse", "Fuse Combos"}, {"power brick", "Power Bricks"}, {"crystal brick", "Crystal Bricks"},
        {"x count", "The X Count"}, {"magic", "Magic"}, {"magic box", "The Magic Box"}, {"wildcard", "The Wildcard"},
        {"hearts", "Lives"}, {"lives", "Lives"}, {"core", "The Core"}, {"drops", "Drops and Slides"},
        {"slides", "Drops and Slides"}, {"drill", "Drills"}};
    const auto found = names.find(term);
    return found != names.end() ? found->second : term;
}

// The glossary terms in a card's text, in order of first mention.
std::vector<std::string> glossary_terms(std::string text) {
    text = tetrisphere::fix_wording(text);
    std::vector<std::string> words;
    std::string word;
    for (std::size_t c = 0; c <= text.size(); ++c) {
        if (c < text.size() && text[c] == '-' && c + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[c + 1]))) {
            if (text[c + 1] == '0' && !word.empty()) words.push_back(word), word.clear();  // a line break
            ++c;
            continue;
        }
        if (c == text.size() || text[c] == ' ') {
            if (!word.empty()) words.push_back(word), word.clear();
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(text[c]))) word += char(std::toupper(static_cast<unsigned char>(text[c])));
    }
    std::vector<std::string> terms;
    for (std::size_t i = 0; i < words.size();) {
        bool matched = false;
        for (const auto& [phrase, term] : glossary_table()) {
            if (i + phrase.size() > words.size()) continue;
            bool same = true;
            for (std::size_t k = 0; k < phrase.size() && same; ++k) same = words[i + k] == phrase[k];
            if (!same) continue;
            if (std::find(terms.begin(), terms.end(), term) == terms.end()) terms.push_back(term);
            i += phrase.size();
            matched = true;
            break;
        }
        if (!matched) ++i;
    }
    return terms;
}

struct TrainingPage {
    std::uint32_t start = 0, end = 0;  // the page's text, up to its break code
    char ends = 0;                     // '2', '5', or 0 (the string's end)
    bool scripted = false;             // opens with -3 (an action on screen)
    bool skip = false;                 // "press down or A to page through the training text"
};

std::vector<TrainingPage> training_pages(std::uint32_t at) {
    std::vector<TrainingPage> pages;
    if (guest_rdram == nullptr || at < 0x80000000u || at >= 0x80800000u) return pages;
    std::uint32_t s = at, e = at;
    for (int guard = 0; guard < 8000 && guest::load8(guest_rdram, s - 1) != 0; ++guard) --s;
    for (int guard = 0; guard < 8000 && guest::load8(guest_rdram, e) != 0; ++guard) ++e;
    std::uint32_t from = s;
    for (std::uint32_t i = s; i + 1 < e; ++i) {
        const char c = char(guest::load8(guest_rdram, i)), d = char(guest::load8(guest_rdram, i + 1));
        if (c == '-' && (d == '2' || d == '5')) {
            pages.push_back({from, i, d});
            from = i + 2;
            ++i;
        }
    }
    pages.push_back({from, e, 0});
    for (auto& page : pages) {
        std::string text;
        for (std::uint32_t i = page.start; i < page.end; ++i) text += char(guest::load8(guest_rdram, i));
        for (std::size_t i = 0; i + 1 < text.size() && text[i] == '-' && std::isdigit(static_cast<unsigned char>(text[i + 1])); i += 2)
            if (text[i + 1] == '3') page.scripted = true;
        page.skip = text.find("TO PAGE THROUGH THE TRAINING TEXT") != std::string::npos;
    }
    return pages;
}

// The pages shown together with the current one: a page that only continues
// the text (after a -2 break, opening with no scripted action) joins the one
// before it. Returns the first and last page of the group, and the current.
struct TrainingGroup { std::vector<TrainingPage> pages; std::size_t first = 0, last = 0, current = 0; };
std::optional<TrainingGroup> training_group() {
    const std::uint32_t at = guest::load32(guest_rdram, 0x80103958);
    TrainingGroup g;
    g.pages = training_pages(at);
    if (g.pages.empty()) return std::nullopt;
    std::size_t k = 0;
    for (std::size_t i = 0; i < g.pages.size(); ++i)
        if (g.pages[i].start <= at) k = i;
    const auto joins = [&](std::size_t i) {  // page i joins page i - 1
        return g.pages[i - 1].ends == '2' && !g.pages[i].scripted && !g.pages[i].skip && !g.pages[i - 1].skip;
    };
    g.current = g.first = g.last = k;
    while (g.first > 0 && joins(g.first)) --g.first;
    while (g.last + 1 < g.pages.size() && joins(g.last + 1)) ++g.last;
    return g;
}

// A to press for the player (Training): through pages already shown in a
// group, and past the paging hint. Pressed in short pulses until the page moves.
std::atomic<std::uint32_t> training_inject{0};
void update_training_presses(bool card_shown) {
    static std::uint32_t pressing_page = 0;
    static auto started = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const bool training = guest_rdram != nullptr && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                          guest::load16(guest_rdram, 0x800E44A8) == 3 && guest::load16(guest_rdram, 0x800E1228) < 0;
    // A long card not yet scrolled to its end: the game is held (A does not
    // move on) until the player has scrolled down.
    if (card_shown && blockplanet::ui::card_needs_scroll() && guest_rdram != nullptr &&
        guest::load16(guest_rdram, 0x800E121C) == -1 && guest::load16(guest_rdram, 0x800E1228) < 0) {
        pressing_page = 0;
        training_inject.store(0x10000u);
        return;
    }
    std::optional<TrainingGroup> g;
    if (training && card_shown) g = training_group();
    bool want = false;
    if (g) {
        const auto& page = g->pages[g->current];
        // Past pages shown already: the player pressed A at the group's first;
        // a demonstration at the group's end runs by itself.
        want = page.skip || (g->current > g->first && !(g->current == g->last && page.ends == '5'));
    }
    if (!want) {
        pressing_page = 0;
        training_inject.store(0);
        return;
    }
    const std::uint32_t at = g->pages[g->current].start;
    if (at != pressing_page) {
        pressing_page = at;
        started = now;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
    if (ms > 4000) {  // the page does not move: leave it to the player
        training_inject.store(0);
        return;
    }
    training_inject.store(0x10000u | (ms % 300 < 120 ? 0x8000u : 0u));
}

// A rules or goal card's whole text, from its heading's first word: the
// game keeps each card as one string ("-1MULTI-0DIG THROUGH..."), in its
// code and data (0x800E0000 to 0x80100000). Found once per word.
std::optional<std::string> find_card_text(const std::string& first_word) {
    if (guest_rdram == nullptr || first_word.size() < 2) return std::nullopt;
    static std::map<std::string, std::optional<std::string>> found;
    if (const auto it = found.find(first_word); it != found.end()) return it->second;
    const std::string needle = "-1" + first_word;
    std::optional<std::string> result;
    for (std::uint32_t a = 0x800E0001u; a < 0x80100000u - needle.size() && !result; ++a) {
        if (guest::load8(guest_rdram, a - 1) != 0) continue;
        bool match = true;
        for (std::size_t k = 0; k < needle.size() && match; ++k) match = char(guest::load8(guest_rdram, a + std::uint32_t(k))) == needle[k];
        if (!match) continue;
        // The whole word: what follows is a code, a space or the end.
        const char next = char(guest::load8(guest_rdram, a + std::uint32_t(needle.size())));
        if (std::isalpha(static_cast<unsigned char>(next))) continue;
        std::string text;
        for (std::uint32_t k = 0; k < 2000; ++k) {
            const char ch = char(guest::load8(guest_rdram, a + k));
            if (ch == 0) break;
            text += ch;
        }
        // Only a card's text: one with a -0 line break after its heading, and no page breaks.
        if (text.find("-0") != std::string::npos && text.find("-2") == std::string::npos) result = text;
    }
    found[first_word] = result;
    return result;
}

// Hide & Seek's goal cards: the owner's titles for the game's internal names
// (docs/DECISIONS-2026-11-07.md). Empty: keep the card's own heading.
std::string hide_and_seek_title(const std::string& card) {
    static const std::pair<const char*, const char*> titles[] = {
        {"-1MULTI-0", "Any Picture"}, {"-1DRILL-0", "Five Drills"}, {"-1HIDE -1AND -1SEEK-0", "Help Them Escape"},
        {"-1SHIFT-0", "Four Parts"}, {"-1UNIQUE-0", "Unique Shape"}, {"-1BRICK-0", "Brick Drop"},
        {"-1CONNECT-0", "Connected Pictures"}, {"-1TOWER-0", "Tower"}, {"-1CRYSTAL -1TOWER-0", "Crystal Tower"}};
    for (const auto& [start, title] : titles)
        if (card.rfind(start, 0) == 0) return title;
    return {};
}

// Puzzle: the drops and slides the puzzle in play gives (context +0x25C6 and
// +0x25C8 count down from them: the most seen since it began).
int puzzle_limit_drops = 0, puzzle_limit_slides = 0;
void track_puzzle_limits() {
    static int puzzle = -1;
    if (guest_rdram == nullptr || guest::load16(guest_rdram, 0x800E44A8) != 4 || guest::load16(guest_rdram, 0x800E121C) != -1) {
        puzzle = -1;
        return;
    }
    const int now = guest::load16(guest_rdram, 0x800E4498);
    if (now != puzzle) puzzle = now, puzzle_limit_drops = puzzle_limit_slides = 0;
    puzzle_limit_drops = std::max<int>(puzzle_limit_drops, std::int16_t(guest::load16(guest_rdram, 0x80110220u + 0x25C6)));
    puzzle_limit_slides = std::max<int>(puzzle_limit_slides, std::int16_t(guest::load16(guest_rdram, 0x80110220u + 0x25C8)));
}
// Puzzle's goal (the same in every puzzle) and this puzzle's limits.
std::string puzzle_goal() {
    const auto count = [](int n, const char* one, const char* many) { return std::to_string(n) + "\u00A0" + (n == 1 ? one : many); };
    return "Clear every brick inside the frame, using only the drops and slides given. Your brick fits any shape. This puzzle: " +
           count(puzzle_limit_drops, "drop", "drops") + ", " + count(puzzle_limit_slides, "slide", "slides") + ".";
}
// A new Hide & Seek run (launched from the menus; a restart is the same run):
// its first level plays the intro.
bool hide_and_seek_new_run = false;
// Hide & Seek's goal in full, for the pause screen (the card's text as
// BlockPlanet rewords it, under the owner's title for the goal).
std::string hide_and_seek_goal_text(int goal) {
    switch (goal) {
    case 1: return "Any Picture: uncover any picture to win.";
    case 2: return "Five Drills: five pink drills poke out of the sphere. Dig under them to find a picture, then uncover it to win.";
    case 3: return "Help Them Escape: uncover enough of the core for your friend to escape, then catch them!";
    case 4: return "Four Parts: four drills each hide part of the core picture. Uncover all four, then slide them together with the blue 2\u00D72 brick.";
    case 5: return "Unique Shape: uncover the unique shape under the drill.";
    case 7: return "Brick Drop: clear the bricks under a red brick until it falls to the core.";
    case 9: return "Connected Pictures: under the drill is a picture with lines. Follow them to find and uncover every picture.";
    case 10: return "Tower: a picture lies under the tower. Clear around it to show the whole picture.";
    case 11: return "Crystal Tower: uncover the picture under the crystal tower, without breaking the tower.";
    default: return "";
    }
}

// Hide & Seek in the shared pattern (docs/MODE_MATRIX.md; the owner,
// 2026-11-15): its intro animation plays once per start (the run's first
// level; later levels skip it, as A does), and each goal's card shows the
// first time a profile meets that goal; after that it closes by itself and
// the goal is on the pause screen. Returns the buttons to press (A) for this
// pass, or 0. BLOCKPLANET_HS_SKIPS=0 leaves both as the game shows them.
std::uint16_t hide_and_seek_skips() {
    static const bool enabled = [] {
        const char* text = std::getenv("BLOCKPLANET_HS_SKIPS");
        return text == nullptr || std::string_view(text) != "0";
    }();
    using clock = std::chrono::steady_clock;
    static int level_now = -1, levels_in_run = 0;
    static std::optional<clock::time_point> level_since, card_since;
    static bool card_handled = false, card_first = false;
    static std::uint32_t clock_at_level = 0;
    if (!enabled || guest_rdram == nullptr) return 0;
    const auto now = clock::now();
    const bool in_mode = guest::load16(guest_rdram, 0x800E44A8) == 5 && guest::load16(guest_rdram, 0x800E121C) == -1;
    if (hide_and_seek_new_run) hide_and_seek_new_run = false, levels_in_run = 0, level_now = -1;
    if (!in_mode) {
        level_now = -1;  // out of play (a restart, the menus): the level starts afresh
        return 0;
    }
    const int level = guest::load16(guest_rdram, 0x800E4498);
    if (level != level_now) {
        level_now = level;
        ++levels_in_run;
        level_since = now;
        card_since.reset();
        card_handled = false;
        card_first = false;
        // The level's clock (+0x2B0C) stands still through the intro and the
        // card; it moves once play starts.
        clock_at_level = guest::load32(guest_rdram, 0x80110220u + 0x2B0C);
    }
    if (guest::load16(guest_rdram, 0x800E1228) >= 0) return 0;  // paused
    const auto box = blockplanet::recent_card_box();
    const bool card = box && box->kind == 1;
    const auto pulse = [&](clock::time_point from) -> std::uint16_t {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - from).count();
        return ms % 350 < 100 ? 0x8000 : 0;
    };
    if (card) {
        if (!card_since) card_since = now;
        if (!card_handled) {
            card_handled = true;
            const int goal = guest::load16(guest_rdram, 0x800E44A4);
            const std::string key = "hide_and_seek:" + std::to_string(goal);
            if (profile_data.progress.start_cards_seen.insert(key).second) {
                profile_dirty = true;
                save_profile();
                card_first = true;  // seen now for the first time: the player closes it
            }
        }
        if (card_first) return 0;
        // A card already seen: close it (after a moment, so it is not a flash).
        if (card_since && now - *card_since > std::chrono::milliseconds(400)) return pulse(*card_since);
        return 0;
    }
    if (card_since) return 0;  // the card is closing: play has started
    // Later levels of a run: skip the intro (A, as the player can), until the
    // card comes; only while the level's clock has not started (+0x2B0C).
    const bool clock_started = guest::load32(guest_rdram, 0x80110220u + 0x2B0C) != clock_at_level;
    // One press a moment in, and once more 2.5 s later if the card has not
    // come (pressing on could close a card that has just appeared, before it
    // is seen here).
    if (levels_in_run >= 2 && !clock_started && level_since) {
        const auto in = now - *level_since;
        const auto press = [&](std::chrono::milliseconds at) {
            return in >= at && in < at + std::chrono::milliseconds(100);
        };
        if (press(std::chrono::milliseconds(800)) || press(std::chrono::milliseconds(3300))) return 0x8000;
    }
    return 0;
}

// Lines' rules card (two pages) shows the first time a profile plays Lines;
// after that it closes by itself (A, through both pages) and the rules are on
// the pause screen. Returns the buttons to press, or 0.
std::uint16_t lines_card_once() {
    using clock = std::chrono::steady_clock;
    static std::optional<clock::time_point> card_since, card_gone;
    static bool first = false;
    if (guest_rdram == nullptr || guest::load16(guest_rdram, 0x800E44A8) != 9 || guest::load16(guest_rdram, 0x800E121C) != -1) {
        card_since.reset();
        return 0;
    }
    const auto now = clock::now();
    const auto box = blockplanet::recent_card_box();
    if (box && box->kind == 1) {
        card_gone.reset();
        if (!card_since) {
            card_since = now;
            first = profile_data.progress.start_cards_seen.insert("lines").second;
            if (first) {
                profile_dirty = true;
                save_profile();
            }
        }
        if (first || now - *card_since < std::chrono::milliseconds(400)) return 0;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - *card_since).count();
        return ms % 350 < 100 ? 0x8000 : 0;
    }
    // Between the card's pages the box goes for a moment: the same card.
    if (card_since) {
        if (!card_gone) card_gone = now;
        if (now - *card_gone > std::chrono::seconds(2)) card_since.reset(), card_gone.reset();
    }
    return 0;
}

// The pause menu's How to Play: BlockPlanet's own short guide to the mode
// (the owner's choice: to read, no demonstration), naming the player's own
// buttons. One paragraph per line.
std::string how_to_play(int mode) {
    const auto family = active_family.load();
    const auto bindings = controller_bindings();
    const std::string a = logical_action_label(family, bindings, LogicalAction::Confirm);
    const std::string b = logical_action_label(family, bindings, LogicalAction::Cancel);
    const std::string magic = magic_control_label(family, magic_control());
    const std::string move = family == ControllerFamily::Keyboard ? "the arrow keys" : "the stick or d-pad";
    const std::string basics =
        "Move the shadow over the sphere with " + move + ". It has the shape of your next brick.\n"
        "Drop your brick (" + a + ") onto bricks of the same shape. Two together are ready; a third breaks them.\n"
        "To slide a brick, line the shadow up with it, hold " + b + " and move.\n"
        "Dropping where no two bricks match costs a heart.\n";
    switch (mode) {
    case 1: {
        std::string text = "Dig through the sphere to its core: clear a big enough area to finish the stage.\n" + basics +
                           "Three or more bricks breaking together make a combo. Smaller combos give power bricks; 20 or more give magic, used with " +
                           magic + ".";
        if (play_variant == PlayVariant::TimeAttack) text += "\nTime Attack: finish each stage as fast as you can. Your splits show against your best.";
        if (play_variant == PlayVariant::ScoreAttack) text += "\nScore Attack: the clock counts down. Combos add time, and time left becomes points.";
        if (play_variant == PlayVariant::Casual) text += "\nCasual: no game over. Lost hearts come back.";
        return text;
    }
    case 4:
        return "Clear every brick on the sphere with only the drops and slides shown.\n"
               "Your brick is a wildcard: it fits any shape.\n"
               "To slide a brick, line the shadow up with it, hold " + b + " and move.\n"
               "Zoom in and out with the shoulder buttons. Start the puzzle again from the pause menu.";
    case 5: {
        const std::string goal = hide_and_seek_goal(guest::load16(guest_rdram, 0x800E44A4));
        return "Uncover the hidden picture to win" + (goal.empty() ? std::string(".") : ": " + goal + ".") + "\n" + basics +
               "Dig carefully: the goal shows on the right of the screen.";
    }
    case 6:
        return "Uncover the core area before your opponent does.\n" + basics +
               "Your combos send dark bricks to your opponent's sphere. Dark bricks break only as part of a combo.";
    default:
        return "Line up three bricks in a row, across or down, to remove them.\n"
               "Clear a big enough area of the core for your friend to escape.\n"
               "Move the shadow over the sphere with " + move + ", and drop your brick with " + a + ".\n"
               "To slide a brick, line the shadow up with it, hold " + b + " and move.\n"
               "Dropping where it makes no line costs a heart.";
    }
}

void publish_game_text() {
    auto view = last_hud_view;
    auto texts = blockplanet::recent_game_text();
    // Puzzle's drops and slides counters are drawn in the cards' font too (white
    // digits in the rings): they are not card text, so they are laid out as
    // the menus' text is, at their own place.
    for (auto& t : texts)
        if (t.font == 1 && t.r == 255 && t.g == 255 && t.b == 255 && !t.text.empty() &&
            t.text.find_first_not_of("0123456789") == std::string::npos)
            t.font = 0;
    // Hint lines the owner's review rewrote (the game-over hint): the new
    // wording, or the line dropped when the rewrite is empty.
    for (auto& t : texts)
        if (t.font == 1)
            if (const auto rewrite = tetrisphere::text_rewrite(t.text)) t.text = *rewrite;
    texts.erase(std::remove_if(texts.begin(), texts.end(), [](const blockplanet::GameText& t) { return t.text.empty(); }), texts.end());
    // The game's end menus (game over: Try Again, Exit, Training; Versus' end:
    // Continue, Exit) in a straight, steady column, as BlockPlanet's menus are.
    // The original slides and staggers the items, so the column is held at the
    // leftmost place seen since the menu appeared, not the current frame's
    // (which moved the whole column sideways every few frames, the owner's
    // report of 2026-11-08). The HUD's panels fade out meanwhile.
    bool end_menu = false;
    {
        static int column = 0;        // the held left edge, in the game's 320 columns
        static int frames_without = 1000;
        std::vector<blockplanet::GameText*> items;
        bool exit = false, other = false;
        for (auto& t : texts)
            if (t.font == 0 && (t.text == "TRY AGAIN" || t.text == "EXIT" || t.text == "TRAINING" || t.text == "CONTINUE")) {
                items.push_back(&t);
                (t.text == "EXIT" ? exit : other) = true;
            }
        if (exit && other && items.size() >= 2) {
            int left = 1 << 20;
            for (const auto* t : items) left = std::min(left, t->x);
            column = frames_without > 30 ? left : std::min(column, left);  // a new menu starts afresh
            frames_without = 0;
            for (auto* t : items) t->x = column, t->width = 0;
            end_menu = true;
        } else if (frames_without < 1000) {
            ++frames_without;
        }
    }
    const auto frame = picture_frame();
    const auto box = blockplanet::recent_card_box();
    // Inside BlockPlanet's card panel (Training), the card's text starts a
    // little in from the panel's rim, as the menus' text does.
    if (box) {
        int left = 1 << 20;
        for (const auto& t : texts)
            if (t.font == 1) left = std::min(left, t.x);
        const int shift = left < (1 << 20) ? std::max(0, box->x + 9 - left) : 0;
        for (auto& t : texts)
            if (t.font == 1) t.x += shift;
    }
    if ((!texts.empty() || box) && frame) {
        if (!view) {
            view = blockplanet::ui::HudView{};
            view->text_only = true;
        }
        view->end_menu = end_menu;
        const float area_w = frame->h * 4.0f / 3.0f;
        const float area_x = frame->x + (frame->w - area_w) / 2.0f;
        // Case: no shouting capitals (the owner's choice). The cards (font 1)
        // are drawn a word at a time, so their words are cased together, in
        // reading order, one run per colour (a yellow heading, an orange body),
        // so sentences read as sentences.
        std::vector<std::string> cased(texts.size());
        // The game's button codes (%A%, %B%, ...) become the player's own button
        // names (Xbox, PlayStation, Switch or keyboard).
        const auto buttons = [&](std::string text) {
            const auto bindings = controller_bindings();
            auto family = active_family.load();
            if (const char* f = std::getenv("BLOCKPLANET_BUTTON_FAMILY")) {  // research: k, x, p or n
                family = *f == 'k' ? ControllerFamily::Keyboard : *f == 'p' ? ControllerFamily::PlayStation
                       : *f == 'n' ? ControllerFamily::NintendoSwitch : ControllerFamily::Xbox;
            }
            // The codes as the game's font draws them (docs/research/m2-native-prompts.md):
            // %C#% is C-down (Magic), %C"% C-up, %C$% C-right; L and R are the shoulders
            // (the port maps C-right to the right trigger, L and R to the bumpers).
            const auto pick = [&](const char* k, const char* x, const char* p, const char* n) {
                return std::string(family == ControllerFamily::Keyboard ? k : family == ControllerFamily::Xbox ? x
                                 : family == ControllerFamily::PlayStation ? p : n);
            };
            const std::pair<const char*, std::string> codes[] = {
                {"%A%", logical_action_label(family, bindings, LogicalAction::Confirm)},
                {"%B%", logical_action_label(family, bindings, LogicalAction::Cancel)},
                {"%L%", pick("J", "LB", "L1", "L")},
                {"%R%", pick("S", "RB", "R1", "R")},
                {"%C#%", magic_control_label(family, magic_control())},
                {"%C\"%", logical_action_label(family, bindings, LogicalAction::FaceUp)},
                {"%C$%", pick("L", "RT", "R2", "ZR")},
            };
            // As "[family:name]", drawn as a button symbol by the UI.
            const char* f = family == ControllerFamily::Keyboard ? "k" : family == ControllerFamily::Xbox ? "x"
                          : family == ControllerFamily::PlayStation ? "p" : "n";
            for (const auto& [code, name] : codes) {
                const std::size_t n = std::strlen(code);
                for (std::size_t at; (at = text.find(code)) != std::string::npos;) text.replace(at, n, std::string("[") + f + ":" + name + "]");
            }
            // The movement controls, for the player's input (BlockPlanet's own wording).
            for (std::size_t at; (at = text.find("%MOVE%")) != std::string::npos;)
                text.replace(at, 6, family == ControllerFamily::Keyboard ? "THE ARROW KEYS" : "THE STICK OR D-PAD");
            // Any other % is the font's apostrophe or quote mark ("LET%S", "%X%").
            return tetrisphere::game_quotes(tetrisphere::fix_wording(text));
        };
        // A card's heading: its first line, when that line ends with a colon
        // ("Training:") or is a short line in one colour over a line starting
        // in another (Hide & Seek's yellow "Drill" over its orange goal).
        // Otherwise the first line is part of the body (a page without one).
        bool card_heading = false;
        {
            std::map<int, std::vector<std::size_t>> rows;
            for (std::size_t i = 0; i < texts.size(); ++i)
                if (texts[i].font == 1) rows[texts[i].y].push_back(i);
            for (auto& [y, idx] : rows)
                std::sort(idx.begin(), idx.end(), [&](std::size_t l, std::size_t r) { return texts[l].x < texts[r].x; });
            if (rows.size() > 1) {
                const auto& first = rows.begin()->second;
                const auto& second = std::next(rows.begin())->second;
                const auto same = [&](std::size_t a, std::size_t b) {
                    return texts[a].r == texts[b].r && texts[a].g == texts[b].g && texts[a].b == texts[b].b;
                };
                const std::string& last = texts[first.back()].text;
                bool one_colour = true;
                for (std::size_t i : first) one_colour = one_colour && same(i, first[0]);
                card_heading = (!last.empty() && last.back() == ':') ||
                               (first.size() <= 3 && one_colour && !same(second[0], first[0]));
            }
        }
        auto texts_in = texts;
        for (auto& t : texts_in) t.text = buttons(t.text);
        for (std::size_t i = 0; i < texts.size(); ++i) cased[i] = tetrisphere::proper_case(texts_in[i].text);
        {
            std::vector<std::size_t> order;
            for (std::size_t i = 0; i < texts.size(); ++i)
                if (texts[i].font == 1) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return texts[a].y != texts[b].y ? texts[a].y < texts[b].y : texts[a].x < texts[b].x;
            });
            // A heading: the first line, when its colour differs from the
            // next line's (a yellow title over an orange body); the rest is
            // one text, cased as sentences.
            std::size_t body = 0;
            if (!order.empty() && card_heading) {
                const int first_y = texts[order[0]].y;
                while (body < order.size() && texts[order[body]].y == first_y) ++body;
            }
            const auto apply = [&](std::size_t start, std::size_t end) {
                if (start >= end) return;
                std::string joined;
                for (std::size_t k = start; k < end; ++k) joined += (k > start ? " " : "") + texts_in[order[k]].text;
                const std::string run = tetrisphere::proper_case(joined);
                std::vector<std::string> words;
                std::string w;
                for (char ch : run) {
                    if (ch == ' ') { if (!w.empty()) words.push_back(w); w.clear(); }
                    else w += ch;
                }
                if (!w.empty()) words.push_back(w);
                // Back to the pieces, by each piece's word count (a piece is a word
                // as a card types in, or a whole line, as the game-over hint draws).
                std::vector<std::size_t> counts;
                std::size_t total = 0;
                for (std::size_t k = start; k < end; ++k) {
                    std::size_t n = 0;
                    bool in_word = false;
                    for (char ch : texts_in[order[k]].text) {
                        if (ch != ' ' && !in_word) ++n;
                        in_word = ch != ' ';
                    }
                    counts.push_back(n);
                    total += n;
                }
                if (total == words.size()) {
                    std::size_t at = 0;
                    for (std::size_t k = start; k < end; ++k) {
                        std::string piece;
                        for (std::size_t n = 0; n < counts[k - start]; ++n) piece += (n ? " " : "") + words[at++];
                        cased[order[k]] = piece;
                    }
                }
            };
            apply(0, body);
            apply(body, order.size());
        }
        // The cards' words (font 1), joined back into their lines: one text per
        // line, from where its first word starts, each word keeping its colour
        // and fade (the typing still shows).
        std::map<int, std::vector<std::size_t>> card_lines;
        for (std::size_t i = 0; i < texts.size(); ++i)
            if (texts[i].font == 1) card_lines[texts[i].y].push_back(i);
        // The body's lines start at one left edge (the game sometimes starts a
        // line after a sentence's two spaces, which looks like an indent).
        int body_left = 1 << 20;
        for (const auto& [y, idx] : card_lines)
            if (!card_heading || y != card_lines.begin()->first)
                for (std::size_t i : idx) body_left = std::min(body_left, texts[i].x);
        // The card's right edge: the furthest the game's own lines reach, kept
        // while the card shows (its words type in one by one). A centred box
        // (text starting left of centre) may use its width mirrored about the
        // centre; a box to one side (Training's) only the game's own edge.
        static int card_right = 0;
        if (card_lines.empty()) card_right = 0;
        for (const auto& [y, idx] : card_lines)
            for (std::size_t i : idx) card_right = std::max(card_right, texts[i].x + texts[i].width);
        std::set<std::size_t> joined_away;
        std::map<std::size_t, std::vector<std::pair<std::string, std::string>>> joined;
        // The heading line stays a line; the body becomes one paragraph, in
        // reading order, wrapped to the card's width (the box is centred in
        // the game's frame, its text from body_left), so it fills the box.
        std::size_t body_first = texts.size();
        for (auto& [y, idx] : card_lines) {
            std::sort(idx.begin(), idx.end(), [&](std::size_t l, std::size_t r) { return texts[l].x < texts[r].x; });
            const bool heading = card_heading && y == card_lines.begin()->first;
            for (std::size_t k = 0; k < idx.size(); ++k) {
                const auto& w = texts[idx[k]];
                char c[16];
                std::snprintf(c, sizeof c, "#%02x%02x%02x%02x", w.r, w.g, w.b, w.a);
                const std::size_t owner = heading ? idx[0] : (body_first == texts.size() ? idx[k] : body_first);
                if (!heading && body_first == texts.size()) body_first = idx[k];
                // A line that ends a sentence: the next starts on a new line
                // (the game-over hint's question, then its suggestion).
                auto& parts = joined[owner];
                if (k == 0 && !parts.empty() && !parts.back().first.empty()) {
                    const char end = parts.back().first.back();
                    if (end == '?' || end == '!' || end == '.') parts.emplace_back("\n", "");
                }
                parts.emplace_back(cased[idx[k]], c);
                if (owner != idx[k]) joined_away.insert(idx[k]);
            }
        }
        // A heading in the panel needs no colon ("Training", not "Training:").
        if (card_heading && !card_lines.empty()) {
            auto& parts = joined[card_lines.begin()->second.front()];
            if (!parts.empty() && !parts.back().first.empty() && parts.back().first.back() == ':') parts.back().first.pop_back();
        }
        for (std::size_t i = 0; i < texts.size(); ++i) {
            if (joined_away.count(i)) continue;
            const auto& t = texts[i];
            blockplanet::ui::HudView::Text out;
            out.text = cased[i];
            out.x = (area_x + float(t.x) / 320.0f * area_w) / frame->window_w;
            out.y = (frame->y + (float(t.y) - 2.0f) / 240.0f * frame->h) / frame->window_h;
            static const float font_lines[] = {13.0f, 14.0f, 11.0f, 11.0f};  // letter height per game font, of its 240 lines
            float lines = t.font >= 0 && t.font < 4 ? font_lines[t.font] : 11.0f;
            if (box && t.font == 1) lines = 12.5f;  // a little smaller in the panel, so a page fits
            out.size = lines / 240.0f * frame->h / frame->window_h * 100.0f;
            out.width = float(t.width) / 320.0f * area_w / frame->window_w;
            char colour[16];
            std::snprintf(colour, sizeof colour, "#%02x%02x%02x%02x", t.r, t.g, t.b, t.a);  // with the game's fade
            out.colour = colour;
            if (t.font == 1) out.width = 0;  // card text flows as text, not fitted to the game's spans
            if (t.font == 1 && !card_lines.empty() && (!card_heading || t.y != card_lines.begin()->first) && body_left < (1 << 20) &&
                t.x - body_left < 40)
                out.x = (area_x + float(body_left) / 320.0f * area_w) / frame->window_w;
            if (const auto j = joined.find(i); j != joined.end()) {
                out.parts = j->second;
                out.width = 0;  // laid out as text, from its start
                if (i == body_first && body_left < (1 << 20))
                {
                    const int right = box ? box->x + box->w - 12
                                          : std::max(card_right + 10, body_left < 160 ? 320 - body_left - 4 : 0);
                    if (right > body_left) out.wrap = float(right - body_left) / 320.0f * area_w / frame->window_w;
                }
            }
            if (box && t.font == 1) continue;  // in the panel instead
            view->game_text.push_back(std::move(out));
        }
        if (box) {
            // Training's bubble as a panel holding the card's text, always
            // spoken by Wheels: its bottom edge fixed just above his hat (the
            // game's line 127; he stands at the same place in every lesson),
            // its right edge where the old bubble's was (column 300), narrower
            // (118 of the game's 320 columns), growing upwards only; the tail
            // over his head (column 277).
            const auto fx = [&](float x) { return (area_x + x / 320.0f * area_w) / frame->window_w; };
            const auto fy = [&](float y) { return (frame->y + y / 240.0f * frame->h) / frame->window_h; };
            const float width = 118.0f * std::max(1.0f, card_text_scale());  // wider for larger text, so it stays on screen
            constexpr float right = 300.0f, bottom = 127.0f, tail_x = 277.0f;
            blockplanet::ui::HudView::Panel panel;
            panel.right = 1.0f - fx(right);
            panel.bottom = 1.0f - fy(bottom);
            panel.w = fx(right) - fx(right - width);
            panel.tail = (tail_x - (right - width)) / width;
            if (box->kind == 1) {
                // A rules or goal card: centred where the game's box was (its
                // middle column, from near its top), a little narrower, growing
                // downwards; no tail (nobody is speaking).
                const float card_w = 148.0f * std::max(1.0f, card_text_scale() * 0.9f);  // clear of the HUD panels beside it
                const float centre = float(box->x) + float(box->w) / 2.0f;
                panel.right = 1.0f - fx(centre + card_w / 2.0f);
                panel.w = fx(centre + card_w / 2.0f) - fx(centre - card_w / 2.0f);
                panel.top = fy(float(box->y) + 6.0f);
                panel.tail = -1.0f;
            }
            bool card_found = false;
            if (!card_lines.empty()) {
                const std::size_t first = card_lines.begin()->second.front();
                if (card_heading) panel.heading = joined[first];
                if (body_first < texts.size()) panel.body = joined[body_first];
            }
            // A card's text (a string in the game's memory, with its codes) as
            // panel blocks: a heading (the first line, when it ends with a colon
            // or is a short line all in yellow; `title` replaces it), then the
            // paragraph, with line breaks only before a list's items.
            // `intro`: the glossary terms this page introduces (their first mention in
            // the lesson's card): drawn as new, and the first one heads the page
            // when it has no heading of its own (the owner's choice).
            const auto add_text = [&](std::string text, const std::string& title, const std::vector<std::string>& intro) {
                // The movement controls first, so the words are cased as one sentence.
                for (std::size_t at; (at = text.find("%MOVE%")) != std::string::npos;) text.replace(at, 6, buttons("%MOVE%"));
                text = tetrisphere::fix_wording(text);
                // The menus' orange and yellow (docs/STYLE.md, D3).
                constexpr const char* orange = "#f5821fff";
                constexpr const char* yellow = "#e8b818ff";
                // Game terms in their own colour, apart from the yellow headings
                // (the owner's choice); "fe" alpha marks a term being introduced
                // (the panel adds a glow).
                constexpr const char* term_colour = "#86d8ffff";
                constexpr const char* term_colour_new = "#86d8fffe";
                struct Word { std::string text; bool yellow = false; bool line_before = false; bool coded = false; bool fresh = false; };
                std::vector<Word> words;
                Word word;
                bool next_yellow = false, line = false;
                const auto flush = [&] {
                    if (!word.text.empty()) words.push_back(word);
                    word = Word{};
                };
                for (std::size_t c = 0; c < text.size(); ++c) {
                    if (text[c] == '-' && c + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[c + 1]))) {
                        const char code = text[++c];
                        if (code == '0') { flush(); line = true; }
                        if (code == '1') { if (word.text.empty()) next_yellow = true; else word.yellow = true; }
                        continue;
                    }
                    if (text[c] == ' ') { flush(); continue; }
                    if (word.text.empty()) {
                        word.yellow = next_yellow;
                        word.line_before = line;
                        next_yellow = line = false;
                    }
                    word.text += text[c];
                }
                flush();
                if (words.empty()) return;
                // Highlights: the game's key terms, the same everywhere (the owner's
                // choice), each on its first mention in a page; no other words.
                {
                    const auto& glossary = glossary_table();
                    const auto bare = [](const std::string& w) {
                        std::string b;
                        for (char ch : w)
                            if (std::isalnum(static_cast<unsigned char>(ch))) b += ch;
                        return b;
                    };
                    for (auto& w : words) w.coded = w.yellow, w.yellow = false;
                    std::set<std::string> seen;
                    for (std::size_t i = 0; i < words.size();) {
                        bool matched = false;
                        for (const auto& [phrase, term] : glossary) {
                            if (i + phrase.size() > words.size()) continue;
                            bool same = true;
                            for (std::size_t k = 0; k < phrase.size() && same; ++k) same = bare(words[i + k].text) == phrase[k];
                            // Within one phrase only (not across a line break).
                            for (std::size_t k = 1; k < phrase.size() && same; ++k) same = !words[i + k].line_before;
                            if (!same) continue;
                            if (seen.insert(term).second) {
                                const bool fresh = std::find(intro.begin(), intro.end(), term) != intro.end();
                                for (std::size_t k = 0; k < phrase.size(); ++k) words[i + k].yellow = true, words[i + k].fresh = fresh;
                            }
                            i += phrase.size();
                            matched = true;
                            break;
                        }
                        if (!matched) ++i;
                    }
                }
                std::size_t body = 0;
                while (body < words.size() && (body == 0 || !words[body].line_before)) ++body;
                bool all_yellow = true;
                for (std::size_t w = 0; w < body; ++w) all_yellow = all_yellow && words[w].coded;  // the game's own highlight marks
                const bool heading = body < words.size() && ((!words[body - 1].text.empty() && words[body - 1].text.back() == ':') ||
                                                             (body <= 3 && all_yellow));
                if (!heading) body = 0;
                const auto runs = [&](std::size_t from, std::size_t to, bool is_title) {
                    std::string joined_text;
                    for (std::size_t w = from; w < to; ++w) joined_text += (w > from ? " " : "") + buttons(words[w].text);
                    std::string cased_text = tetrisphere::proper_case(joined_text);
                    if (is_title && !cased_text.empty() && cased_text.back() == ':') cased_text.pop_back();
                    std::vector<std::string> split;
                    std::size_t a = 0;
                    for (std::size_t b; (b = cased_text.find(' ', a)) != std::string::npos; a = b + 1) split.push_back(cased_text.substr(a, b - a));
                    split.push_back(cased_text.substr(a));
                    std::vector<std::pair<std::string, std::string>> out;
                    for (std::size_t w = from; w < to; ++w) {
                        const std::string& t = split.size() == to - from ? split[w - from] : buttons(words[w].text);
                        if (w > from && words[w].line_before && std::isdigit(static_cast<unsigned char>(t[0]))) out.push_back({"\n", ""});
                        out.push_back({t, is_title ? yellow : words[w].yellow ? (words[w].fresh ? term_colour_new : term_colour) : orange});
                    }
                    return out;
                };
                if (heading) {
                    if (!title.empty()) panel.blocks.push_back({true, {{title, yellow}}});
                    else panel.blocks.push_back({true, runs(0, body, true)});
                } else if (!intro.empty()) {
                    panel.blocks.push_back({true, {{glossary_heading(intro.front()), yellow}}});
                }
                if (body < words.size()) panel.blocks.push_back({false, runs(body, words.size(), false)});
            };
            // Training: the whole group of pages, read from the lesson's text.
            if (box->kind == 0 && guest::load16(guest_rdram, 0x800E44A8) == 3)
                if (const auto g = training_group()) {
                    // Which page of the card introduces each term: its first mention.
                    std::vector<std::vector<std::string>> intro(g->pages.size());
                    std::set<std::string> met;
                    for (std::size_t i = 0; i < g->pages.size(); ++i) {
                        if (g->pages[i].skip) continue;
                        std::string text;
                        for (std::uint32_t a = g->pages[i].start; a < g->pages[i].end; ++a) text += char(guest::load8(guest_rdram, a));
                        for (const auto& term : glossary_terms(tetrisphere::text_rewrite(text).value_or(text)))
                            if (met.insert(term).second) intro[i].push_back(term);
                    }
                    for (std::size_t i = g->first; i <= g->last; ++i) {
                        const auto& pg = g->pages[i];
                        if (pg.skip) continue;
                        std::string text;
                        for (std::uint32_t a = pg.start; a < pg.end; ++a) text += char(guest::load8(guest_rdram, a));
                        add_text(tetrisphere::text_rewrite(text).value_or(text), "", intro[i]);  // the owner's reviewed wording
                    }
                }
            // A rules or goal card (Hide & Seek, Lines, VS): found in the game's
            // memory from its heading's first word, which types in first, and
            // shown whole at once.
            if (box->kind == 1 && !card_lines.empty()) {
                std::string first;
                for (char ch : texts[card_lines.begin()->second.front()].text)
                    if (std::isalpha(static_cast<unsigned char>(ch))) first += ch; else break;
                // The card's whole text, shown from its first page; a later
                // page of the same card (Lines' rules run on to a second) keeps
                // it, rather than the game's text from mid-sentence.
                static std::optional<std::string> shown;
                static auto shown_at = std::chrono::steady_clock::time_point{};
                const auto now_card = std::chrono::steady_clock::now();
                if (now_card - shown_at > std::chrono::seconds(1)) shown.reset();  // a new card
                if (const auto found = find_card_text(first)) shown = *found;
                if (shown) {
                    add_text(tetrisphere::text_rewrite(*shown).value_or(*shown), hide_and_seek_title(*shown), {});
                    card_found = true;
                }
                shown_at = now_card;
            }
            if (box->prompt == 1) panel.hint = buttons("%A%") + " More";
            if (box->prompt == 2) panel.hint = "Wheels is showing you\u2026";  // a demonstration: it moves on by itself
            // A rules or goal card closes with A: say so, the same on every card.
            if (box->kind == 1 && panel.hint.empty()) panel.hint = buttons("%A%") + " Start";
            {
                auto family = active_family.load();
                if (const char* o = std::getenv("BLOCKPLANET_BUTTON_FAMILY"))  // research, as for the cards' buttons
                    family = *o == 'k' ? ControllerFamily::Keyboard : *o == 'p' ? ControllerFamily::PlayStation
                           : *o == 'n' ? ControllerFamily::NintendoSwitch : ControllerFamily::Xbox;
                const char f = family == ControllerFamily::Xbox ? 'x' : family == ControllerFamily::PlayStation ? 'p' : 'n';
                panel.scroll_hint = family == ControllerFamily::Keyboard ? "[k:PgDn] Scroll" : std::string("[") + f + ":RS] Scroll";
            }
            view->card_panel = panel;
        }
    }
    blockplanet::ui::set_hud(std::move(view));
}

// The title screen: the painted picture (with the copyright lines) is not
// drawn while the 3D letters are held, so the original game's notice is shown
// as text, word for word (the owner's exception in AGENTS.md), with
// BlockPlanet's own disclaimer beneath.
constexpr const char* title_notice_lines[] = {
    "\u00A91997 Nintendo \u00B7 Game design by H2O Entertainment Corp.",  // the original's wide gap, as a dot
    "Tetrisphere\u2122 licensed to Nintendo by The Tetris Company.",
};
constexpr const char* title_disclaimer =
    "BlockPlanet is an unofficial fan project, not affiliated with or endorsed by Nintendo or The Tetris Company. "
    "It needs your own copy of the game.";

bool update_title_overlay() {
    if (!blockplanet_title_recent(nullptr)) return false;
    const auto frame = picture_frame();
    if (!frame) {
        show_hud(std::nullopt);
        return true;
    }
    // Where the picture had the lines: near the bottom of the game's 4:3
    // frame (about 87% of its height), across the frame's width.
    const float area_w = frame->h * 4.0f / 3.0f;
    const float area_x = frame->x + (frame->w - area_w) / 2.0f;
    blockplanet::ui::HudView view;
    for (const char* line : title_notice_lines) view.title_notice.emplace_back(line);
    view.title_disclaimer = title_disclaimer;
    view.title_opacity = blockplanet_title_notice_opacity();
    view.title_x = (area_x + area_w * 0.04f) / frame->window_w;
    view.title_w = area_w * 0.92f / frame->window_w;
    view.title_y = (frame->y + frame->h * 0.84f) / frame->window_h;
    show_hud(view);
    return true;
}


// Places the held magic's 3D model in the HUD's item box, wherever the HUD's
// layout put it (ui::hud_item_box), in picture heights from the picture's
// centre. The box must be inside the picture (not on portrait screens, where
// the containers are above and below it).
void update_magic_box() {
    const auto frame = picture_frame();
    const auto box = blockplanet::ui::hud_item_box();
    if (!frame || !box || box->frame_width <= 0.0f || box->frame_height <= 0.0f) {
        blockplanet::set_magic_box(true, NAN, NAN, 1.0f, NAN, 0.0f);
        return;
    }
    // The box's centre and height in window pixels.
    const float sx = frame->window_w / box->frame_width, sy = frame->window_h / box->frame_height;
    const float bx = (box->x + box->width / 2.0f) * sx, by = (box->y + box->height / 2.0f) * sy;
    const float bh = box->height * sy;
    const float x = (bx - (frame->x + frame->w / 2.0f)) / frame->h;
    const float y = (by - (frame->y + frame->h / 2.0f)) / frame->h;
    static const bool log_box = std::getenv("BLOCKPLANET_MAGIC_BOX_LOG") != nullptr;  // research
    static int logged = 0;
    if (log_box && logged++ % 120 == 0)
        std::fprintf(stdout, "{\"event\":\"magic_box\",\"x\":%g,\"y\":%g,\"box\":[%g,%g,%g,%g],\"frame\":[%g,%g]}\n", x, y,
                     box->x, box->y, box->width, box->height, box->frame_width, box->frame_height);
    if (std::abs(x) > frame->aspect / 2.0f || std::abs(y) > 0.5f) {
        blockplanet::set_magic_box(true, NAN, NAN, 1.0f, NAN, 0.0f);
        return;
    }
    // The held draw is a left-anchored span, which RT64 moves left on wide
    // pictures; measured: by about 0.73 of the picture's width beyond 4:3, halved.
    const float shift = 0.73f * (frame->aspect - 4.0f / 3.0f) / 2.0f;
    const float x_anchored = x + shift;
    // Sized to sit inside the box as it turns (measured with the Magnet: about
    // 0.13 picture heights tall at scale 1).
    blockplanet::set_magic_box(true, x, y, 0.45f * (bh / frame->h) / 0.13f, x_anchored, shift,
                               box->width * sx / frame->h, bh / frame->h, 0.8f * (frame->aspect - 4.0f / 3.0f) / 2.0f);
}

// The sphere's radius on screen (picture heights), estimated from its highest
// layer and the camera distance (the core's surface is at a third of the
// grid's length and each layer adds 0.0875; calibrated so stage 1:1 at rest
// fills 55% of the picture's height).
float hud_sphere_radius(const tetrisphere::StoryHudState& state) {
    return 1584.0f * (1.0f / 3.0f + 0.0875f * float(state.top_layer)) / state.camera_distance;
}

void place_hud(blockplanet::ui::HudView& view, const tetrisphere::StoryHudState& state) {
    const auto frame = picture_frame();
    if (!frame) return;
    view.picture_x = frame->x / frame->window_w;
    view.picture_y = frame->y / frame->window_h;
    view.picture_w = frame->w / frame->window_w;
    view.picture_h = frame->h / frame->window_h;
    view.sphere_radius = hud_sphere_radius(state);
    const auto distance = profile_data.preferences.hud_distance.value_or("near");
    view.distance_mode = distance == "edges" ? 1 : distance == "follow" ? 2 : 0;
}

Settings persisted_settings{};
SDL_Rect windowed_bounds{};
std::unique_ptr<WindowModeController> window_mode_controller;

void remember_windowed_bounds() {
    SDL_GetWindowPosition(window, &windowed_bounds.x, &windowed_bounds.y);
    SDL_GetWindowSize(window, &windowed_bounds.w, &windowed_bounds.h);
}

// The window's place between launches (window_placement.h): saved a second
// after it last moved or changed size, and on the way out, while windowed.
std::optional<std::chrono::steady_clock::time_point> window_placement_dirty;

std::filesystem::path window_placement_path() { return application_data_directory() / "window.txt"; }

std::optional<tetrisphere::WindowPlacement> load_window_placement() {
    std::ifstream in(window_placement_path());
    if (!in) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto saved = tetrisphere::parse_window_placement(text);
    if (!saved) return std::nullopt;
    std::vector<tetrisphere::ScreenArea> screens;
    for (int display = 0; display < SDL_GetNumVideoDisplays(); ++display) {
        SDL_Rect r{};
        if (SDL_GetDisplayUsableBounds(display, &r) == 0) screens.push_back({r.x, r.y, r.w, r.h});
    }
    return tetrisphere::usable_window_placement(*saved, screens);
}

void save_window_placement() {
    window_placement_dirty.reset();
    if (window == nullptr) return;
    const Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED)) return;
    tetrisphere::WindowPlacement placement;
    SDL_GetWindowPosition(window, &placement.x, &placement.y);
    SDL_GetWindowSize(window, &placement.width, &placement.height);
    placement.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    std::error_code error;
    std::filesystem::create_directories(window_placement_path().parent_path(), error);
    std::ofstream(window_placement_path(), std::ios::trunc) << tetrisphere::format_window_placement(placement);
}

bool enter_fullscreen(const char*& kind) {
    bool exact_uhd = false;
    if (active_settings.graphics.resolution == GraphicsResolution::P2160) {
        const int window_display = SDL_GetWindowDisplayIndex(window);
        SDL_DisplayMode selected{};
        int selected_display = -1;
        int best_difference = 1000;
        for (int display = 0; display < SDL_GetNumVideoDisplays(); ++display) {
            SDL_DisplayMode current{};
            const int current_refresh = SDL_GetCurrentDisplayMode(display, &current) == 0
                ? current.refresh_rate : 60;
            for (int index = 0; index < SDL_GetNumDisplayModes(display); ++index) {
                SDL_DisplayMode candidate{};
                if (SDL_GetDisplayMode(display, index, &candidate) != 0 ||
                    candidate.w != 3840 || candidate.h != 2160) continue;
                const int difference = std::abs(candidate.refresh_rate - current_refresh);
                if (selected_display < 0 ||
                    (display == window_display && selected_display != window_display) ||
                    ((display == window_display) == (selected_display == window_display) &&
                     difference < best_difference)) {
                    selected = candidate;
                    selected_display = display;
                    best_difference = difference;
                }
            }
        }
        if (selected_display >= 0) {
            SDL_SetWindowPosition(window,
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(selected_display),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(selected_display));
        }
        if (selected_display >= 0 && SDL_SetWindowDisplayMode(window, &selected) == 0 &&
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) == 0) {
            SDL_PumpEvents();
            SDL_DisplayMode applied{};
            int drawable_width = 0;
            int drawable_height = 0;
            SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);
            exact_uhd = SDL_GetWindowDisplayIndex(window) == selected_display &&
                SDL_GetCurrentDisplayMode(selected_display, &applied) == 0 &&
                applied.w == 3840 && applied.h == 2160 &&
                drawable_width == 3840 && drawable_height == 2160;
            if (exact_uhd) kind = "exclusive-uhd";
        }
    }
    if (!exact_uhd) {
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
            return false;
        kind = "desktop";
    }
    return true;
}

void report_graphics_surface(const char* fullscreen_kind) {
    int actual_window_width = 0;
    int actual_window_height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GetWindowSize(window, &actual_window_width, &actual_window_height);
    SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);
    SDL_DisplayMode active_display{};
    const int display_index = SDL_GetWindowDisplayIndex(window);
    if (display_index >= 0) SDL_GetCurrentDisplayMode(display_index, &active_display);
    const auto requested_internal_scale =
        graphics_internal_scale(active_settings.graphics.resolution);
    std::fprintf(stdout,
                 "{\"event\":\"graphics_surface_dimensions\","
                 "\"run_id\":\"%s\",\"window_width\":%d,\"window_height\":%d,"
                 "\"drawable_width\":%d,\"drawable_height\":%d,"
                 "\"requested_internal_height\":%d,\"aspect\":\"%s\","
                 "\"window_mode\":\"%s\",\"fullscreen_kind\":\"%s\","
                 "\"display_index\":%d,\"display_width\":%d,\"display_height\":%d}\n",
                 run_id(), actual_window_width, actual_window_height,
                 drawable_width, drawable_height,
                 requested_internal_scale
                     ? static_cast<int>(*requested_internal_scale * 240.0) : 0,
                 graphics_aspect_name(active_settings.graphics.aspect),
                 graphics_window_mode_name(active_settings.graphics.window_mode),
                 fullscreen_kind, display_index, active_display.w, active_display.h);
    std::fflush(stdout);
}

bool apply_window_mode(bool fullscreen) {
    if (fullscreen) remember_windowed_bounds();
    const char* kind = fullscreen ? "desktop" : "none";
    if (fullscreen) {
        if (!enter_fullscreen(kind)) {
            SDL_SetWindowFullscreen(window, 0);
            SDL_SetWindowPosition(window, windowed_bounds.x, windowed_bounds.y);
            SDL_SetWindowSize(window, windowed_bounds.w, windowed_bounds.h);
            return false;
        }
    } else {
        if (SDL_SetWindowFullscreen(window, 0) != 0) return false;
        SDL_SetWindowDisplayMode(window, nullptr);
        SDL_SetWindowSize(window, windowed_bounds.w, windowed_bounds.h);
        SDL_SetWindowPosition(window, windowed_bounds.x, windowed_bounds.y);
    }
    active_settings.graphics.window_mode = fullscreen
        ? GraphicsWindowMode::Fullscreen : GraphicsWindowMode::Windowed;
    auto config = ultramodern::renderer::get_graphics_config();
    config.wm_option = fullscreen ? ultramodern::renderer::WindowMode::Fullscreen
                                  : ultramodern::renderer::WindowMode::Windowed;
    ultramodern::renderer::set_graphics_config(config);
    report_graphics_surface(kind);
    return true;
}

const char* family_override() {
    const char* temporary = std::getenv("TETRISPHERE_CONTROLLER_FAMILY");
    return temporary == nullptr ? active_settings.family_override.c_str() : temporary;
}

void poll_input() {
    input_polls.fetch_add(1, std::memory_order_relaxed);
    const auto controllers = controller_ports.snapshot();
    const auto keys = keyboard_state.buttons();
    std::lock_guard lock(input_snapshot_mutex);
    input_snapshot = controllers;
    keyboard_snapshot = keys;
}

void count_vi() {
    observed_vis.fetch_add(1, std::memory_order_relaxed);
}

// BlockPlanet's menus take the input while open, and until every button is
// released after they close (so the closing press does not reach the game).
std::atomic<bool> menus_block_game{false};
// Buttons the mode launcher presses on port 1 (bit 16 set while active).
std::atomic<std::uint32_t> launcher_buttons{0};
ModeLauncher mode_launcher;
// A direct start (without the original menus) waiting for the game's thread;
// if it is refused the launch falls back to mode_launcher.
struct DirectWait {
    LaunchMode mode;
    std::chrono::steady_clock::time_point since;
};
std::optional<DirectWait> direct_wait;
bool launching() { return mode_launcher.active() || direct_wait.has_value(); }
// Back from "Who is playing?" plays the game's intro again (no relaunch); the
// menus then wait for the title as at boot: first for the old title to go,
// then for it to be shown again.
bool title_seen = false;
bool title_restart_pending = false;
// Profile changes in place (no relaunch, the owner's request 2026-11-10).
std::filesystem::path trophy_definitions_path;
std::optional<std::chrono::steady_clock::time_point> box_art_returning_since;  // Back from "Who is playing?" to the box art
std::string profile_to_remove;  // removed once the game has let go of its save
bool start_chapter_given = false;  // --start-chapter: progress is not recorded

// BlockPlanet's pause menu over the game's pause.
PauseBridge pause_bridge;
EndMenuBridge end_menu;

// Settings changed in BlockPlanet's menus, applied like the PC settings menu.
std::deque<PCSettingsRequest> menu_settings_requests;


// When the game last saw Start held (either port): the game opens its pause
// only on Start, while chapter-end scenes reuse the pause row, so
// BlockPlanet's pause screen opens only after a Start press.
std::atomic<std::int64_t> last_start_ms{-1000000};
void note_start(std::uint16_t buttons) {
    if (buttons & 0x1000u)
        last_start_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool start_pressed_recently() {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return now - last_start_ms.load() < 1500;
}

bool get_input(int port, std::uint16_t* buttons, float* x, float* y) {
    if (port < 0 || port >= 2) return false;
    if (const auto injected = launcher_buttons.load(); injected & 0x10000u) {
        *buttons = port == 0 ? static_cast<std::uint16_t>(injected & 0xFFFFu) : 0;
        *x = 0; *y = 0;
        note_start(*buttons);
        return true;
    }
    if (controls_editor_blocks_game() || pc_settings_blocks_game() || menus_block_game.load()) {*buttons=0;*x=0;*y=0;return true;}
    // The runtime asks for ports in order. Freeze both readings together so
    // an SDL event between get_input(0) and get_input(1) cannot mix cycles.
    struct ReadCycle {
        std::array<ControllerPortSnapshot, 2> controllers{};
        std::uint16_t keys = 0;
        bool valid = false;
    };
    thread_local ReadCycle cycle;
    if (port == 0 || !cycle.valid) {
        std::lock_guard lock(input_snapshot_mutex);
        cycle.controllers = input_snapshot;
        cycle.keys = keyboard_snapshot;
        cycle.valid = true;
    }
    const auto& controller = cycle.controllers[port];
    const auto keys = cycle.keys;
    if (port == 1) cycle.valid = false;
    if (port == 1 && !controller.connected) return false;
    const auto mapped = map_port_input(port, controller, keys,
                                       controller_bindings(), magic_control());
    const std::uint16_t mouse_bits = port == 0 ? mouse_play.next_frame() : 0;
    if (minefield_frozen.load() || game_over_frozen.load()) {*buttons=0;*x=0;*y=0;return true;}
    *buttons = port == 0 ? tetrisphere::minefield_sphere::filter_input(mapped.buttons | mouse_bits) : mapped.buttons;
    note_start(mapped.buttons);
    *x = mapped.x;
    *y = mapped.y;
    // The game moves the cursor by the stick, so the mouse's steps go there too.
    if (mouse_bits & 0x0200) *x = -1.0f;
    if (mouse_bits & 0x0100) *x = 1.0f;
    if (mouse_bits & 0x0800) *y = 1.0f;
    if (mouse_bits & 0x0400) *y = -1.0f;
    if (port == 0) tetrisphere::minefield_sphere::filter_stick(*x, *y);
    static std::array<std::uint16_t, 2> previous{};
    if (mapped.buttons != previous[port]) {
        const auto family = port == 0 ? active_family.load() : active_p2_family.load();
        std::fprintf(stdout,
                     "{\"event\":\"logical_input_sample\",\"run_id\":\"%s\",\"port\":%d,\"family\":\"%s\","
                     "\"buttons\":\"0x%04X\",\"prompt_confirm\":\"%s\"}\n",
                     run_id(), port, controller_family_name(family),
                     static_cast<unsigned>(mapped.buttons),
                     logical_action_label(family, controller_bindings(),
                                          LogicalAction::Confirm));
        std::fflush(stdout);
        previous[port] = mapped.buttons;
    }
    return true;
}

void set_rumble(int port, bool enabled) {
    controller_ports.rumble(port, enabled);
}

ultramodern::input::connected_device_info_t device_info(int port) {
    if (port < 0 || port >= 2)
        return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    std::lock_guard lock(input_snapshot_mutex);
    if (input_snapshot[port].connected)
        return {ultramodern::input::Device::Controller,
                ultramodern::input::Pak::RumblePak};
    if (port == 0)
        return {ultramodern::input::Device::Controller,
                ultramodern::input::Pak::None};
    return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
}

void set_audio_frequency(std::uint32_t frequency) {
    std::lock_guard lock(audio_mutex);
    std::string error;
    if (!audio_output.open(frequency, error)) {
        diagnostic_log().error("audio_open_failed");
        std::fprintf(stderr, "{\"event\":\"fatal_audio\",\"detail\":\"%s\"}\n",
                     error.c_str());
        std::exit(70);
    }
    const char* capture_path = std::getenv("TETRISPHERE_AUDIO_CAPTURE_WAV");
    if (audio_capture.active()) {
        // A later rate (the game's own, after the runtime's placeholder):
        // the capture's header follows it.
        audio_capture.set_sample_rate(frequency);
        return;
    }
    if (capture_path != nullptr && *capture_path != '\0') {
        if (!audio_capture.open(capture_path, frequency)) {
            std::fprintf(stderr,
                         "{\"event\":\"fatal_audio_capture\","
                         "\"detail\":\"cannot_open_wav\"}\n");
            std::exit(70);
        }
        std::fprintf(stdout,
                     "{\"event\":\"audio_capture_started\","
                     "\"source\":\"queued_to_sdl_device\",\"frequency\":%u}\n",
                     frequency);
        std::fflush(stdout);
    }
}

void queue_audio(std::int16_t* samples, std::size_t count) {
    std::lock_guard lock(audio_mutex);
    std::string error;
    if (!audio_output.queue(samples, count, error)) {
        diagnostic_log().error("audio_queue_failed");
        std::fprintf(stderr,
                     "{\"event\":\"fatal_audio_queue\",\"detail\":\"%s\","
                     "\"sample_count\":%zu}\n", error.c_str(), count);
        std::exit(70);
    }
    if (audio_capture.active() && !audio_capture.append(samples, count)) {
        std::fprintf(stderr,
                     "{\"event\":\"fatal_audio_capture\","
                     "\"detail\":\"wav_write_failed\"}\n");
        std::exit(70);
    }
}

std::size_t audio_frames_remaining() {
    return audio_output.frames_remaining();
}

void rsp_init() { recomp::rsp::constants_init(); }

RspUcodeFunc* select_rsp_ucode(const OSTask* task) {
    constexpr std::uint32_t audio_ucode = 0x800DE7D0u;
    return task->t.type == M_AUDTASK &&
           static_cast<std::uint32_t>(task->t.ucode) == audio_ucode
        ? tetrisphere_audio_rsp : nullptr;
}

bool run_rsp(std::uint8_t* rdram, const OSTask* task) {
    constexpr std::uint32_t audio_ucode = 0x800DE7D0u;
    constexpr std::size_t audio_ucode_size = 0xDF0;
    constexpr std::uint64_t audio_ucode_hash = 0xE621BF918F29B135ULL;
    if (task->t.type != M_AUDTASK ||
        static_cast<std::uint32_t>(task->t.ucode) != audio_ucode ||
        XXH3_64bits(rdram + (audio_ucode & 0x7FFFFFu), audio_ucode_size) !=
            audio_ucode_hash) {
        return false;
    }
    recomp::rsp::set_callbacks({select_rsp_ucode});
    const auto start = std::chrono::steady_clock::now();
    const bool result = recomp::rsp::run_task(rdram, task);
    const auto elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
    audio_rsp_tasks.fetch_add(1, std::memory_order_relaxed);
    audio_rsp_total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
    auto previous_max = audio_rsp_max_us.load(std::memory_order_relaxed);
    while (elapsed_us > previous_max && !audio_rsp_max_us.compare_exchange_weak(
               previous_max, elapsed_us, std::memory_order_relaxed)) {}
    return result;
}

void show_error(const char* message) {
    diagnostic_log().error("runtime_error");
    std::fprintf(stderr, "{\"event\":\"runtime_error\",\"detail\":\"%s\"}\n",
                 message == nullptr ? "unknown" : message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tetrisphere error",
        message == nullptr ? "Unknown error." : message, window);
}

} // namespace

void publish_menu_settings(const Settings& settings);
void update_build_label();
void update_player_line();

extern "C" void tetrisphere_rt64_clear_keyboard_input() {
    SDL_Event focus_lost{};
    focus_lost.type = SDL_WINDOWEVENT;
    focus_lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    keyboard_state.handle_event(focus_lost);
    std::lock_guard lock(input_snapshot_mutex);
    keyboard_snapshot = 0;
}

ControllerFamily active_controller_family() { return active_family.load(); }

ControllerFamily active_controller_family_for_port(int port) {
    if (port == 0) return active_family.load();
    if (port == 1) return active_p2_family.load();
    return ControllerFamily::Keyboard;
}

bool linux_audio_fifo_full() {
    return audio_output.full();
}

std::size_t linux_audio_current_bytes() {
    return audio_output.current_bytes();
}

unsigned linux_audio_drain_wait_budget_ms() {
    const auto pacing = audio_output.pacing_snapshot();
    return audio_drain_wait_budget_ms(pacing.device_buffer_frames,
                                      pacing.obtained_frequency);
}

// The renderer's picture shape for the Picture setting: 4:3 the original,
// 16:9 a fixed widescreen picture, Fill the window's own shape.
ultramodern::renderer::AspectRatio renderer_aspect(GraphicsAspect aspect) {
    switch (aspect) {
    case GraphicsAspect::Original4x3: return ultramodern::renderer::AspectRatio::Original;
    case GraphicsAspect::Expand16x9: return ultramodern::renderer::AspectRatio::Manual;
    case GraphicsAspect::Fill: return ultramodern::renderer::AspectRatio::Expand;
    }
    return ultramodern::renderer::AspectRatio::Expand;
}

void initialize_linux_platform(std::uint8_t* rdram, const char* rom_sha256,
                               const RunOptions& run_options) {
#ifndef _WIN32
    // Without a D-Bus session (headless machines), SDL's shutdown makes a
    // call libdbus only warns about; its default is to abort on warnings.
    setenv("DBUS_FATAL_WARNINGS", "0", 0);
#endif
    guest_rdram = rdram;
    {
        const char* value = std::getenv("TETRISPHERE_LOG_GAME_EVENTS");
        log_game_events = value != nullptr && std::strcmp(value, "1") == 0;
    }
    initialize_controller_mappings();
#ifdef _WIN32
    // ultramodern paces VIs and the audio wait with Win32 Sleep. Ask for 1 ms
    // timer resolution explicitly instead of relying on SDL to have done so.
    timeBeginPeriod(1);
#endif
    {
        const char* perf_env = std::getenv("TETRISPHERE_PERF_METRICS");
        perf_log_enabled = run_options.perf_log ||
            (perf_env != nullptr && std::strcmp(perf_env, "1") == 0);
        perf_present_mode = present_mode_name(
            run_options.present_mode.value_or(PresentMode::Console));
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    const char* data_dir_override = std::getenv("TETRISPHERE_DATA_DIR");
    const auto user_path = application_data_directory();
    if (user_path.empty()) {
        show_error("Cannot determine the game data directory.");
        std::exit(74);
    }
    // Player profiles: the cartridge save and trophies live in the active
    // profile's folder; settings are shared (docs/SAVES.md). A guest has no
    // folder: the save stays in memory and no trophies are earned.
    profile_store = std::make_unique<ProfileStore>(user_path);
    guest_session = run_options.guest;
    tetrisphere::direct_start::set_guest(guest_session);
    // The main build shows every menu as if unlocked (the owner's call,
    // 2026-11-14; each item carries its development status). The unlocking
    // of docs/MENU_STRUCTURE.md applies once BLOCKPLANET_LOCKED is set (for
    // testing it), and in release builds when they come.
    unlock_all_testing = std::getenv("BLOCKPLANET_LOCKED") == nullptr || run_options.unlock_all ||
                         std::getenv("BLOCKPLANET_UNLOCK_ALL") != nullptr;
    auto& profiles = *profile_store;
    std::string profile_error;
    if (!profiles.load(profile_error) ||
        (run_options.profile && !profiles.use(*run_options.profile, profile_error))) {
        show_error(profile_error.c_str());
        std::exit(74);
    }
    update_player_line();
    profiles.purge_deleted(30);
    const auto profile_path = run_options.guest ? std::filesystem::path{} : profiles.directory(profiles.active());
    std::string eeprom_error;
    std::filesystem::path eeprom_path;
    if (!run_options.guest) {
        std::error_code ignored;
        std::filesystem::create_directories(profile_path, ignored);
        eeprom_path = profile_path / "cartridge.eep";
        // Keep the cartridge saves of the last three sessions.
        if (!rotate_backup(eeprom_path, profile_path / "backups", 3, eeprom_error))
            std::fprintf(stderr, "Could not back up the save: %s\n", eeprom_error.c_str());
    }
    std::fprintf(stdout, "{\"event\":\"profile\",\"id\":\"%s\",\"guest\":%s,\"profiles\":%zu}\n",
                 run_options.guest ? "" : profiles.active().id.c_str(), run_options.guest ? "true" : "false",
                 profiles.profiles().size());
    std::fflush(stdout);
    // Who is playing is asked after the title (the owner's choice), unless
    // the command line said (as it does when the game restarts as a profile).
    profile_picked = run_options.profile.has_value() || run_options.guest;
    credits_pending = run_options.credits;
    update_build_label();
    // The menus' button hints before any input (menu_input.cpp then follows the device).
    blockplanet::ui::set_button_text("new_profile_hint", "[k:T] New profile\u00A0\u00A0\u00A0[k:G] Play as guest");
    blockplanet::ui::set_button_text("move_back", "[k:Up] [k:Down] Move\u00A0\u00A0\u00A0[k:Esc] Back");
    if (!initialize_eeprom(eeprom_path, eeprom_error)) {
        show_error(eeprom_error.c_str());
        std::exit(74);
    }
    const auto settings_path = user_path / "settings.dat";
    SettingsStore settings_store(settings_path);
    std::string settings_error;
    if (!settings_store.load(active_settings, settings_error)) {
        show_error(settings_error.c_str());
        std::exit(74);
    }
    if (!std::filesystem::exists(settings_path) &&
        !settings_store.save(active_settings, settings_error)) {
        show_error(settings_error.c_str());
        std::exit(74);
    }
    // Controls belong to the profile. settings.dat keeps the PC's defaults,
    // which a profile without controls of its own starts from; a guest uses
    // them for the session.
    pc_controls = controls_of(active_settings);
    start_chapter_given = run_options.start_chapter.has_value();
    if (!run_options.guest) {
        profile_save = std::make_unique<ProfileSave>(profile_path / "save.json");
        std::string save_error;
        if (!profile_save->load(profile_data, save_error)) {
            show_error(save_error.c_str());
            std::exit(74);
        }
        refresh_continue();
        if (profile_data.preferences.controls) {
            apply_controls(active_settings, *profile_data.preferences.controls);
        } else {
            profile_data.preferences.controls = pc_controls;
            profile_dirty = true;
            save_profile();
        }
        profile_recording = !run_options.start_chapter;
        // The profile's audio settings, kept applied over the game's own.
        if (profile_data.preferences.audio) {
            blockplanet::set_audio_levels(profile_data.preferences.audio->music, profile_data.preferences.audio->effects);
            blockplanet::set_music_random(profile_data.preferences.audio->random_music);
        }
    }
    persisted_settings=active_settings;
    initialize_pc_settings(persisted_settings);
    publish_menu_settings(persisted_settings);
    if (run_options.fullscreen) {
        active_settings.graphics.window_mode = *run_options.fullscreen
            ? GraphicsWindowMode::Fullscreen : GraphicsWindowMode::Windowed;
    }
    set_controller_bindings(active_settings.bindings);
    set_magic_control(active_settings.magic);
    active_family.store(ControllerFamily::Keyboard);
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto diagnostic_path = user_path / "diagnostics" /
        (std::to_string(timestamp) + "-" + run_id() + ".jsonl");
    std::string diagnostic_error;
    if (!diagnostic_log().start(diagnostic_path, run_id(), TETRISPHERE_BUILD_ID,
                                rom_sha256, active_settings, diagnostic_error)) {
        show_error(diagnostic_error.c_str());
        std::exit(74);
    }
    install_crash_handler(diagnostic_path.parent_path(), run_id(), TETRISPHERE_BUILD_ID);
    // A crash last time: say where its report is (once). Not in automated test
    // runs, which would stop at the message.
    if (const auto reports = take_new_crash_reports(diagnostic_path.parent_path());
        !reports.empty() && std::getenv("TETRISPHERE_QA_CONTROLLERS") == nullptr) {
        const std::string message = "BlockPlanet closed unexpectedly last time. A crash report was saved here:\n\n" +
                                    std::string(reinterpret_cast<const char*>(reports.back().u8string().c_str())) +
                                    "\n\nPlease send it with a short note of what you were doing.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "BlockPlanet", message.c_str(), nullptr);
    }
    // The box art's flow events also go to the diagnostics file.
    tetrisphere::direct_start::set_event_sink([](const std::string& line) { diagnostic_log().perf(line); });
    std::fprintf(stdout, "{\"event\":\"diagnostic_file_ready\",\"run_id\":\"%s\"}\n",
                 run_id());
    std::fflush(stdout);
    {
        char* base = SDL_GetBasePath();
        const auto definitions = base != nullptr
            ? std::filesystem::u8path(base) / "trophies.json" : std::filesystem::path("trophies.json");
        if (base != nullptr) SDL_free(base);
        trophy_definitions_path = definitions;
        if (!run_options.guest) initialize_trophies(definitions, profile_path / "trophies.json");
        load_developer_records_at_start(definitions.parent_path() / "developer_records.json");
    }
    game_option_applier = std::make_unique<GameOptionApplier>(
        GameOptions{run_options.casual, run_options.start_chapter});
    if (game_option_applier->affects_awards()) {
        trophy_engine().set_awards_enabled(false);
        std::fprintf(stdout, "{\"event\":\"game_options\",\"casual\":%s,\"start_chapter\":%d,"
                     "\"trophies\":\"paused\"}\n", run_options.casual ? "true" : "false",
                     run_options.start_chapter.value_or(0));
        std::fflush(stdout);
    }
    SDL_Rect usable_display{};
    if (SDL_GetDisplayUsableBounds(0, &usable_display) != 0)
        usable_display = SDL_Rect{0, 0, 960, 720};
    const auto [window_width, window_height] = graphics_window_size(
        active_settings.graphics, usable_display.w, usable_display.h);
    // Where the window was last time, if it still fits a screen.
    const auto placement = load_window_placement();
    window = SDL_CreateWindow("Tetrisphere",
                              placement ? placement->x : SDL_WINDOWPOS_CENTERED,
                              placement ? placement->y : SDL_WINDOWPOS_CENTERED,
                              placement ? placement->width : window_width,
                              placement ? placement->height : window_height,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_ALLOW_HIGHDPI);
    if (window != nullptr && placement && placement->maximized &&
        active_settings.graphics.window_mode != GraphicsWindowMode::Fullscreen)
        SDL_MaximizeWindow(window);
    if (window == nullptr) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    remember_windowed_bounds();
    const bool fullscreen_requested =
        active_settings.graphics.window_mode == GraphicsWindowMode::Fullscreen;
    const char* fullscreen_kind = "none";
    if (fullscreen_requested && !enter_fullscreen(fullscreen_kind)) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    report_graphics_surface(fullscreen_kind);
    window_mode_controller = std::make_unique<WindowModeController>(
        fullscreen_requested, apply_window_mode);
    const char* qa_actions = std::getenv("TETRISPHERE_QA_CONTROLLERS");
    if (qa_actions != nullptr) {
        if (*qa_actions == '\0' || data_dir_override == nullptr ||
            *data_dir_override == '\0') {
            show_error("QA controllers require TETRISPHERE_DATA_DIR and an actions file");
            std::exit(74);
        }
        std::string qa_error;
        if (!qa_virtual_controllers.initialize(std::filesystem::u8path(qa_actions),
                                                user_path, qa_error) ||
            !controller_ports.set_preferred_ids(qa_virtual_controllers.instance_ids())) {
            qa_virtual_controllers.close();
            show_error(qa_error.empty() ? "QA controller port assignment failed" : qa_error.c_str());
            std::exit(74);
        }
        qa_controllers_active = true;
        const auto ids = qa_virtual_controllers.instance_ids();
        std::fprintf(stdout,
                     "{\"event\":\"qa_virtual_controllers\",\"run_id\":\"%s\","
                     "\"p1_instance_id\":%d,\"p2_instance_id\":%d}\n",
                     run_id(), static_cast<int>(ids[0]), static_cast<int>(ids[1]));
        std::fflush(stdout);
    }
    controller_ports.open_all();
    {
        const auto initial = controller_ports.snapshot();
        std::lock_guard lock(input_snapshot_mutex);
        input_snapshot = initial;
        keyboard_snapshot = keyboard_state.buttons();
        if (initial[0].connected)
            active_family.store(select_controller_family(initial[0].name.c_str(), family_override()));
        if (initial[1].connected)
            active_p2_family.store(select_controller_family(initial[1].name.c_str(), family_override()));
    }
    std::fprintf(stdout,
#ifdef _WIN32
                 "{\"event\":\"native_windows_started\",\"run_id\":\"%s\",\"backend\":\"rt64-vulkan\","
#else
                 "{\"event\":\"native_linux_started\",\"run_id\":\"%s\",\"backend\":\"rt64-vulkan\","
#endif
                 "\"controller_family\":\"%s\",\"prompt_confirm\":\"%s\"}\n",
                 run_id(),
                 controller_family_name(active_family.load()),
                 logical_action_label(active_family.load(), controller_bindings(),
                                      LogicalAction::Confirm));
    std::fflush(stdout);

    ultramodern::renderer::GraphicsConfig graphics{};
    graphics.developer_mode = run_options.developer;
    graphics.res_option = ultramodern::renderer::Resolution::Auto;
    graphics.wm_option = fullscreen_requested
        ? ultramodern::renderer::WindowMode::Fullscreen
        : ultramodern::renderer::WindowMode::Windowed;
    graphics.hr_option = ultramodern::renderer::HUDRatioMode::Original;
    graphics.api_option = ultramodern::renderer::GraphicsApi::Vulkan;
    graphics.ar_option = renderer_aspect(active_settings.graphics.aspect);
    graphics.msaa_option = ultramodern::renderer::Antialiasing::None;
    set_rt64_run_options(resolve_graphics_run_options(active_settings.graphics, run_options));
    tetrisphere::sphere::set_detail(
        resolve_graphics_run_options(active_settings.graphics, run_options).sphere_extended.value_or(false)
            ? tetrisphere::sphere::Detail::Extended : tetrisphere::sphere::Detail::Original);
    graphics.rr_option = active_settings.graphics.refresh == GraphicsRefresh::Original
        ? ultramodern::renderer::RefreshRate::Original
        : ultramodern::renderer::RefreshRate::Manual;
    graphics.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    switch (active_settings.graphics.refresh) {
        case GraphicsRefresh::Fps30: graphics.rr_manual_value = 30; break;
        case GraphicsRefresh::Fps60: graphics.rr_manual_value = 60; break;
        case GraphicsRefresh::Fps120: graphics.rr_manual_value = 120; break;
        case GraphicsRefresh::Original: graphics.rr_manual_value = 60; break;
    }
    graphics.ds_option = 1;
    set_rt64_internal_resolution_scale(
        graphics_internal_scale(active_settings.graphics.resolution));
    ultramodern::renderer::set_graphics_config(graphics);

    ultramodern::set_callbacks(
        {rsp_init, run_rsp}, {create_rt64_context},
        {queue_audio, audio_frames_remaining, set_audio_frequency},
        {poll_input, get_input, set_rumble, device_info},
        {}, {count_vi, nullptr}, {show_error}, {});
#ifdef _WIN32
    SDL_SysWMinfo window_info{};
    SDL_VERSION(&window_info.version);
    if (SDL_GetWindowWMInfo(window, &window_info) != SDL_TRUE) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    ultramodern::renderer::WindowHandle native_window{
        window_info.info.win.window, GetCurrentThreadId()};
    ultramodern::preinit(rdram, native_window);
#else
    ultramodern::preinit(rdram, window);
#endif
}

// Current values for the menus' setting rows (assets/ui/screens/options.rml).
// The build label (bottom right): the branch and version, then the graphics
// settings in effect, so a screenshot says how the game was set up (the
// owner's idea, 2026-11-12). Updated whenever the settings change.
void update_build_label() {
#ifdef BLOCKPLANET_BUILD_LABEL
    if (std::getenv("BLOCKPLANET_HIDE_BUILD_LABEL") != nullptr) return;
    const auto& g = active_settings.graphics;
    std::string text = BLOCKPLANET_BUILD_LABEL;
    text += std::string(" \u00B7 sphere ") + graphics_sphere_name(g.sphere);
    text += std::string(" \u00B7 resolution ") + graphics_resolution_name(g.resolution);
    text += std::string(" \u00B7 picture ") + graphics_aspect_name(g.aspect);
    text += std::string(" \u00B7 refresh ") + graphics_refresh_name(g.refresh);
    if (const char* msaa = graphics_msaa_name(g.msaa_samples); g.msaa_samples > 0 && msaa) text += std::string(" \u00B7 MSAA ") + msaa;
    text += std::string(" \u00B7 filter ") + graphics_presentation_filter_name(g.presentation_filter);
    // Started as "classic" (BlockPlanet.bat's choice 8): the game's own sphere drawing and HUD.
    if (const char* sphere = std::getenv("BLOCKPLANET_SPHERE"); sphere && std::string(sphere) == "original") text += " \u00B7 classic sphere";
    if (std::getenv("BLOCKPLANET_ORIGINAL_HUD")) text += " \u00B7 classic HUD";
    blockplanet::ui::set_build_label(text);
#endif
}

void publish_menu_settings(const Settings& settings) {
    update_build_label();
    blockplanet::ui::set_setting("sphere", graphics_sphere_name(settings.graphics.sphere));
    blockplanet::ui::set_setting("aspect", settings.graphics.aspect == GraphicsAspect::Fill ? "fill"
                                           : settings.graphics.aspect == GraphicsAspect::Expand16x9 ? "16x9" : "4x3");
    blockplanet::ui::set_setting("window",
        settings.graphics.window_mode == GraphicsWindowMode::Fullscreen ? "fullscreen" : "windowed");
    blockplanet::ui::set_setting("texture_filter", graphics_texture_filter_name(settings.graphics.three_point_filter));
    blockplanet::ui::set_setting("presentation_filter",
                                 graphics_presentation_filter_name(settings.graphics.presentation_filter));
}

// The audio rows (Options, and the pause menu's Audio): the profile's
// choices (defaults until the player changes one) and, for the pause menu,
// the game's current track. Sent only when changed.
AudioPreferences current_audio() { return profile_data.preferences.audio.value_or(AudioPreferences{}); }

void publish_audio_settings() {
    static std::string last;
    const auto audio = current_audio();
    const int track = guest_rdram != nullptr ? blockplanet::current_track(guest_rdram) : 0;
    const std::string choice = blockplanet::track_override() ? "choose" : audio.random_music ? "random" : "auto";
    const std::string key = std::to_string(audio.music) + "/" +
                            std::to_string(audio.effects) + "/" + choice + "/" + std::to_string(track);
    if (key == last) return;
    last = key;
    blockplanet::ui::set_setting("music_volume", std::to_string(audio.music));
    blockplanet::ui::set_setting("effects_volume", std::to_string(audio.effects));
    blockplanet::ui::set_setting("music", audio.random_music ? "random" : "auto");
    blockplanet::ui::set_setting("music_choice", choice);
    blockplanet::ui::set_setting("track", std::to_string(track));
}

// Ends the pause menu's track override (quitting, or starting another game).
void end_track_override() {
    if (blockplanet::track_override()) blockplanet::set_music_random(current_audio().random_music);
}

// Audio settings from the menus. Music and effects levels and
// Auto/Random are saved in the profile (a guest's last the session); the
// pause menu's Choose and Track play a track now, for this game only.
bool handle_audio_setting(const std::string& command) {
    const auto equals = command.find('=');
    if (command.rfind("setting:", 0) != 0 || equals == std::string::npos) return false;
    const auto name = command.substr(8, equals - 8);
    const auto value = command.substr(equals + 1);
    if (name == "track") {
        blockplanet::play_track(std::atoi(value.c_str()));
        publish_audio_settings();
        return true;
    }
    if (name == "music_choice" && value == "choose") {
        blockplanet::play_track(guest_rdram != nullptr ? blockplanet::current_track(guest_rdram) : 0);
        publish_audio_settings();
        return true;
    }
    if (name != "music_volume" && name != "effects_volume" && name != "music" &&
        name != "music_choice")
        return false;
    auto audio = current_audio();
    const int number = std::clamp(std::atoi(value.c_str()), 0, 100);
    if (name == "music_volume") audio.music = number;
    if (name == "effects_volume") audio.effects = number;
    if (name == "music" || name == "music_choice") audio.random_music = value == "random";
    profile_data.preferences.audio = audio;
    blockplanet::set_audio_levels(audio.music, audio.effects);
    if (name == "music" || name == "music_choice") blockplanet::set_music_random(audio.random_music);
    profile_dirty = true;
    save_profile();
    publish_audio_settings();
    return true;
}

// Graphics > HUD panels, saved in the profile.
bool handle_hud_setting(const std::string& command) {
    const std::string prefix = "setting:hud_distance=";
    if (command.rfind(prefix, 0) != 0) return false;
    const auto value = command.substr(prefix.size());
    if (value != "near" && value != "edges" && value != "follow") return true;
    profile_data.preferences.hud_distance = value;
    profile_dirty = true;
    save_profile();
    blockplanet::ui::set_setting("hud_distance", value);
    return true;
}

void publish_hud_settings() {
    static std::string last, last_text;
    const auto value = profile_data.preferences.hud_distance.value_or("near");
    const auto text = profile_data.preferences.text_size.value_or("default");
    if (text != last_text) blockplanet::ui::set_setting("text_size", last_text = text);
    if (value == last) return;
    last = value;
    blockplanet::ui::set_setting("hud_distance", value);
}

// Graphics > Text size: the card and speech text's size, saved in the profile.
bool handle_text_setting(const std::string& command) {
    const std::string prefix = "setting:text_size=";
    if (command.rfind(prefix, 0) != 0) return false;
    const auto value = command.substr(prefix.size());
    if (value != "small" && value != "default" && value != "large" && value != "larger") return true;
    profile_data.preferences.text_size = value;
    profile_dirty = true;
    save_profile();
    blockplanet::ui::set_setting("text_size", value);
    return true;
}

// Options > Sky test: the brick sky experiment's settings (not saved; each
// change is logged as "brick_sky_settings" so the values can be reported).
bool handle_sky_setting(const std::string& command) {
    const auto equals = command.find('=');
    if (command.rfind("setting:sky_", 0) != 0 || equals == std::string::npos) return false;
    const auto name = command.substr(8, equals - 8);
    if (!tetrisphere::brick_sky::set(name, command.substr(equals + 1))) return true;
    std::string all;
    for (const auto& [setting, value] : tetrisphere::brick_sky::values()) {
        blockplanet::ui::set_setting(setting, value);
        all += std::string(all.empty() ? "" : ",") + "\"" + setting + "\":\"" + value + "\"";
    }
    std::fprintf(stdout, "{\"event\":\"brick_sky_settings\",%s}\n", all.c_str());
    std::fflush(stdout);
    return true;
}

void publish_sky_settings() {
    static bool published = false;
    if (published) return;
    published = true;
    for (const auto& [setting, value] : tetrisphere::brick_sky::values()) blockplanet::ui::set_setting(setting, value);
}

// "setting:NAME=VALUE" from the menus, as a settings change (field numbers
// as in the PC settings menu). False if the command is not a known setting.
bool queue_menu_setting(const std::string& command) {
    const auto equals = command.find('=');
    if (command.rfind("setting:", 0) != 0 || equals == std::string::npos) return false;
    const auto name = command.substr(8, equals - 8);
    const auto value = command.substr(equals + 1);
    Settings requested = persisted_settings;
    int field = -1;
    if (name == "sphere" && parse_graphics_sphere(value, requested.graphics.sphere)) field = 7;
    // Menu values avoid ':' (it separates a value from its label): 16x9, 4x3, fill.
    else if (name == "aspect" && parse_graphics_aspect(value == "16x9" ? "16:9" : value == "4x3" ? "4:3" : value,
                                                       requested.graphics.aspect)) field = 1;
    else if (name == "window" && (value == "fullscreen" || value == "windowed")) {
        requested.graphics.window_mode =
            value == "fullscreen" ? GraphicsWindowMode::Fullscreen : GraphicsWindowMode::Windowed;
        field = 3;
    } else if (name == "texture_filter" && parse_graphics_texture_filter(value, requested.graphics.three_point_filter))
        field = 6;
    else if (name == "presentation_filter" &&
             parse_graphics_presentation_filter(value, requested.graphics.presentation_filter))
        field = 5;
    if (field < 0) return false;
    menu_settings_requests.push_back({requested, field});
    return true;
}

// The Story chapter select: Chapter 0 (Training Basics), the chapters this
// profile has completed, and the next one. A guest gets Chapter 0 and 1.
constexpr int rescue_chapters = 10;  // episodes 1-10 (9 and 10 are bonus episodes)
std::vector<blockplanet::ui::ListItem> chapter_choices() {
    std::set<int> completed;
    if (profile_save) {
        const auto found = profile_data.progress.chapters_completed.find("rescue");
        if (found != profile_data.progress.chapters_completed.end()) completed = found->second;
    }
    return story_chapters(completed, rescue_chapters, unlock_all_testing);
}

// A start has been accepted: close BlockPlanet's menus, remember where the
// player came from, and show the loading cover until play has started.
void begin_launch_presentation(LaunchMode mode) {
    blockplanet::ui::request_close();
    // The loading cover (the owner's request): over the game's own menus
    // while they are pressed through, or the stage's first frames after a
    // direct start, until play has started.
    static const char* names[] = {"Rescue", "Hide & Seek", "Puzzle", "Time Trial", "Versus", "Lines",
                                  "Training", "Training", "Puzzle Training"};
    loading_title = names[std::min<std::size_t>(std::size_t(mode), 8)];
    // Minefield plays in Rescue's mode: its own name on the loading cover.
    if (tetrisphere::minefield_sphere::armed()) loading_title = "Minefield";
    else if (mode == LaunchMode::Rescue) loading_title = variant_title(play_variant);
    // A restart from the pause keeps the menus the game was first started
    // from: the pause screen is not a place to come back to.
    if (const auto path = blockplanet::ui::menu_path();
        !path.empty() && std::none_of(path.begin(), path.end(), [](const auto& s) { return s.first == "pause"; }))
        launch_path = path, launch_place = path.back().first == "stages" ? stages_title_now : std::string();
    loading_since = std::chrono::steady_clock::now();
    loading_active = true;
}

void launch_mode(LaunchMode mode, bool keep_music = false) {
    if (guest_rdram == nullptr) return;
    if (game_option_applier) {
        // Casual (Story without a game over) applies to Story's launches only,
        // and pauses trophies as --casual does.
        // The variants are Rescue's and, from its own menus, Lines'.
        play_lines = mode == LaunchMode::Lines;
        if (mode != LaunchMode::Rescue && !(play_lines && menu_lines)) play_variant = PlayVariant::Story;
        const bool casual = mode == LaunchMode::Rescue && play_variant == PlayVariant::Casual;
        game_option_applier->set_menu_casual(casual);
        trophy_engine().set_awards_enabled(!casual && !game_option_applier->affects_awards());
    }
    if (direct_wait) return;  // a direct start is already on its way
    if (!keep_music) end_track_override();
    // The start card: Rescue launched from the menus (a restart keeps the
    // music and goes straight back into play). BLOCKPLANET_START_CARD=0: none.
    static const bool start_cards = [] {
        const char* text = std::getenv("BLOCKPLANET_START_CARD");
        return text == nullptr || std::string_view(text) != "0";
    }();
    start_card_pending.reset();
    if (mode == LaunchMode::HideAndSeek && !keep_music) hide_and_seek_new_run = true;
    lines_time_reset = true;
    if ((mode == LaunchMode::Rescue || mode == LaunchMode::Puzzle) && !keep_music && start_cards) {
        start_card_pending = std::chrono::steady_clock::now();
        start_card_mode = mode == LaunchMode::Puzzle ? 4 : 1;
    }
    // Story, Training, Puzzle, Lines, Hide & Seek and Versus start directly, without the original menus, from the game's main
    // menu (docs/research/blockplanet-game-state.md, "Starting modes
    // directly"); BLOCKPLANET_LAUNCH=menus keeps the menu route.
    const bool training = mode == LaunchMode::TrainingBasics || mode == LaunchMode::TrainingAdvanced ||
                          mode == LaunchMode::TrainingPuzzle;
    if ((mode == LaunchMode::Rescue || mode == LaunchMode::Puzzle || mode == LaunchMode::Lines ||
         mode == LaunchMode::HideAndSeek || mode == LaunchMode::VsCpu || training) &&
        direct_start::enabled() && game_option_applier && !launching() &&
        guest::load16(guest_rdram, 0x800E121C) >= 0) {
        DirectStartRequest request;
        if (training) {
            request.mode = DirectMode::Training;
            request.training_type = mode == LaunchMode::TrainingBasics ? 1 : mode == LaunchMode::TrainingAdvanced ? 2 : 3;
            // A lesson is a level of its training (context +0x23BA, func_80088E50's
            // lesson): the one chosen in Training's list, else the first.
            request.level = game_option_applier->rescue_start_level() - 1;
        } else {
            if (mode == LaunchMode::Puzzle) request.mode = DirectMode::Puzzle;
            if (mode == LaunchMode::Lines) request.mode = DirectMode::Lines;
            if (mode == LaunchMode::HideAndSeek) request.mode = DirectMode::HideAndSeek;
            if (mode == LaunchMode::VsCpu) request.mode = DirectMode::Versus;
            request.chapter = game_option_applier->rescue_start_chapter() - 1;
            request.level = game_option_applier->rescue_start_level() - 1;
            // The menus clear the chapter (Puzzle: the puzzle is the level);
            // Rescue and Lines start at the episode and stage chosen.
            if (mode != LaunchMode::Rescue && mode != LaunchMode::Lines) request.chapter = 0;
            if (mode == LaunchMode::VsCpu) request.level = 0;
        }
        if (direct_start::request(request)) {
            direct_wait = DirectWait{mode, std::chrono::steady_clock::now()};
            begin_launch_presentation(mode);
            std::fprintf(stdout, "{\"event\":\"mode_launch\",\"mode\":\"%s\",\"status\":\"started\",\"route\":\"direct\"}\n",
                         launch_mode_name(mode));
            std::fflush(stdout);
            return;
        }
    }
    const bool accepted = mode_launcher.request(mode, guest_rdram, std::chrono::steady_clock::now());
    if (accepted) begin_launch_presentation(mode);
    std::fprintf(stdout, "{\"event\":\"mode_launch\",\"mode\":\"%s\",\"status\":\"%s\"}\n",
                 launch_mode_name(mode), accepted ? "started" : "refused");
    std::fflush(stdout);
}

// Saves everything and stops the game's threads.
void stop_game() {
    save_trophies();
    save_profile();
    exited.store(true);
    graphics_shutdown_ready.signal();
    ultramodern::join_event_threads();
    ultramodern::join_thread_cleaner_thread();
}

// Saves everything, stops the game and closes its devices and window.
void shut_down() {
    save_window_placement();
    stop_game();
    // RT64's event-filter userdata dies with its rendering thread. Controller
    // teardown can emit SDL events, so remove the filter before closing devices.
    SDL_SetEventFilter(nullptr, nullptr);
    controller_ports.close();
    qa_virtual_controllers.close();
    audio_output.close();
    SDL_DestroyWindow(window);
    SDL_Quit();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

[[noreturn]] void close_game() {
    shut_down();
    std::exit(0);
}

// Restarts the game as another profile or as a guest (the game reads its
// cartridge save while booting). `remove_id`, if set, is a profile deleted
// once nothing is using its folder any more.
[[noreturn]] void restart_as(std::optional<std::string> profile, bool guest, const std::string& remove_id = {},
                             const std::vector<std::string>& extra = {}) {
    std::fprintf(stdout, "{\"event\":\"profile_restart\",\"profile\":\"%s\",\"guest\":%s}\n",
                 profile ? profile->c_str() : "", guest ? "true" : "false");
    std::fflush(stdout);
    shut_down();
    if (!remove_id.empty()) {
        std::string error;
        if (const auto* removed = profile_by_id(*profile_store, remove_id)) {
            const std::string name = removed->name;
            if (!profile_store->remove(name, error))
                std::fprintf(stderr, "Could not delete the profile: %s\n", error.c_str());
        }
        if (!guest) profile = profile_store->active().name;
    }
    auto arguments = relaunch_arguments(launch_arguments(), profile, guest);
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    if (!relaunch(arguments)) {
        std::fprintf(stderr, "Could not restart BlockPlanet.\n");
        std::exit(75);
    }
    std::exit(0);  // Windows: the new process is running
}

// Changes the playing profile without restarting the game (the owner's
// request, 2026-11-10: no menu item relaunches): everything start-up loads
// for a profile is loaded again, and the game's cartridge save is changed on
// its own thread (direct_start::request_save_switch). `remove_id`: a profile
// to delete once its save is no longer in use (deleting the playing one).
// Who is playing, after "Main menu" in its yellow subtitle (the owner's
// request, 2026-11-13).
void update_player_line() {
    if (!profile_store) return;
    blockplanet::ui::set_text("player_line", std::string(" \u00B7 ") + (guest_session ? std::string("Guest") : profile_store->active().name));
}

// A profile just made, waiting for its coin before the game restarts as it.
std::optional<std::string> new_profile_pending;

void switch_profile(std::optional<std::string> name, bool guest, const std::string& remove_id = {}) {
    auto& profiles = *profile_store;
    save_profile();
    save_trophies();
    std::string error;
    if (!guest && name && !profiles.use(*name, error)) {
        show_error(error.c_str());
        return;
    }
    if (!guest && !name) name = profiles.active().name;
    guest_session = guest;
    tetrisphere::direct_start::set_guest(guest);
    std::filesystem::path profile_path, eeprom_path;
    if (!guest) {
        profile_path = profiles.directory(profiles.active());
        std::error_code ignored;
        std::filesystem::create_directories(profile_path, ignored);
        eeprom_path = profile_path / "cartridge.eep";
        if (!rotate_backup(eeprom_path, profile_path / "backups", 3, error))
            std::fprintf(stderr, "Could not back up the save: %s\n", error.c_str());
    }
    tetrisphere::direct_start::request_save_switch(eeprom_path);
    profile_to_remove = remove_id;
    // BlockPlanet's records and preferences for the profile.
    profile_save.reset();
    profile_data = ProfileSaveData{};
    profile_dirty = false;
    continue_mode.reset();
    pending_restore.reset();
    return_to_menu_chapter.reset();
    single_stage.reset();
    if (!guest) {
        profile_save = std::make_unique<ProfileSave>(profile_path / "save.json");
        if (!profile_save->load(profile_data, error)) {
            std::fprintf(stderr, "Could not load the profile: %s\n", error.c_str());
            profile_data = ProfileSaveData{};
        }
    }
    refresh_continue();
    if (profile_data.preferences.controls) {
        apply_controls(active_settings, *profile_data.preferences.controls);
    } else {
        apply_controls(active_settings, pc_controls);
        if (!guest) {
            profile_data.preferences.controls = pc_controls;
            profile_dirty = true;
            save_profile();
        }
    }
    set_controller_bindings(active_settings.bindings);
    set_magic_control(active_settings.magic);
    profile_recording = !guest && !start_chapter_given;
    if (profile_data.preferences.audio) {
        blockplanet::set_audio_levels(profile_data.preferences.audio->music, profile_data.preferences.audio->effects);
        blockplanet::set_music_random(profile_data.preferences.audio->random_music);
    } else {
        blockplanet::set_audio_levels(100, 100);
        blockplanet::set_music_random(false);
    }
    const auto trophies = profile_path / "trophies.json";
    switch_trophies(trophy_definitions_path, guest ? nullptr : &trophies);
    // A new or changed profile starts at the main menu, not where the last
    // game was started from (it opened Solo after New profile, the owner's
    // report, 2026-11-11).
    launch_path.clear();
    launch_place.clear();
    profile_picked = true;
    update_player_line();
    std::fprintf(stdout, "{\"event\":\"profile_switch\",\"id\":\"%s\",\"guest\":%s}\n",
                 guest ? "" : profiles.active().id.c_str(), guest ? "true" : "false");
    std::fflush(stdout);
    // The main menu opens for the profile (the game's menu check opens it).
    blockplanet::ui::request_close();
}

// The profile screens' commands (profile_menu.h). True if `command` was one.
bool handle_profile_command(const std::string& command) {
    if (!profile_store) return false;
    auto& store = *profile_store;
    const auto argument = [&](const char* prefix) -> std::optional<std::string> {
        const std::string text(prefix);
        if (command.rfind(text, 0) != 0) return std::nullopt;
        return command.substr(text.size());
    };
    // The start-up screens: who is playing, and Back through them.
    if (command == "profile_picked") {
        profile_picked = true;
        open_main_menu();
        return true;
    }
    if (command == "back_to_profiles") {
        profile_picked = false;
        blockplanet::ui::request_close();  // the picker opens in its place
        return true;
    }
    // Back from "Who is playing?" (the owner's design, 2026-11-10): to the box
    // art, over the grid that stays up (Back there plays the launch animation
    // again); without the box art, to the intro, as at boot. Neither restarts
    // the game; "Who is playing?" opens again after the title.
    if (command == "back_to_title") {
        profile_picked = false;
        title_seen = false;
        title_restart_pending = true;
        blockplanet::ui::request_close();
        if (tetrisphere::box_art::enabled()) {
            box_art_returning_since = std::chrono::steady_clock::now();
            tetrisphere::direct_start::request_title_restart();
        } else {
            tetrisphere::direct_start::request_intro_restart();
        }
        return true;
    }
    // Minefield: a size, then the field (one screen per width).
    // Minefield on the sphere: the next Casual 1:1 becomes arena N's field
    // (minefield_sphere::arenas()). Research: BLOCKPLANET_MINEFIELD_MINES sets
    // the count (for testing a loss).
    if (const auto arena = argument("minefield_sphere:")) {
        const char* mines = std::getenv("BLOCKPLANET_MINEFIELD_MINES");
        tetrisphere::minefield_sphere::arm(std::atoi(arena->c_str()), mines ? std::atoi(mines) : -1);
        blockplanet::ui::send_command("casual");
        // Stage 1:1 on its own: at the field's end the game's own stage end
        // plays (the sphere pulls away and fades) and the single stage goes
        // back to the menus (the owner's idea).
        blockplanet::ui::send_command("stage:1:1");
        return true;
    }
    if (command == "minefield") {
        blockplanet::ui::request_show("minefield");
        return true;
    }
    if (command == "profiles") {
        blockplanet::ui::set_list("profiles", profile_list(store, guest_session, !profile_picked));
        blockplanet::ui::request_show("profiles");
        return true;
    }
    // Refreshes the lists that show this profile.
    const auto refresh = [&](const std::string& id) {
        blockplanet::ui::set_list("profiles", profile_list(store, guest_session, !profile_picked));
        blockplanet::ui::set_list("profile_actions", profile_actions(store, id, guest_session));
        blockplanet::ui::set_list("characters", character_choices(store, id));
        blockplanet::ui::set_list("coins", coin_choices(store, id, coin_screen_colours));
    };
    const auto show_name = [&](const std::string& hint) {
        blockplanet::ui::set_text("name_edit", name_editor.text() + "|");
        blockplanet::ui::set_text("name_hint", hint);
    };
    if (const auto id = argument("profile_rename:")) {
        const auto* profile = profile_by_id(store, *id);
        if (profile == nullptr) return true;
        name_editor.start(*id, profile->name);
        blockplanet::ui::set_list("keyboard", name_editor.keyboard());
        show_name("");
        blockplanet::ui::request_show("name");
        return true;
    }
    if (command == "profile_new") {
        name_editor.start("new", "");
        blockplanet::ui::set_list("keyboard", name_editor.keyboard());
        show_name("Leave it empty for \"" + next_profile_name(store) + "\"");
        blockplanet::ui::request_show("name");
        return true;
    }
    if (const auto key = argument("name_key:")) {
        if (name_editor.press(*key)) blockplanet::ui::set_list("keyboard", name_editor.keyboard());
        show_name("");
        return true;
    }
    if (const auto text = argument("name_type:")) {
        name_editor.type(*text);
        show_name("");
        return true;
    }
    if (command == "name_done") {
        std::string name = name_editor.text();
        while (!name.empty() && name.back() == ' ') name.pop_back();
        // "Guest" is for guest play; a profile already called that keeps it.
        const auto* renamed = name_editor.target() == "new" ? nullptr : profile_by_id(store, name_editor.target());
        if (tetrisphere::reserved_profile_name(name) && !(renamed != nullptr && renamed->name == name)) {
            show_name("That name is for guest play");
            return true;
        }
        if (name_editor.target() == "new") {
            if (name.empty()) name = next_profile_name(store);
            if (store.find(name) != nullptr) {
                show_name("That name is taken");
                return true;
            }
            // A new profile chooses its coin before the game restarts as it
            // (the owner's report: it went straight in without one).
            std::string error;
            if (!store.add(name, error)) {
                show_name("Names are letters, digits and symbols");
                return true;
            }
            new_profile_pending = name;
            blockplanet::ui::send_command("profile_coin:" + store.find(name)->id);
            return true;
        }
        const auto* profile = profile_by_id(store, name_editor.target());
        if (profile == nullptr) return true;
        if (name.empty()) {
            show_name("Enter a name");
            return true;
        }
        std::string error;
        const std::string old_name = profile->name;
        if (!store.rename(old_name, name, error)) {
            show_name(store.find(name) != nullptr ? "That name is taken" : "Names are letters, digits and symbols");
            return true;
        }
        refresh(name_editor.target());
        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        return true;
    }
    if (const auto id = argument("profile_character:")) {
        const auto* profile = profile_by_id(store, *id);
        if (profile == nullptr) return true;
        character_profile_id = *id;
        blockplanet::ui::set_text("character_for", "For " + profile->name);
        blockplanet::ui::set_setting("profile_colour", std::to_string(profile->palette));
        blockplanet::ui::set_list("characters", character_choices(store, *id));
        blockplanet::ui::request_show("character");
        return true;
    }
    if (const auto id = argument("profile_coin:")) {
        const auto* profile = profile_by_id(store, *id);
        if (profile == nullptr) return true;
        coin_screen_colours = random_coin_colours();
        blockplanet::ui::set_text("coin_for", "For " + profile->name);
        blockplanet::ui::set_list("coins", coin_choices(store, *id, coin_screen_colours));
        blockplanet::ui::request_show("coin");
        return true;
    }
    // A coin picked: choose its colour, starting at the one it showed.
    if (const auto pick = argument("profile_pick_coin:")) {
        const auto first = pick->find(':');
        const auto second = first == std::string::npos ? first : pick->find(':', first + 1);
        if (second == std::string::npos) return true;
        const auto id = pick->substr(0, first);
        const auto coin = pick->substr(first + 1, second - first - 1);
        const auto* profile = profile_by_id(store, id);
        if (profile == nullptr) return true;
        blockplanet::ui::set_text("coin_colour_for", "For " + profile->name);
        blockplanet::ui::set_list("coin_colours", coin_colour_choices(id, coin, std::atoi(pick->c_str() + second + 1)));
        blockplanet::ui::request_show("coin_colour");
        return true;
    }
    if (const auto choice = argument("profile_set_coin:")) {
        const auto first = choice->find(':');
        const auto second = first == std::string::npos ? first : choice->find(':', first + 1);
        if (second == std::string::npos) return true;
        const auto id = choice->substr(0, first);
        const auto coin = choice->substr(first + 1, second - first - 1);
        const int palette = std::atoi(choice->c_str() + second + 1);
        const auto* profile = profile_by_id(store, id);
        if (profile == nullptr) return true;
        std::string error;
        const std::string name = profile->name;
        const std::string character = profile->character;
        if (!store.set_coin(name, coin == "none" ? "" : coin, error) ||
            !store.set_character(name, character, palette, error))
            std::fprintf(stderr, "Could not set the coin: %s\n", error.c_str());
        // A new profile: its coin chosen, the game restarts as it.
        if (new_profile_pending && *new_profile_pending == name) {
            new_profile_pending.reset();
            switch_profile(name, false);
            return true;
        }
        refresh(id);
        // Back to the profile: from the coin screen, or from its colour step.
        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        if (coin != "none") blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        return true;
    }
    if (const auto choice = argument("profile_set_character:")) {
        const auto colon = choice->find(':');
        if (colon == std::string::npos) return true;
        const auto id = choice->substr(0, colon);
        const auto character = choice->substr(colon + 1);
        const auto* profile = profile_by_id(store, id);
        if (profile == nullptr) return true;
        std::string error;
        const std::string name = profile->name;
        if (!store.set_character(name, character == "none" ? "" : character, profile->palette, error))
            std::fprintf(stderr, "Could not set the character: %s\n", error.c_str());
        refresh(id);
        return true;
    }
    if (const auto colour = argument("setting:profile_colour=")) {
        const auto* profile = profile_by_id(store, character_profile_id);
        if (profile == nullptr) return true;
        std::string error;
        const std::string name = profile->name;
        const int palette = std::atoi(colour->c_str());
        if (!store.set_character(name, profile->character, palette, error))
            std::fprintf(stderr, "Could not set the colour: %s\n", error.c_str());
        blockplanet::ui::set_setting("profile_colour", std::to_string(profile_by_id(store, character_profile_id)->palette));
        refresh(character_profile_id);  // the profile list's coin takes the new colour
        return true;
    }
    if (const auto id = argument("profile:")) {
        blockplanet::ui::set_list("profile_actions", profile_actions(store, *id, guest_session));
        blockplanet::ui::request_show("profile");
        return true;
    }
    if (const auto id = argument("profile_play:")) {
        if (const auto* profile = profile_by_id(store, *id)) switch_profile(profile->name, false);
        return true;
    }
    if (command == "profile_guest") {
        switch_profile(std::nullopt, true);
        return true;
    }
    if (command == "profile_guest_button") {
        const auto action = profile_guest_action(guest_session, !profile_picked);
        if (action == "close") blockplanet::ui::request_close();
        else blockplanet::ui::send_command(action);
        return true;
    }
    if (const auto id = argument("profile_delete_confirmed:")) {
        const auto* profile = profile_by_id(store, *id);
        if (profile == nullptr) return true;
        // The playing profile: change to another (the next in the list, or
        // a guest) first; it is removed once its save is no longer in use.
        if (!guest_session && profile->id == store.active().id) {
            const Profile* other = nullptr;
            for (const auto& candidate : store.profiles())
                if (candidate.id != profile->id) { other = &candidate; break; }
            if (other != nullptr) switch_profile(other->name, false, *id);
            else switch_profile(std::nullopt, true, *id);
            return true;
        }
        std::string error;
        const std::string name = profile->name;
        if (!store.remove(name, error)) std::fprintf(stderr, "Could not delete the profile: %s\n", error.c_str());
        // Back to the (updated) profile list.
        blockplanet::ui::set_list("profiles", profile_list(store, guest_session, !profile_picked));
        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        return true;
    }
    if (const auto id = argument("profile_delete:")) {
        blockplanet::ui::set_text("confirm_title", "Are you sure?");
        blockplanet::ui::set_text("confirm_note", "Deleted profiles can be restored for 30 days");
        blockplanet::ui::set_list("confirm", delete_confirmation(store, *id));
        blockplanet::ui::request_show("confirm");
        return true;
    }
    return false;
}

[[noreturn]] void run_linux_event_loop() {
    SDL_Event event{};
    const auto event_loop_start = std::chrono::steady_clock::now();
    auto next_audio_report = event_loop_start + std::chrono::seconds(10);
    auto next_trophy_save = event_loop_start + std::chrono::seconds(30);
    const char* audio_metrics = std::getenv("TETRISPHERE_AUDIO_METRICS");
    const bool report_audio = audio_metrics != nullptr && std::strcmp(audio_metrics, "1") == 0;
    constexpr auto perf_interval = std::chrono::seconds(5);
    auto perf_window_start = event_loop_start;
    std::uint64_t perf_last_vis = observed_vis.load(std::memory_order_relaxed);
    std::uint64_t perf_last_rejections =
        tetrisphere_ai_fifo_rejections.load(std::memory_order_relaxed);
    if (perf_log_enabled) {
        // Start the first window now so boot work is not counted.
        auto& metrics = perf_metrics();
        metrics.display_list.take(); metrics.display_list_rt64.take();
        metrics.screen_update.take(); metrics.audio_wait.take();
        std::fprintf(stdout, "{\"event\":\"perf_log_started\",\"interval_ms\":%lld,"
                     "\"present_mode\":\"%s\"}\n",
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                         perf_interval).count()), perf_present_mode);
        std::fflush(stdout);
    }
    for (;;) {
        for (;;) {
            auto request=take_pc_settings_change();
            if(!request) {
                if(auto controls=take_controls_apply())request=PCSettingsRequest{*controls,13};
            }
            if(!request && !menu_settings_requests.empty()) {
                request=menu_settings_requests.front();
                menu_settings_requests.pop_front();
            }
            if(!request)break;
            const auto merged=merge_pc_settings_change(persisted_settings,request->settings,request->field);
            const auto* requested=&merged;
            std::string error;
            SettingsStore store(application_data_directory() / "settings.dat");
            // settings.dat keeps the PC's default controls; a profile's own
            // controls go to its save file, and a guest's last the session.
            auto on_disk = *requested;
            apply_controls(on_disk, pc_controls);
            if (request->field==13 && profile_save) {
                profile_data.preferences.controls = controls_of(*requested);
                profile_dirty = true;
                std::string save_error;
                if (!profile_save->save(profile_data, save_error)) {
                    std::fprintf(stderr, "Could not save the profile's controls: %s\n", save_error.c_str());
                    acknowledge_controls_settings(persisted_settings);
                    complete_controls_apply(false);
                    continue;
                }
                profile_dirty = false;
            }
            if (!store.save(on_disk, error)) {
                std::fprintf(stderr, "Could not save PC options: %s\n", error.c_str());
                acknowledge_controls_settings(persisted_settings);
                if(request->field==13)complete_controls_apply(false);
                continue;
            }
            persisted_settings=merged;
            acknowledge_controls_settings(persisted_settings);
            bool window_applied = true;
            if (request->field==3) window_applied=window_mode_controller->set_fullscreen(
                requested->graphics.window_mode == GraphicsWindowMode::Fullscreen);
            active_settings.family_override=requested->family_override;
            active_settings.bindings=requested->bindings;
            active_settings.magic=requested->magic;
            if(request->field==5)active_settings.graphics.presentation_filter=requested->graphics.presentation_filter;
            if(request->field==1) {
                // Picture: applies at once (the renderer changes its picture shape live).
                active_settings.graphics.aspect=requested->graphics.aspect;
                auto config = ultramodern::renderer::get_graphics_config();
                config.ar_option = renderer_aspect(requested->graphics.aspect);
                ultramodern::renderer::set_graphics_config(config);
            }
            if(request->field==6)active_settings.graphics.three_point_filter=requested->graphics.three_point_filter;
            if(request->field==7) {
                active_settings.graphics.sphere=requested->graphics.sphere;
                // Applies from the next frame. A --sphere option on the
                // command line only sets the starting value.
                tetrisphere::sphere::set_detail(requested->graphics.sphere==GraphicsSphere::Extended
                    ? tetrisphere::sphere::Detail::Extended : tetrisphere::sphere::Detail::Original);
            }
            set_controller_bindings(requested->bindings);
            set_magic_control(requested->magic);
            const auto ports = controller_ports.snapshot();
            if (active_family.load() != ControllerFamily::Keyboard && ports[0].connected)
                active_family.store(select_controller_family(ports[0].name.c_str(), family_override()));
            if (active_p2_family.load() != ControllerFamily::Keyboard && ports[1].connected)
                active_p2_family.store(select_controller_family(ports[1].name.c_str(), family_override()));
            set_rt64_live_filters(request->field==5
                ? std::optional{requested->graphics.presentation_filter} : std::nullopt,
                request->field==6 ? std::optional{requested->graphics.three_point_filter} : std::nullopt);
            if(request->field==13) {
                complete_controls_apply(true);
            }
            publish_menu_settings(persisted_settings);
            std::fprintf(stdout, "{\"event\":\"pc_options_saved\",\"window_applied\":%s}\n",
                         window_applied ? "true" : "false");
            std::fflush(stdout);
        }
        if (take_close_game_request()) {
            std::fprintf(stdout, "{\"event\":\"close_game_requested\"}\n");
            std::fflush(stdout);
            close_game();
        }
        if (qa_controllers_active) {
            std::vector<QaControllerAction> applied;
            std::string qa_error;
            if (!qa_virtual_controllers.pump(applied, qa_error)) {
                std::fprintf(stderr, "QA controller input failed: %s\n", qa_error.c_str());
                std::fflush(stderr);
                controller_ports.close();
                qa_virtual_controllers.close();
                std::exit(74);
            }
            for (const auto& action : applied) {
                std::fprintf(stdout,
                             "{\"event\":\"qa_virtual_action\",\"run_id\":\"%s\","
                             "\"seq\":%llu,\"port\":%d,\"button\":%d,\"pressed\":%s}\n",
                             run_id(), static_cast<unsigned long long>(action.seq),
                             action.port, static_cast<int>(action.button),
                             action.pressed ? "true" : "false");
            }
            if (!applied.empty()) std::fflush(stdout);
        }
        if (window_placement_dirty && std::chrono::steady_clock::now() - *window_placement_dirty > std::chrono::seconds(1))
            save_window_placement();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                close_game();
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                window_mode_controller->focus_lost();
            }
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_MOVED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 event.window.event == SDL_WINDOWEVENT_MAXIMIZED || event.window.event == SDL_WINDOWEVENT_RESTORED))
                window_placement_dirty = std::chrono::steady_clock::now();
            const auto before_removal = event.type == SDL_CONTROLLERDEVICEREMOVED
                ? controller_ports.snapshot()
                : std::array<ControllerPortSnapshot, 2>{};
            const int active_port = controller_ports.handle_event(event);
            if (active_port >= 0) {
                const auto current = controller_ports.snapshot();
                const auto family = select_controller_family(current[active_port].name.c_str(),
                                                             family_override());
                if (active_port == 0) active_family.store(family);
                else active_p2_family.store(family);
            }
            std::string audio_error;
            if (audio_output.handle_event(event, audio_error)) {
                diagnostic_log().audio(audio_error.empty() ? "reopened" : "unavailable");
                std::fprintf(audio_error.empty() ? stdout : stderr,
                             "{\"event\":\"audio_device_change\",\"status\":\"%s\"}\n",
                             audio_error.empty() ? "reopened" : "unavailable");
                std::fflush(audio_error.empty() ? stdout : stderr);
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (before_removal[0].instance_id == event.cdevice.which)
                    active_family.store(ControllerFamily::Keyboard);
                if (before_removal[1].instance_id == event.cdevice.which)
                    active_p2_family.store(ControllerFamily::Keyboard);
            }
            if (keyboard_state.handle_event(event)) {
                active_family.store(ControllerFamily::Keyboard);
            }
            const bool removed_assigned=event.type==SDL_CONTROLLERDEVICEREMOVED &&
                (before_removal[0].instance_id==event.cdevice.which || before_removal[1].instance_id==event.cdevice.which);
            int event_port=active_port;
            if(event.type==SDL_CONTROLLERAXISMOTION || event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_CONTROLLERBUTTONUP) {
                const auto ports=controller_ports.snapshot();
                const auto id=event.type==SDL_CONTROLLERAXISMOTION ? event.caxis.which:event.cbutton.which;
                for(int i=0;i<2;++i)if(ports[i].connected && ports[i].instance_id==id)event_port=i;
            }
            const auto detected=event_port>=0
                ? detect_controller_family(controller_ports.snapshot()[event_port].name.c_str())
                : ControllerFamily::Keyboard;
            // Research aid: with BLOCKPLANET_DUMP_DIR set, F2 saves the guest's 8 MB
            // of memory there (as held in memory: 32-bit words in host order).
            if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F2 &&
                std::getenv("BLOCKPLANET_DUMP_DIR") != nullptr && guest_rdram != nullptr) {
                static int dumps = 0;
                const auto path = std::filesystem::u8path(std::getenv("BLOCKPLANET_DUMP_DIR")) /
                                  ("rdram-" + std::to_string(++dumps) + ".bin");
                std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(guest_rdram), 0x800000);
                std::fprintf(stdout, "{\"event\":\"rdram_dump\",\"path\":\"%s\"}\n", path.string().c_str());
                std::fflush(stdout);
                continue;
            }
            // The card panel's scrolling (when its text is taller than the screen
            // allows): the right stick (the game does not use it), Page Up /
            // Page Down and the mouse wheel. Outside the menus only.
            if (!blockplanet::ui::menus_open()) {
                if (event.type == SDL_CONTROLLERAXISMOTION && event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY)
                    card_stick = std::abs(event.caxis.value) > 8000 ? float(event.caxis.value) / 32767.0f : 0.0f;
                if (event.type == SDL_KEYDOWN && (event.key.keysym.scancode == SDL_SCANCODE_PAGEUP ||
                                                  event.key.keysym.scancode == SDL_SCANCODE_PAGEDOWN))
                    blockplanet::ui::scroll_card(event.key.keysym.scancode == SDL_SCANCODE_PAGEUP ? -3.0f : 3.0f);
                if (event.type == SDL_MOUSEWHEEL && blockplanet::ui::card_needs_scroll())
                    blockplanet::ui::scroll_card(-2.0f * float(event.wheel.y));
                // The mouse in play (the owner's design, 2026-11-15): see
                // include/tetrisphere/mouse_play.h.
                if (window != nullptr) {
                    int w = 0, h = 0;
                    SDL_GetWindowSize(window, &w, &h);
                    mouse_play.set_window_height(h);
                }
                if (event.type == SDL_MOUSEMOTION) mouse_play.motion(event.motion.xrel, event.motion.yrel);
                if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
                    mouse_play.button(event.button.button, event.type == SDL_MOUSEBUTTONDOWN);
                if (event.type == SDL_MOUSEWHEEL && !blockplanet::ui::card_needs_scroll()) {
                    const int flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
                    mouse_play.wheel(flip * event.wheel.x, flip * event.wheel.y);
                }
            } else {
                mouse_play.reset();
            }
            // The mouse in the menus (the owner's request, 2026-11-15): pointing
            // focuses an item, the left button chooses it, the right button goes
            // back, the wheel moves up and down.
            if (blockplanet::ui::menus_open() && window != nullptr) {
                int w = 0, h = 0;
                SDL_GetWindowSize(window, &w, &h);
                if (w > 0 && h > 0) {
                    if (event.type == SDL_MOUSEMOTION) {
                        blockplanet::ui::pointer(float(event.motion.x) / float(w), float(event.motion.y) / float(h), false);
                        continue;
                    }
                    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                        blockplanet::ui::pointer(float(event.button.x) / float(w), float(event.button.y) / float(h), true);
                        continue;
                    }
                    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
                        continue;
                    }
                    if (event.type == SDL_MOUSEWHEEL && event.wheel.y != 0) {
                        blockplanet::ui::queue_action(event.wheel.y > 0 ? blockplanet::ui::Action::Up : blockplanet::ui::Action::Down);
                        continue;
                    }
                }
            }
            if (handle_menu_event(event, event_port>=0, active_settings.bindings)) continue;
            handle_controls_event(event,event_port>=0 || removed_assigned,detected);
        }
        {
            // The right stick scrolls the card panel smoothly: up to 8 lines a second.
            static auto last = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            const float seconds = std::min(0.1f, std::chrono::duration<float>(now - last).count());
            last = now;
            if (card_stick != 0.0f) blockplanet::ui::scroll_card(card_stick * 8.0f * seconds);
        }
        bool held=keyboard_state.buttons()!=0;
        for(const auto& port:controller_ports.snapshot()) {
            if(!port.connected)continue;
            const auto& s=port.sample;
            held=held || s.south || s.east || s.west || s.north || s.back || s.start ||
                s.dpad_up || s.dpad_down || s.dpad_left || s.dpad_right ||
                s.left_shoulder || s.right_shoulder || s.trigger_left>8000 || s.trigger_right>8000 ||
                std::abs(s.left_x)>8000 || std::abs(s.left_y)>8000;
        }
        if((controls_editor_blocks_game() || pc_settings_blocks_game()) && !controls_editor_active() && !pc_settings_active() && !held) {
            // Refresh the guest snapshot before releasing the input quarantine.
            // Otherwise the last polled held Apply button could enter the guest again.
            const auto current=controller_ports.snapshot();
            std::lock_guard lock(input_snapshot_mutex);
            input_snapshot=current;keyboard_snapshot=keyboard_state.buttons();
        }
        controls_editor_release_guard(held);
        if (blockplanet::ui::menus_open()) menus_block_game.store(true);
        else if (menus_block_game.load() && !held) {
            // As above: refresh the snapshot before letting input through.
            const auto current=controller_ports.snapshot();
            std::lock_guard lock(input_snapshot_mutex);
            input_snapshot=current;keyboard_snapshot=keyboard_state.buttons();
            menus_block_game.store(false);
        }
        while (auto command = blockplanet::ui::take_command()) {
            std::fprintf(stdout, "{\"event\":\"ui_command\",\"command\":\"%s\"}\n", command->c_str());
            std::fflush(stdout);
            if (*command == "exit") close_game();
            // Credits (the main menu): BlockPlanet's own credits screen (the
            // owner's choice, 2026-11-08), built from the game's credits data
            // in memory (from the player's ROM): 125 entries, name pointers at
            // 0x800E031C, each entry's role group at 0x800E0512 and its line
            // layout at 0x800E0618 (2: a two-line entry). The game's own
            // credits sequence still plays with --credits (research).
            if (*command == "credits" && guest_rdram != nullptr) {
                std::vector<tetrisphere::CreditEntry> entries;
                int previous_layout = 0;
                bool previous_first = false;
                for (std::uint32_t k = 0; k < 125; ++k) {
                    tetrisphere::CreditEntry entry;
                    entry.group = guest::load16(guest_rdram, 0x800E0512u + 2 * k);
                    const int layout = guest::load16(guest_rdram, 0x800E0618u + 2 * k);
                    std::uint32_t at = guest::load32(guest_rdram, 0x800E031Cu + 4 * k);
                    for (int n = 0; n < 64 && at != 0; ++n, ++at) {
                        const char c = char(guest::load8(guest_rdram, at));
                        if (c == 0) break;
                        entry.name += c;
                    }
                    entry.line2 = layout == 2 && previous_layout == 2 && previous_first &&
                                  !entries.empty() && entries.back().group == entry.group;
                    previous_first = layout == 2 && !entry.line2;
                    previous_layout = layout;
                    entries.push_back(entry);
                }
                std::string text;
                for (const auto& line : tetrisphere::credit_lines(entries)) text += line + "\n";
                for (const auto& line : tetrisphere::blockplanet_credit_lines()) text += line + "\n";
                blockplanet::ui::set_text("credits", text);
                blockplanet::ui::request_show("credits");
                continue;
            }
            // F5 (testing): restart the game from the title, as the same
            // profile (or guest), with the same launch options.
            if (*command == "reset") restart_as(std::nullopt, guest_session);
            if (handle_audio_setting(*command)) continue;
            if (handle_hud_setting(*command)) continue;
            if (handle_text_setting(*command)) continue;
            if (handle_sky_setting(*command)) continue;
            if (queue_menu_setting(*command)) continue;
            if (handle_profile_command(*command)) continue;
            if (*command == "audio_screen") {
                // Options > Audio: in a game (paused) the version with Choose and
                // Track; otherwise Auto or Random only.
                const bool in_game = guest_rdram != nullptr && read_game_state(guest_rdram).scene == -1;
                blockplanet::ui::request_show(in_game ? "audio" : "audio_options");
                continue;
            }
            if (*command == "pause") {
                pause_bridge.request_pause(std::chrono::steady_clock::now());
                continue;
            }
            if (command->rfind("research_drop_timer:", 0) == 0 && guest_rdram != nullptr) {
                // Research: the drop timer set this many units short of its limit (the camera closes in near the end).
                float limit = 0.0f;
                const std::uint32_t bits = guest::load32(guest_rdram, 0x800F116Cu);
                std::memcpy(&limit, &bits, sizeof limit);
                const float value = std::max(0.0f, limit - float(std::atof(command->c_str() + 20)));
                std::uint32_t out = 0;
                std::memcpy(&out, &value, sizeof out);
                guest::store32(const_cast<std::uint8_t*>(guest_rdram), 0x80110220u + 0x2408, out);
                std::fprintf(stderr, "DROPTIMER limit %.0f set %.0f\n", limit, value);
                continue;
            }
            if (*command == "research_diff_start") { diff_start = std::chrono::steady_clock::now(); diff_mark.reset(); continue; }
            if (*command == "research_diff_mark") { diff_mark = std::chrono::steady_clock::now(); continue; }
            if (*command == "research_minefield_lose") {
                tetrisphere::minefield_sphere::research_lose();
                continue;
            }
            if (*command == "minefield_lost_again" || *command == "minefield_lost_exit") {
                // From a lost field's Game Over panel: leave play through the pause
                // (as a single stage ends), then a new field, or back to its menu.
                blockplanet::ui::request_close();
                if (*command == "minefield_lost_again") {
                    tetrisphere::minefield_sphere::again();
                    continue_mode = PendingLaunch{LaunchMode::Rescue, std::pair{1, 1}, true};
                }
                grid_quit_at = std::chrono::steady_clock::now(), grid_quit_red = true;
                single_stage_quit_at = std::chrono::steady_clock::now();
                continue;
            }
            if (*command == "minefield_again") {
                // A new field in the same arena: Casual 1:1 again, kept armed.
                tetrisphere::minefield_sphere::again();
                continue_mode = PendingLaunch{LaunchMode::Rescue, std::pair{1, 1}, true};
                pause_bridge.choose(PauseBridge::Choice::Quit, std::chrono::steady_clock::now());
                continue;
            }
            if (*command == "pause_restart_chapter") {
                // Story and Hide & Seek: ask first; Yes restarts the episode (Rescue) or
                // chapter from its first level, or a single stage from its start.
                const bool rescue = guest_rdram != nullptr && guest::load16(guest_rdram, 0x800E44A8) == 1;
                blockplanet::ui::set_text("confirm_title", single_stage ? "Restart this stage?" : rescue ? "Restart episode?" : "Restart chapter?");
                blockplanet::ui::set_text("confirm_note", single_stage ? "The stage starts again from the beginning."
                                                          : rescue ? "Progress within the episode will be lost."
                                                                   : "Progress within the chapter will be lost.");
                blockplanet::ui::set_list("confirm", {{"yes", "Yes", "", "command:pause_restart", true},
                                                      {"no", "No", "", "back", true, 1, true}});
                blockplanet::ui::request_show("confirm");
                continue;
            }
            if (*command == "pause_restart" && guest_rdram != nullptr) {
                const bool restart_single_lesson = single_lesson.has_value();
                // Quit through the game's pause, then start the same place again.
                const auto state = read_game_state(guest_rdram);
                std::optional<LaunchMode> mode;
                std::optional<std::pair<int, int>> start;
                switch (state.mode) {
                // The episode from its first stage, or a single stage itself.
                case 1: mode = LaunchMode::Rescue; start = std::pair{state.chapter + 1, single_stage ? state.level + 1 : 1}; break;
                case 5: mode = LaunchMode::HideAndSeek; start = std::pair{0, 1}; break;  // its own chapter, level 1
                case 4: mode = LaunchMode::Puzzle; start = std::pair{state.chapter + 1, state.level + 1}; break;
                case 2: mode = LaunchMode::TimeTrial; break;
                case 6: mode = LaunchMode::VsCpu; break;
                // Lines: the episode from its first stage, or a single stage itself.
                case 9: mode = LaunchMode::Lines; start = std::pair{state.chapter + 1, single_stage ? state.level + 1 : 1}; break;
                case 3:
                    // The same lesson (a level of its training), and, when it was
                    // chosen on its own, still on its own.
                    start = std::pair{0, guest::load16(guest_rdram, 0x80110220u + 0x23BA) + 1};
                    if (state.training_type == 1) mode = LaunchMode::TrainingBasics;
                    if (state.training_type == 2) mode = LaunchMode::TrainingAdvanced;
                    if (state.training_type == 3) mode = LaunchMode::TrainingPuzzle;
                    break;
                default: break;
                }
                if (mode) {
                    if (state.mode == 3 && restart_single_lesson) single_lesson = start->second - 1;
                    continue_mode = PendingLaunch{*mode, start, true};
                    pause_bridge.choose(PauseBridge::Choice::Quit, std::chrono::steady_clock::now());
                    std::fprintf(stdout, "{\"event\":\"restart\",\"mode\":\"%s\"}\n", launch_mode_name(*mode));
                    std::fflush(stdout);
                }
                continue;
            }
            if (*command == "pause_howto") {
                const int mode = guest::load16(guest_rdram, 0x800E44A8);
                blockplanet::ui::set_text("howto_title", mode == 1 ? std::string(variant_title(play_variant)) : mode == 4 ? "Puzzle"
                                                         : mode == 5 ? "Hide & Seek" : mode == 6 ? "VS" : "Lines");
                blockplanet::ui::set_text("howto_body", how_to_play(mode));
                blockplanet::ui::request_show("howto");
                continue;
            }
            if (command->rfind("end_choose:", 0) == 0) {
                // The end screen: the game's row for that item. Exit goes back
                // to the menus the game was started from, as Quit to menu does.
                const int row = std::atoi(command->c_str() + 11);
                if (row == end_menu.exit_row())
                    end_track_override(), return_to_menu_chapter = 0, grid_quit_at = std::chrono::steady_clock::now(),
                    grid_quit_red = !end_menu.episode_end();
                end_menu.choose(row, std::chrono::steady_clock::now());
                continue;
            }
            if (*command == "pause_resume" || *command == "pause_skip" || *command == "pause_quit") {
                if (*command == "pause_quit") end_track_override(), return_to_menu_chapter = 0, grid_quit_at = std::chrono::steady_clock::now();
                pause_bridge.choose(*command == "pause_resume" ? PauseBridge::Choice::Resume
                                    : *command == "pause_skip" ? PauseBridge::Choice::Skip
                                                               : PauseBridge::Choice::Quit,
                                    std::chrono::steady_clock::now());
                continue;
            }
            if (*command == "continue") {
                const auto last = profile_data.progress.last_play;
                if (!last.submode.empty() && last.chapter >= 1 && last.level >= 1 && game_option_applier) {
                    continue_mode.reset();
                    single_stage.reset();
                    single_stage_quit_at.reset();
                    single_stage_result.reset();
                    menu_variant = play_variant = last.submode == "casual" ? PlayVariant::Casual : PlayVariant::Story;
                    chapter_zero_lesson.clear();
                    game_option_applier->choose_start(last.chapter, 1);
                    pending_restore = last;
                    launch_mode(LaunchMode::Rescue);
                }
                continue;
            }
            if (*command == "story" || *command == "time_attack" || *command == "score_attack" || *command == "casual") {
                menu_lines = false;
                menu_variant = *command == "story" ? PlayVariant::Story
                               : *command == "casual" ? PlayVariant::Casual
                               : *command == "time_attack" ? PlayVariant::TimeAttack : PlayVariant::ScoreAttack;
                auto choices = chapter_choices();
                blockplanet::ui::set_text("chapters_title", menu_variant == PlayVariant::Story ? "Campaign" : variant_title(menu_variant));
                blockplanet::ui::set_text("chapters_subtitle", "Choose an episode");
                blockplanet::ui::set_list("chapters", choices);
                blockplanet::ui::request_show("chapters");
                continue;
            }
            if (*command == "open_story") {
                menu_lines = false;
                // Solo > Rescue: until Episode 1 is done it launches Episode 1
                // straight away; afterwards its menu (docs/MENU_STRUCTURE.md).
                if (tetrisphere::rescue_menu_open(menu_unlocks_now())) {
                    blockplanet::ui::request_show("story");
                } else {
                    menu_variant = PlayVariant::Story;
                    blockplanet::ui::send_command("chapter:1");
                }
                continue;
            }
            if (*command == "open_lines") {
                // Solo > Bonus > Lines: as Rescue, Episode 1 straight away until
                // it is done; afterwards Lines' menu.
                const auto& done = profile_data.progress.chapters_completed;
                const bool opened = menu_unlocks_now().everything || (done.count("lines") && done.at("lines").count(1));
                menu_lines = true;
                menu_variant = PlayVariant::Story;
                if (opened) blockplanet::ui::request_show("lines_menu");
                else blockplanet::ui::send_command("chapter:1");
                continue;
            }
            if (*command == "lines_time_attack" || *command == "lines_score_attack") {
                menu_lines = true;
                menu_variant = *command == "lines_time_attack" ? PlayVariant::TimeAttack : PlayVariant::ScoreAttack;
                std::set<int> completed;
                if (const auto found = profile_data.progress.chapters_completed.find("lines");
                    found != profile_data.progress.chapters_completed.end())
                    completed = found->second;
                blockplanet::ui::set_text("chapters_title", std::string("Lines ") + variant_title(menu_variant));
                blockplanet::ui::set_text("chapters_subtitle", "Choose an episode");
                blockplanet::ui::set_list("chapters", story_chapters(completed, lines_chapters, unlock_all_testing));
                blockplanet::ui::request_show("chapters");
                continue;
            }
            if (*command == "campaign" || *command == "lines_campaign") {
                // Campaign: on from the next stage not yet completed (the
                // owner's plan; earlier stages are replayed in the attack modes).
                // Rescue's, or Lines' (lines_campaign).
                const bool lines = *command == "lines_campaign";
                menu_lines = lines;
                const char* game = lines ? "lines" : "rescue";
                const int episodes_in_game = lines ? lines_chapters : rescue_chapters;
                menu_variant = PlayVariant::Story;
                const auto& levels = profile_data.progress.levels_completed;
                const auto& chapters = profile_data.progress.chapters_completed;
                const auto episodes = chapters.find(game);
                int episode = 1;
                while (episode <= episodes_in_game && episodes != chapters.end() && episodes->second.count(episode)) ++episode;
                if (episode > episodes_in_game) {
                    // Every episode done: the episode list, to play any again.
                    if (lines) {
                        blockplanet::ui::set_text("chapters_title", "Lines Campaign");
                        blockplanet::ui::set_text("chapters_subtitle", "Choose an episode");
                        blockplanet::ui::set_list("chapters", story_chapters(episodes->second, lines_chapters, unlock_all_testing));
                        blockplanet::ui::request_show("chapters");
                    } else {
                        blockplanet::ui::send_command("story");
                    }
                    continue;
                }
                int stage = 1;
                if (const auto found = levels.find(game); found != levels.end())
                    if (const auto c = found->second.find(episode); c != found->second.end())
                        while (stage < 10 && c->second.count(stage)) ++stage;
                blockplanet::ui::send_command("chapter:" + std::to_string(episode) + ":" + std::to_string(stage));
                continue;
            }
            if (command->rfind("lesson:", 0) == 0 && game_option_applier) {
                // lesson:T:L: Training type T (1 Basics, 2 Advanced, 3 Puzzle), lesson L (1-based).
                const int type = std::atoi(command->c_str() + 7);
                const auto colon = command->find(':', 7);
                const int lesson = colon == std::string::npos ? 1 : std::atoi(command->c_str() + colon + 1);
                single_stage.reset();
                continue_mode.reset();
                chapter_zero_lesson.clear();  // back to the menus afterwards
                single_lesson = lesson - 1;  // the lesson's level (0-based)
                game_option_applier->choose_start(0, lesson);
                launch_mode(type == 2 ? LaunchMode::TrainingAdvanced : type == 3 ? LaunchMode::TrainingPuzzle : LaunchMode::TrainingBasics);
                continue;
            }
            if (command->rfind("research_text:", 0) == 0 && guest_rdram != nullptr) {
                // Research: the guest's text at an address, up to 300 bytes ("research_text:800EEED0").
                std::uint32_t a = std::uint32_t(std::strtoul(command->c_str() + 14, nullptr, 16));
                std::string t;
                for (int k = 0; k < 300; ++k) {
                    const char ch = char(guest::load8(guest_rdram, a + std::uint32_t(k)));
                    t += ch == 0 ? '|' : (ch >= 32 && ch < 127 ? ch : '?');
                }
                std::fprintf(stderr, "TEXT %08X %s\n", a, t.c_str());
                continue;
            }
            if (command->rfind("research_card:", 0) == 0) {
                const auto t = find_card_text(command->substr(14));
                std::fprintf(stderr, "CARD %s => %s || %s\n", command->c_str() + 14, t ? t->c_str() : "-",
                             t ? tetrisphere::text_rewrite(*t).value_or("(same)").c_str() : "-");
                continue;
            }
            if (*command == "research_dump_board" && guest_rdram != nullptr) {
                const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u);
                for (int layer = 1; layer <= 2; ++layer)
                    for (int y = 8; y < 24; ++y) {
                        std::string row;
                        for (int x = 8; x < 24; ++x) {
                            const int v = guest::load16(guest_rdram, values + 2u * std::uint32_t(layer * 1024 + y * 32 + x));
                            char b[8]; std::snprintf(b, sizeof b, "%4d", v); row += b;
                        }
                        std::fprintf(stderr, "BOARD L%d y%2d %s\n", layer, y, row.c_str());
                    }
                std::fprintf(stderr, "BOARD cursor %d %d\n", 8 + guest::load16(guest_rdram, 0x80110220u + 0x2B2A), 8 + guest::load16(guest_rdram, 0x80110220u + 0x2B2E));
                continue;
            }
            if (command->rfind("puzzle_again:", 0) == 0) {
                // The puzzle just solved, once more: quit through the pause and start it again.
                continue_mode = PendingLaunch{LaunchMode::Puzzle, std::pair{1, std::atoi(command->c_str() + 13)}, true};
                pause_bridge.choose(PauseBridge::Choice::Quit, std::chrono::steady_clock::now());
                continue;
            }
            if (*command == "hs_next_episode") {
                blockplanet::ui::request_close();  // the game's card shows; A carries on
                continue;
            }
            if (*command == "hs_exit") {
                blockplanet::ui::request_close();
                hs_exit_since = std::chrono::steady_clock::now();
                hs_exit_paused = false;
                grid_quit_at = *hs_exit_since, grid_quit_red = false;  // the grid covers the way out
                continue;
            }
            if (command->rfind("research_lines_level:", 0) == 0 && game_option_applier) {
                // Research (the test harness): Lines from stage C:L ("research_lines_level:C:L").
                const int c = std::atoi(command->c_str() + 21);
                const auto colon = command->find(':', 21);
                game_option_applier->choose_start(c, colon == std::string::npos ? 1 : std::atoi(command->c_str() + colon + 1));
                launch_mode(LaunchMode::Lines);
                continue;
            }
            if (command->rfind("research_hs_level:", 0) == 0 && game_option_applier) {
                // Research (the test harness): Hide & Seek from level N.
                game_option_applier->choose_start(0, std::atoi(command->c_str() + 18));
                launch_mode(LaunchMode::HideAndSeek);
                continue;
            }
            if (*command == "research_win" && guest_rdram != nullptr &&
                guest::load16(guest_rdram, 0x800E121C) == -1) {
                // Research (the test harness, "poke cmd:research_win"): wins the
                // stage in play. Every top-layer cell is painted one colour
                // (288), which sets off one combo that clears the layer and
                // wins (docs/research/blockplanet-game-state.md, the one-colour board).
                auto* rdram = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t values = guest::load32(rdram, 0x800E44D0u);
                // (Puzzle's border, type 47 cells of value 423, stays: its puzzles are the cells inside.)
                for (int c = 0; c < 1024; ++c) {
                    const std::uint32_t at = values + 2048u + 2u * std::uint32_t(c);
                    const std::uint16_t v = guest::load16(rdram, at);
                    if (v != 0 && v != 423) guest::store16(rdram, at, 288);
                }
                continue;
            }
            if (*command == "dev_complete_rescue" && profile_save) {
                // Developer (main build): every Rescue episode and stage completed
                // for this profile, as if played; asks first.
                blockplanet::ui::set_text("confirm_title", "Complete every episode?");
                blockplanet::ui::set_text("confirm_note", "For " + profile_store->active().name +
                                                              ": every Rescue episode and stage marked completed. This cannot be undone.");
                blockplanet::ui::set_list("confirm", {{"yes", "Yes", "", "command:dev_complete_rescue_yes", true},
                                                      {"no", "No", "", "back", true, 1, true}});
                blockplanet::ui::request_show("confirm");
                continue;
            }
            if (*command == "dev_complete_rescue_yes" && profile_save) {
                auto& progress = profile_data.progress;
                for (int episode = 1; episode <= rescue_chapters; ++episode) {
                    progress.chapters_completed["rescue"].insert(episode);
                    for (int stage = 1; stage <= 10; ++stage) progress.levels_completed["rescue"][episode].insert(stage);
                }
                profile_dirty = true;
                save_profile();
                std::fprintf(stdout, "{\"event\":\"dev_complete_rescue\",\"profile\":\"%s\"}\n", profile_store->active().id.c_str());
                std::fflush(stdout);
                blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
                continue;
            }
            if (*command == "first_training_yes") {
                // The first-launch question's Yes: Training (Basics), then back to the menus.
                single_stage.reset();
                continue_mode.reset();
                chapter_zero_lesson.clear();
                launch_mode(LaunchMode::TrainingBasics);
                continue;
            }
            if (*command == "training_story") {
                // X on Story: its Training lessons.
                const auto done = [&](const char* mode) {
                    const auto found = profile_data.progress.chapters_completed.find(mode);
                    return profile_save && found != profile_data.progress.chapters_completed.end() && found->second.count(0) != 0;
                };
                blockplanet::ui::set_text("chapters_title", "Training");
                blockplanet::ui::set_text("chapters_subtitle", "Choose a lesson");
                blockplanet::ui::set_list("chapters", training_lessons(done("rescue"), done("training_advanced")));
                blockplanet::ui::request_show("chapters");
                continue;
            }
            if (*command == "training" || *command == "training_advanced") {
                single_stage.reset();
                continue_mode.reset();
                chapter_zero_lesson.clear();  // back to the menus afterwards
                launch_mode(*command == "training" ? LaunchMode::TrainingBasics : LaunchMode::TrainingAdvanced);
                continue;
            }
            if (*command == "puzzle" || *command == "puzzles") {
                auto& records = profile_data.progress.episode_records;
                const auto& times = records["puzzle"][0].best_levels;
                const auto& moves = records["puzzle_moves"][0].best_levels;
                blockplanet::ui::set_text("stages_title", stages_title_now = "Puzzles");
                blockplanet::ui::set_text("stages_subtitle", "Choose a puzzle");
                blockplanet::ui::set_list("stages", puzzle_list(profile_data.progress.puzzles_solved, times, moves));
                blockplanet::ui::request_show("stages");
                continue;
            }
            if (command->rfind("puzzle:", 0) == 0) {
                // puzzle:N: one puzzle, chosen from the list.
                const int number = std::atoi(command->c_str() + 7);
                if (number >= 1 && number <= 100 && game_option_applier) {
                    chapter_zero_lesson.clear();
                    continue_mode.reset();
                    game_option_applier->choose_start(0, number);
                    launch_mode(LaunchMode::Puzzle);
                }
                continue;
            }
            if (command->rfind("puzzle_chapter:", 0) == 0) {
                const bool training = std::atoi(command->c_str() + 15) == 0;
                chapter_zero_lesson.clear();  // Training from X: back to the menus afterwards
                continue_mode.reset();
                launch_mode(training ? LaunchMode::TrainingPuzzle : LaunchMode::Puzzle);
                continue;
            }
            if (command->rfind("launch:", 0) == 0) {
                // launch:MODE:CHAPTER:LEVEL (MODE as in the menus, e.g. hide_and_seek):
                // start a chaptered mode at a chapter and level (testing; level select).
                const auto rest = command->substr(7);
                const auto first = rest.find(':');
                const auto second = first == std::string::npos ? first : rest.find(':', first + 1);
                const auto mode = launch_mode_for_command(rest.substr(0, first));
                if (mode && second != std::string::npos && game_option_applier) {
                    game_option_applier->choose_start(std::atoi(rest.c_str() + first + 1),
                                                      std::atoi(rest.c_str() + second + 1));
                    launch_mode(*mode);
                }
                continue;
            }
            if (command->rfind("stages:", 0) == 0) {
                const int chapter = std::atoi(command->c_str() + 7);
                std::set<int> done;
                if (const auto m = profile_data.progress.levels_completed.find(menu_lines ? "lines" : "rescue"); m != profile_data.progress.levels_completed.end())
                    if (const auto c = m->second.find(chapter); c != m->second.end()) done = c->second;
                std::vector<double> best;
                // (Lines keeps one set of stage times for now, whichever menu it was played from.)
                const std::string records_key = menu_lines ? "lines" : variant_records(menu_variant);
                if (const auto m = profile_data.progress.episode_records.find(records_key); m != profile_data.progress.episode_records.end())
                    if (const auto c = m->second.find(chapter); c != m->second.end()) best = c->second.best_levels;
                if (menu_variant == PlayVariant::ScoreAttack) best.clear();  // points, not times: not shown as times
                blockplanet::ui::set_text("stages_title", stages_title_now = "Episode " + std::to_string(chapter));
                blockplanet::ui::set_text("stages_subtitle", "The whole episode, or one stage");
                blockplanet::ui::set_list("stages", story_stages(chapter, done, best, 10, unlock_all_testing));
                launch_stages_chapter = chapter;
                if (!command->ends_with(":refresh")) blockplanet::ui::request_show("stages");
                continue;
            }
            if (*command == "sky_test_stage") {
                // Options > Sky test > Play test stage: Story 1:1 on its own,
                // as Casual (no game over, no trophies), with the brick sky
                // on for this run only; the profile is put back afterwards
                // and nothing is saved meanwhile. Quit (or the stage's end)
                // returns to the Sky test.
                if (game_option_applier && !sky_test_profile) {
                    // Trophy stats from earlier play are written now, so
                    // nothing from before is saved during the run (the
                    // periodic save would otherwise write them then).
                    save_trophies();
                    sky_test_profile = profile_data;
                    sky_test_seen_play = false;
                    sky_test_variants[0] = int(play_variant);
                    sky_test_variants[1] = int(menu_variant);
                    tetrisphere::brick_sky::set_test_run(true);
                    continue_mode.reset();
                    single_stage = std::pair{1, 1};
                    menu_variant = play_variant = PlayVariant::Casual;
                    single_stage_quit_at.reset();
                    single_stage_result.reset();
                    chapter_zero_lesson.clear();
                    game_option_applier->choose_start(1, 1);
                    launch_mode(LaunchMode::Rescue);
                    if (!mode_launcher.active()) {  // refused: nothing started
                        profile_data = *sky_test_profile;
                        sky_test_profile.reset();
                        play_variant = PlayVariant(sky_test_variants[0]);
                        menu_variant = PlayVariant(sky_test_variants[1]);
                        tetrisphere::brick_sky::set_test_run(false);
                        single_stage.reset();
                    }
                }
                continue;
            }
            if (command->rfind("stage:", 0) == 0) {
                // stage:N:L: one Story stage on its own.
                const int chapter = std::atoi(command->c_str() + 6);
                const auto colon = command->find(':', 6);
                const int level = colon == std::string::npos ? 1 : std::atoi(command->c_str() + colon + 1);
                if (chapter >= 1 && chapter <= (menu_lines ? lines_chapters : rescue_chapters) && level >= 1 && game_option_applier) {
                    continue_mode.reset();
                    single_stage = std::pair{chapter, level};
                    play_variant = menu_variant;
                    single_stage_quit_at.reset();
                    single_stage_result.reset();
                    game_option_applier->choose_start(chapter, level);
                    launch_mode(menu_lines ? LaunchMode::Lines : LaunchMode::Rescue);
                }
                continue;
            }
            if (command->rfind("chapter:", 0) == 0) {
                single_stage.reset();
                play_variant = menu_variant;
                const int chapter = std::atoi(command->c_str() + 8);
                chapter_zero_lesson = chapter == 0 ? "basics" : "";
                continue_mode.reset();
                if (chapter == 0) {
                    launch_mode(LaunchMode::TrainingBasics);
                } else if (chapter <= (menu_lines ? lines_chapters : rescue_chapters) && game_option_applier) {
                    // chapter:N, or chapter:N:L to start at level L (Rescue's, or Lines' from its menus).
                    const auto colon = command->find(':', 8);
                    if (colon != std::string::npos)
                        game_option_applier->choose_start(chapter, std::atoi(command->c_str() + colon + 1));
                    else if (menu_lines)
                        game_option_applier->choose_start(chapter, 1);  // Lines' start is set as Puzzle's
                    else
                        game_option_applier->choose_chapter(chapter);
                    launch_mode(menu_lines ? LaunchMode::Lines : LaunchMode::Rescue);
                }
                continue;
            }
            if (const auto mode = launch_mode_for_command(*command)) launch_mode(*mode);
        }
        // A profile change's cartridge save has been swapped: a profile
        // deleted while it was playing can go now.
        if (const auto switched = tetrisphere::direct_start::take_save_switch_result()) {
            if (!*switched) show_error("Could not load the profile's save.");
            if (!profile_to_remove.empty() && profile_store) {
                std::string error;
                if (const auto* removed = profile_by_id(*profile_store, profile_to_remove)) {
                    const std::string name = removed->name;
                    if (!profile_store->remove(name, error))
                        std::fprintf(stderr, "Could not delete the profile: %s\n", error.c_str());
                }
                profile_to_remove.clear();
            }
        }
        if (direct_wait && guest_rdram != nullptr) {
            if (const auto result = direct_start::take_result()) {
                const auto mode = direct_wait->mode;
                direct_wait.reset();
                if (*result == direct_start::Result::Started) {
                    std::fprintf(stdout, "{\"event\":\"mode_launch\",\"status\":\"playing\",\"route\":\"direct\"}\n");
                    std::fflush(stdout);
                } else {
                    // Not possible from here: start through the original menus.
                    const bool accepted = mode_launcher.request(mode, guest_rdram, std::chrono::steady_clock::now());
                    if (!accepted) loading_active = false;
                    std::fprintf(stdout, "{\"event\":\"mode_launch\",\"mode\":\"%s\",\"status\":\"%s\",\"route\":\"menus\"}\n",
                                 launch_mode_name(mode), accepted ? "started" : "refused");
                    std::fflush(stdout);
                }
            } else if (std::chrono::steady_clock::now() - direct_wait->since > std::chrono::seconds(2)) {
                // The game has not reached its menu loop (the title, the
                // intro): give up; the result arrives on the next pass.
                direct_start::cancel();
            }
        }
        if (mode_launcher.active() && guest_rdram != nullptr) {
            std::uint16_t press = 0;
            const auto result = mode_launcher.update(const_cast<std::uint8_t*>(guest_rdram),
                                                     std::chrono::steady_clock::now(), press);
            launcher_buttons.store(result == ModeLauncher::Result::Working ? (0x10000u | press) : 0u);
            if (result == ModeLauncher::Result::Started || result == ModeLauncher::Result::TimedOut) {
                std::fprintf(stdout, "{\"event\":\"mode_launch\",\"status\":\"%s\"}\n",
                             result == ModeLauncher::Result::Started ? "playing" : "timed_out");
                std::fflush(stdout);
            }
        }
        if (!launching() && guest_rdram != nullptr) {
            // The start card: once play has started (a moment in, as the
            // sphere arrives), pause the game; the pause shows the card.
            {
                const auto now_c = std::chrono::steady_clock::now();
                if (start_card_pending) {
                    const bool rescue_play = guest::load16(guest_rdram, 0x800E121C) == -1 && guest::load16(guest_rdram, 0x800E44A8) == start_card_mode &&
                                             std::int16_t(guest::load16(guest_rdram, 0x800E1228)) < 0 &&
                                             std::int16_t(guest::load16(guest_rdram, 0x80110220u + 0x2D7E)) >= 0;
                    static std::optional<std::chrono::steady_clock::time_point> play_seen;
                    if (!rescue_play) play_seen.reset();
                    else if (!play_seen) play_seen = now_c;
                    if (tetrisphere::minefield_sphere::armed() || sky_test_profile || now_c - *start_card_pending > std::chrono::seconds(40) ||
                        profile_data.progress.start_cards_seen.count(start_card_mode == 4 ? "puzzle" : start_card_key(play_variant)) != 0) {
                        start_card_pending.reset();
                    } else if (play_seen && now_c - *play_seen > std::chrono::milliseconds(600) && !blockplanet::ui::menus_open()) {
                        start_card_pending.reset();
                        play_seen.reset();
                        start_card_requested = now_c;
                        pause_bridge.request_pause(now_c);
                    }
                }
                if (start_card_requested && now_c - *start_card_requested > std::chrono::seconds(4)) start_card_requested.reset();
            }
            // A puzzle solved: pause as the next one starts and show its results.
            static std::optional<std::chrono::steady_clock::time_point> puzzle_results_requested;
            {
                static std::optional<std::chrono::steady_clock::time_point> solved_seen;
                const auto now_p = std::chrono::steady_clock::now();
                if (puzzle_result && !puzzle_results_requested) {
                    if (!solved_seen) solved_seen = now_p;
                    const bool puzzle_play = guest::load16(guest_rdram, 0x800E44A8) == 4 && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                                             std::int16_t(guest::load16(guest_rdram, 0x800E1228)) < 0;
                    if (!puzzle_play && now_p - *solved_seen > std::chrono::seconds(10)) puzzle_result.reset(), solved_seen.reset();
                    else if (puzzle_play && now_p - *solved_seen > std::chrono::milliseconds(700) && !blockplanet::ui::menus_open()) {
                        puzzle_results_requested = now_p;
                        solved_seen.reset();
                        pause_bridge.request_pause(now_p);
                    }
                }
                if (puzzle_results_requested && now_p - *puzzle_results_requested > std::chrono::seconds(4))
                    puzzle_results_requested.reset(), puzzle_result.reset();
            }
            // The last heart lost (the owner's game over): its loss is held while
            // the sphere freezes, the camera goes back to its resting distance
            // (the drop timer and other effects close it in; the owner: "pull the
            // camera back to its starting point"), and the shell closes; then released.
            {
                static std::optional<std::chrono::steady_clock::time_point> held_since, shell_from;
                const auto now_h = std::chrono::steady_clock::now();
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const bool in_play_c = guest::load16(guest_rdram, 0x800E121C) == -1;
                if (tetrisphere::sphere::game_over_held()) {
                    if (!held_since) held_since = now_h, shell_from.reset();
                    guest::store32(rd, 0x80110220u + 0x2408, 0);  // no drop meanwhile
                    guest::store16(rd, 0x80110220u + 0x2B10, 0);
                    // Eased back and held there for the whole hold (the game's own
                    // pushes would close it in again).
                    // (The camera is eased back on the game's own thread: game_over_shell.cpp.)
                    if (!shell_from && tetrisphere::sphere::game_over_camera_settled()) shell_from = now_h;
                    // The shell once the camera is back (or after a second at most).
                    if (!shell_from && now_h - *held_since > std::chrono::seconds(1)) shell_from = now_h;
                    hold_shell_on.store(shell_from.has_value());
                    if ((shell_from && now_h - *shell_from > std::chrono::milliseconds(2400)) || !in_play_c)
                        tetrisphere::sphere::release_game_over(), held_since.reset(), shell_from.reset();
                } else {
                    held_since.reset(), shell_from.reset();
                    hold_shell_on.store(false);
                }
                game_over_frozen.store(tetrisphere::sphere::game_over_held());
            }
            std::uint16_t hs_buttons = hide_and_seek_skips();
            if (const auto lines_buttons = lines_card_once()) hs_buttons = lines_buttons;
            track_puzzle_limits();
            const auto pause = pause_bridge.update(const_cast<std::uint8_t*>(guest_rdram),
                                                   blockplanet::ui::menus_open(), std::chrono::steady_clock::now(),
                                                   start_pressed_recently());
            // Where the player is, for the pause and end screens' subtitles.
            const auto where_now = [&] {
                const auto state = read_game_state(guest_rdram);
                std::string where;
                switch (state.mode) {
                case 1: where = std::string(variant_title(play_variant)) + " \u00B7 " + std::to_string(state.chapter + 1) + ":" + std::to_string(state.level + 1); break;
                case 2: where = "Score Attack"; break;
                case 3: {
                    // The lesson by its name in Training's menu (a lesson is a level of its training).
                    static const char* basics[] = {"The Basics", "Lives", "Combos", "Magic"};
                    static const char* advanced[] = {"Crystal Bricks", "X Count", "Fuse Combos"};
                    const int lesson = guest::load16(guest_rdram, 0x80110220u + 0x23BA);
                    const char* name = state.training_type == 3 ? "Puzzle Controls"
                                       : state.training_type == 2 ? (lesson >= 0 && lesson < 3 ? advanced[lesson] : "Advanced")
                                                                  : (lesson >= 0 && lesson < 4 ? basics[lesson] : "Basics");
                    where = std::string("Training \u00B7 ") + name;
                    break;
                }
                case 4: where = "Puzzle " + std::to_string(state.level + 1); break;
                case 5: where = "Hide & Seek \u00B7 Level " + std::to_string(state.level + 1); break;  // its chapter is not the Rescue one
                case 6: where = "Versus"; break;
                case 9: where = "Lines \u00B7 " + std::to_string(state.chapter + 1) + ":" + std::to_string(state.level + 1); break;
                default: break;
                }
                return where;
            };
            // "Exit to <place>": where the way out of play goes (the launch
            // path's last screen; docs/STYLE.md).
            // From the stage list, its heading, which is what the player sees
            // there ("Exit to Chapter 2", "Exit to Puzzles"), rather than the
            // screen's name, Stages.
            const auto exit_label = [&]() -> std::string {
                // Training's lessons go back to Training's menu (its screen's file title is another mode's).
                if (guest::load16(guest_rdram, 0x800E44A8) == 3 && !launch_path.empty() && launch_path.back().first == "chapters")
                    return "Exit to Training";
                if (!launch_path.empty() && !launch_place.empty()) return "Exit to " + launch_place;
                return blockplanet::ui::exit_to_label(launch_path.empty() ? std::string() : launch_path.back().first);
            };
            // A lost Minefield (the owner, 2026-11-15): after its mines are shown,
            // the standard game over's black shell closes over the sphere, then
            // the Game Over panel (BlockPlanet's: the game's own game over goes
            // on to its high-score table instead).
            {
                static std::optional<std::chrono::steady_clock::time_point> shell_since;
                const auto now_m = std::chrono::steady_clock::now();
                if (tetrisphere::minefield_sphere::take_lost_game_over()) shell_since = now_m;
                const bool in_play_m = guest::load16(guest_rdram, 0x800E121C) == -1;
                if (shell_since && !in_play_m) shell_since.reset();
                // Only while that field is lost and in play (a new field starts bare).
                if (shell_since && !tetrisphere::minefield_sphere::lost()) shell_since.reset();
                tetrisphere::sphere::set_game_over_shell_forced(shell_since.has_value() || hold_shell_on.load());
                minefield_frozen.store(shell_since.has_value());
                if (false) {
                } else if (shell_since && now_m - *shell_since > std::chrono::milliseconds(3800) && !blockplanet::ui::menus_open()) {  // once the shell has closed
                    shell_since = now_m + std::chrono::hours(24);  // shown once
                    blockplanet::ui::set_text("end_title", "Game Over");
                    blockplanet::ui::set_text("end_where", "Minefield \u00B7 " + tetrisphere::minefield_sphere::arena_name());
                    blockplanet::ui::set_text("end_line", game_over_line(true));  // (the mines' count reads "Boom" by then)
                    blockplanet::ui::ListItem again{"try_again", "New Field", "", "command:minefield_lost_again", true, 1, true};
                    again.icon = "\uE14C";
                    blockplanet::ui::ListItem leave{"exit", exit_label(), "", "command:minefield_lost_exit", true};
                    leave.icon = "\uE112";
                    blockplanet::ui::set_list("end", {again, leave});
                    blockplanet::ui::request_open("gameover");
                }
            }
            // Hide & Seek's episode end (the shared pattern): after level 30
            // the game shows the next episode's card (the pause row at 100)
            // and waits for A. BlockPlanet's results panel comes first: Next
            // Episode shows the card; Exit closes it and leaves through the
            // next level's pause.
            {
                static bool hs_end_shown = false;
                const bool hs_card = guest::load16(guest_rdram, 0x800E44A8) == 5 && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                                     guest::load16(guest_rdram, 0x800E1228) == 100 && guest::load16(guest_rdram, 0x800E4498) == 29;
                if (!hs_card) hs_end_shown = false;
                else if (!hs_end_shown && !hs_exit_since && guest::load16(guest_rdram, 0x800DFF08u) == 1) {
                    hs_end_shown = true;
                    const int chapter = guest::load16(guest_rdram, 0x800E44A0) + 1;
                    std::string line;
                    if (hs_run_from_first) {
                        const double total = hs_run_total + hs_level_time;
                        const auto& rec = profile_data.progress.episode_records["hide_and_seek"][chapter];
                        line = "Episode time " + hud_time(total);
                        // (The best was just updated if this run beat it: compare with the levels' sum only when it is another run's.)
                        if (rec.best_total <= 0.0 || total <= rec.best_total + 0.01) line += "  \u00B7  New best";
                        else line += "  \u00B7  Best " + hud_time(rec.best_total) + " (" + hud_delta(total - rec.best_total) + ")";
                    }
                    blockplanet::ui::set_text("end_title", "Episode Cleared");
                    blockplanet::ui::set_text("end_where", "Hide & Seek \u00B7 Episode " + std::to_string(chapter));
                    blockplanet::ui::set_text("end_line", line);
                    blockplanet::ui::ListItem next{"next", "Next Episode", "Episode " + std::to_string(chapter + 1), "command:hs_next_episode", true, 1, true};
                    next.icon = "\uE140";
                    blockplanet::ui::ListItem leave{"exit", exit_label(), "", "command:hs_exit", true};
                    leave.icon = "\uE112";
                    blockplanet::ui::set_list("end", {next, leave});
                    blockplanet::ui::request_open("episode_end");
                }
                if (hs_exit_since) {
                    const auto now_x = std::chrono::steady_clock::now();
                    if (now_x - *hs_exit_since > std::chrono::seconds(20)) {
                        hs_exit_since.reset();  // give up
                    } else if (hs_card) {
                        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_x - *hs_exit_since).count();
                        hs_buttons = ms % 350 < 100 ? 0x8000 : 0;  // close the card
                    } else if (!hs_exit_paused && guest::load16(guest_rdram, 0x800E1228) < 0 && guest::load16(guest_rdram, 0x800E121C) == -1) {
                        hs_exit_paused = true;
                        pause_bridge.request_pause(now_x);
                    }
                }
            }
            if (pause.open_menu && puzzle_results_requested && puzzle_result) {
                const auto r = *puzzle_result;
                puzzle_results_requested.reset();
                puzzle_result.reset();
                std::string line = hud_time(r.time);
                if (r.best_before <= 0.0 || r.time < r.best_before) line += "  \u00B7  New best";
                else line += "  \u00B7  Best " + hud_time(r.best_before) + " (" + hud_delta(r.time - r.best_before) + ")";
                if (r.moves > 0) line += "  \u00B7  " + std::to_string(r.moves) + (r.moves == 1 ? " move" : " moves");
                blockplanet::ui::set_text("results_title", "Puzzle Solved");
                blockplanet::ui::set_text("results_where", "Puzzle " + std::to_string(r.number));
                blockplanet::ui::set_text("results_line", line);
                std::vector<blockplanet::ui::ListItem> items;
                blockplanet::ui::ListItem next{"next", "Next Puzzle", "Puzzle " + std::to_string(r.number + 1), "command:pause_resume", true, 1, true};
                next.icon = "\uE140";
                blockplanet::ui::ListItem again{"again", "Try Again", "", "command:puzzle_again:" + std::to_string(r.number), true};
                again.icon = "\uE14C";
                blockplanet::ui::ListItem leave{"quit", exit_label(), "", "command:pause_quit", true};
                leave.icon = "\uE112";
                blockplanet::ui::set_list("results", {next, again, leave});
                blockplanet::ui::request_open("puzzle_results");
            } else if (pause.open_menu && hs_exit_since) {
                hs_exit_since.reset();
                hs_exit_paused = false;
                end_track_override(), return_to_menu_chapter = 0, grid_quit_at = std::chrono::steady_clock::now();
                pause_bridge.choose(PauseBridge::Choice::Quit, std::chrono::steady_clock::now());
            } else if (pause.open_menu && start_card_requested) {
                start_card_requested.reset();
                start_card_showing = true;
                // Shown once per game type for each profile; after that the
                // goal is on the pause screen.
                if (profile_data.progress.start_cards_seen.insert(start_card_mode == 4 ? "puzzle" : start_card_key(play_variant)).second) {
                    profile_dirty = true;
                    save_profile();
                }
                const auto state = read_game_state(guest_rdram);
                if (start_card_mode == 4) {
                    blockplanet::ui::set_text("card_title", "Puzzle");
                    blockplanet::ui::set_text("card_where", "Puzzle " + std::to_string(state.level + 1));
                    blockplanet::ui::set_text("card_goal", puzzle_goal());
                } else {
                const int chapter = state.chapter + 1, level = state.level + 1;
                const int square = guest::load16(guest_rdram, 0x80110220u + 0x23E4);
                blockplanet::ui::set_text("card_title", variant_title(play_variant));
                blockplanet::ui::set_text("card_where", single_stage ? "Stage " + std::to_string(chapter) + ":" + std::to_string(level)
                                                        : level > 1 ? "Episode " + std::to_string(chapter) + " \u00B7 from stage " +
                                                                          std::to_string(chapter) + ":" + std::to_string(level)
                                                                    : "Episode " + std::to_string(chapter));
                std::string goal = rescue_goal(square);
                if (!single_stage && level == 1) goal += " Ten stages in all.";
                blockplanet::ui::set_text("card_goal", goal);
                }
                blockplanet::ui::ListItem go{"start", "Start", "", "command:pause_resume", true, 1, true};
                go.icon = "\uE140";
                blockplanet::ui::set_list("start_card", {go});
                blockplanet::ui::request_open("start_card");
            } else if (pause.open_menu) {
                start_card_showing = false;
                blockplanet::ui::set_text("pause_where", where_now());
                // The goal, as the start card gave it (Rescue; the other modes to follow).
                const int mode_now = guest::load16(guest_rdram, 0x800E44A8);
                blockplanet::ui::set_text("pause_goal", mode_now == 1 && tetrisphere::minefield_sphere::arena_name().empty()
                                                            ? rescue_goal(guest::load16(guest_rdram, 0x80110220u + 0x23E4))
                                                        : mode_now == 5 ? hide_and_seek_goal_text(guest::load16(guest_rdram, 0x800E44A4))
                                                        : mode_now == 4 ? puzzle_goal()
                                                        : mode_now == 9 ? std::string("Line up three bricks across or down to remove them. Clear enough of the core for your friend to escape.")
                                                                        : std::string());
                auto pause_items = pause.items;
                for (auto& item : pause_items) {
                    if (item.id == "quit") item.label = exit_label();
                    // Rescue: its runs are episodes; a stage played on its own restarts itself.
                    if (item.id == "restart" && guest::load16(guest_rdram, 0x800E44A8) == 1)
                        item.label = single_stage ? "Restart Stage" : "Restart Episode";
                    // Hide & Seek: its run goes back to level 1.
                    if (item.id == "restart" && guest::load16(guest_rdram, 0x800E44A8) == 5) item.label = "Restart from Level 1";
                    if (item.id == "restart" && guest::load16(guest_rdram, 0x800E44A8) == 4) item.label = "Restart Puzzle";
                    // Lines starts again from its first stage.
                    if (item.id == "restart" && guest::load16(guest_rdram, 0x800E44A8) == 9)
                        item.label = single_stage ? "Restart Stage" : "Restart Episode";
                }
                // Minefield on the sphere (played in Story's mode): its own
                // name, New Field in place of Restart Chapter, and no Story
                // how-to (the owner's report).
                if (const auto arena = tetrisphere::minefield_sphere::arena_name(); !arena.empty()) {
                    blockplanet::ui::set_text("pause_where", "Minefield \u00B7 " + arena);
                    pause_items.erase(std::remove_if(pause_items.begin(), pause_items.end(),
                                                     [](const auto& item) { return item.id == "howto"; }),
                                      pause_items.end());
                    for (auto& item : pause_items)
                        if (item.id == "restart") item.label = "New Field", item.action = "command:minefield_again";
                }
                blockplanet::ui::set_list("pause", pause_items);
                blockplanet::ui::request_open("pause");
            }
            // The game's end menus (game over, Versus' end) under BlockPlanet's
            // panel (the owner's request, 2026-11-10): its items as the game
            // draws them (its menu text, kept rather than drawn).
            std::vector<EndMenuBridge::Label> end_labels;
            for (const auto& t : blockplanet::recent_game_text())
                if (t.font == 0) end_labels.push_back({t.text, t.y});
            if (episode_end_expected != 0) end_menu.expect_episode_end(episode_end_expected > 0), episode_end_expected = 0;
            const auto end = end_menu.update(const_cast<std::uint8_t*>(guest_rdram), end_labels, blockplanet::ui::menus_open(),
                                             std::chrono::steady_clock::now(), pause_bridge.hide_original());
            end_menu_active = end.active;
            end_menu_cleared = end.cleared;
            if (end.open_menu) {
                blockplanet::ui::set_text("end_title", end.title);
                const bool lines_end = guest::load16(guest_rdram, 0x800E44A8) == 9;
                if (lines_end) episode_end_line.clear();  // (no episode time kept for Lines yet)
                blockplanet::ui::set_text("end_where", end.cleared ? std::string(lines_end ? "Lines" : variant_title(play_variant)) + " \u00B7 Episode " +
                                                                         std::to_string(read_game_state(guest_rdram).chapter + 1)
                                                                   : where_now());
                blockplanet::ui::set_text("end_line", end.cleared ? episode_end_line : game_over_line());
                auto end_items = end.items;
                for (auto& item : end_items) {
                    if (item.id == "exit") item.label = exit_label();
                    if (item.id == "continue" && end.cleared) {
                        const int next = read_game_state(guest_rdram).chapter + 2;  // (0-based there)
                        item.label = "Next Episode";
                        if (next <= rescue_chapters) item.note = "Episode " + std::to_string(next);
                    }
                }
                blockplanet::ui::set_list("end", end_items);
                blockplanet::ui::request_open(end.cleared ? "episode_end" : "gameover");
            }
            // Only in play: back at the game's menu (after Quit), the menus shown are the launch path's.
            if ((pause.close_menu || end.close_menu) && guest::load16(guest_rdram, 0x800E121C) == -1) blockplanet::ui::request_close();
            launcher_buttons.store(pause.inject ? (0x10000u | pause.buttons)
                                   : end.inject ? (0x10000u | end.buttons)
                                   : hs_buttons != 0 ? (0x10000u | hs_buttons)
                                                : training_inject.load());
            blockplanet::set_pause_menu_shown(pause_bridge.hide_original() || end_menu.hide_original());
            set_menu_play_state(read_game_state(guest_rdram).scene == -1);
            update_hud();
            {
                // What the game is doing, for a crash report.
                const auto st = read_game_state(guest_rdram);
                char context[160];
                std::snprintf(context, sizeof context, "scene %d, mode %d, chapter %d, level %d, menus %s, screen %s", st.scene, st.mode,
                              st.chapter + 1, st.level + 1, blockplanet::ui::menus_open() ? "open" : "closed",
                              blockplanet::ui::current_screen().c_str());
                set_crash_context(context);
                // Research: BLOCKPLANET_CRASH_TEST=seconds crashes on purpose after that long, to test the report.
                static const char* crash_test = std::getenv("BLOCKPLANET_CRASH_TEST");
                static const auto started = std::chrono::steady_clock::now();
                if (crash_test != nullptr && std::chrono::steady_clock::now() - started > std::chrono::seconds(std::atoi(crash_test))) {
                    volatile int* nowhere = nullptr;
                    *nowhere = 1;
                }
            }
            publish_game_text();
            update_training_presses(blockplanet::recent_card_box().has_value());
            // A lesson chosen on its own: once the game moves on to the next
            // lesson (its level counts on), leave through the pause.
            if (single_lesson && guest::load16(guest_rdram, 0x800E44A8) == 3 && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                guest::load16(guest_rdram, 0x80110220u + 0x23BA) > *single_lesson) {
                single_lesson.reset();
                end_track_override(), return_to_menu_chapter = 0, grid_quit_at = std::chrono::steady_clock::now();
                single_stage_quit_at = std::chrono::steady_clock::now();
            }
            // The training's last lesson: the game ends Training itself and goes
            // back to its menu; BlockPlanet's menus then open at the launch path
            // (return_to_menu_chapter 0), as after Exit.
            if (single_lesson && guest::load16(guest_rdram, 0x800E121C) >= 0) {
                single_lesson.reset();
                end_track_override(), return_to_menu_chapter = 0;
            }
            // A single stage finished: pause, then quit to the menu.
            if (single_stage_quit_at && std::chrono::steady_clock::now() >= *single_stage_quit_at) {
                single_stage_quit_at.reset();
                single_stage_quitting = true;
                pause_bridge.request_pause(std::chrono::steady_clock::now());
            }
            if (single_stage_quitting && pause_bridge.hide_original()) {
                single_stage_quitting = false;
                // (Minefield goes back to its own menu, the launch path's.)
                if (single_stage && !tetrisphere::minefield_sphere::armed()) {
                    return_to_menu_chapter = single_stage->first;
                    if (single_stage_result)
                        pending_results = StageResults{single_stage->first, single_stage->second, play_variant == PlayVariant::ScoreAttack,
                                                       single_stage_result->time, single_stage_result->best_before,
                                                       std::string(play_lines ? "Lines" : variant_title(play_variant)) + " \u00B7 " +
                                                           std::to_string(single_stage->first) + ":" + std::to_string(single_stage->second)};
                }
                single_stage.reset();
                single_stage_result.reset();
                pause_bridge.choose(PauseBridge::Choice::Quit, std::chrono::steady_clock::now());
            }
        }
        pc_settings_release_guard(held);
        poll_game_events();
        publish_audio_settings();
        publish_hud_settings();
        publish_sky_settings();
        if (const auto frame = picture_frame()) tetrisphere::brick_sky::set_picture_aspect(frame->aspect);
        // The Sky test's test stage ends once play has been left (Quit, or
        // the stage's end) and the game is back at its menus: the sky goes
        // off and the profile is put back as it was.
        // During the run no trophy stats are counted (Casual turns awards
        // off at launch; held off here throughout, whatever else changes).
        if (sky_test_profile) trophy_engine().set_awards_enabled(false);
        if (sky_test_profile && guest_rdram != nullptr && !mode_launcher.active()) {
            const bool playing = guest::load16(guest_rdram, 0x800E121C) == -1;
            sky_test_seen_play = sky_test_seen_play || playing;
            if (sky_test_seen_play && !playing) {
                tetrisphere::brick_sky::set_test_run(false);
                profile_data = *sky_test_profile;
                sky_test_profile.reset();
                profile_dirty = false;
                // Back to the variant the menus had (the run was Casual), and
                // Casual's no-game-over and trophies as they are for that.
                play_variant = PlayVariant(sky_test_variants[0]);
                menu_variant = PlayVariant(sky_test_variants[1]);
                single_stage.reset();
                if (game_option_applier) {
                    const bool casual = play_variant == PlayVariant::Casual;
                    game_option_applier->set_menu_casual(casual);
                    trophy_engine().set_awards_enabled(!casual && !game_option_applier->affects_awards());
                }
                std::fprintf(stdout, "{\"event\":\"sky_test_stage_end\"}\n");
                std::fflush(stdout);
            }
        }
        // Back at the game's menu after leaving play from BlockPlanet: open
        // BlockPlanet's menus (the stage list after a single stage).
        if (return_to_menu_chapter && !continue_mode && !launching() && guest_rdram != nullptr &&
            read_game_state(guest_rdram).scene >= 0) {
            const int chapter = *return_to_menu_chapter;
            return_to_menu_chapter.reset();
            if (!launch_path.empty()) {
                // Back to the menus the game was started from, refreshing the
                // stage list's bests first.
                restore_launch_path();
                show_stage_results();
            } else {
            blockplanet::ui::request_open("main");
            if (chapter > 0) {
                blockplanet::ui::request_show("solo");
                blockplanet::ui::request_show("story");
                const char* list = menu_variant == PlayVariant::TimeAttack ? "time_attack"
                                   : menu_variant == PlayVariant::ScoreAttack ? "score_attack"
                                   : menu_variant == PlayVariant::Casual ? "casual" : "story";
                blockplanet::ui::send_command(list);
                blockplanet::ui::send_command("stages:" + std::to_string(chapter));
            }
            }
        }
        // A pending launch (after a Chapter 0, or Restart level) starts once
        // the game is back at its menu.
        static auto last_continue_try = std::chrono::steady_clock::time_point{};
        if (continue_mode && !launching() && guest_rdram != nullptr && game_option_applier &&
            read_game_state(guest_rdram).scene >= 0 &&
            std::chrono::steady_clock::now() - last_continue_try > std::chrono::seconds(1)) {
            last_continue_try = std::chrono::steady_clock::now();
            if (continue_mode->start)
                game_option_applier->choose_start(continue_mode->start->first, continue_mode->start->second);
            launch_mode(continue_mode->mode, continue_mode->keep_music);
            if (launching()) continue_mode.reset();
        }
        // BlockPlanet's menus in place of the original's (the owner's choice,
        // 2026-11-08): whenever the game is at its own menus (after the title,
        // after quitting), BlockPlanet's main menu opens, and the original
        // menu is not drawn behind it. BLOCKPLANET_ORIGINAL_MENUS=1 keeps the
        // original menus (F1 still opens BlockPlanet's).
        {
            static const bool original_menus = std::getenv("BLOCKPLANET_ORIGINAL_MENUS") != nullptr;
            // Only once the title has been shown: before it (the logo
            // animation) the menu row already reads as a menu.
            const bool title_now = blockplanet_title_recent(nullptr);
            if (title_restart_pending) {
                if (!title_now) title_restart_pending = false;  // the old title has gone
            } else {
                title_seen = title_seen || title_now;
            }
            if (credits_pending && title_now && guest_rdram != nullptr) {
                credits_pending = false;
                guest::store16(const_cast<std::uint8_t*>(guest_rdram), 0x800E122E, 0);
            }
            const bool at_game_menu = guest_rdram != nullptr && title_seen && !title_now &&
                                      guest::load16(guest_rdram, 0x800E121C) >= 0 &&
                                      guest::load16(guest_rdram, 0x800E122E) < 0;  // not in the credits
            // Not on the way to the box art's demo, nor during it.
            // The box art (the game's title) holds for a press: the menus
            // open only once the game has left its intro and title.
            const bool box_art_demo = tetrisphere::box_art::phase().load() != tetrisphere::box_art::Phase::None ||
                                      (tetrisphere::box_art::enabled() && guest::load16(guest_rdram, 0x800E1646) == 0);
            if (!original_menus && at_game_menu && !box_art_demo && !blockplanet::ui::menus_open() && !launching() &&
                !continue_mode && !return_to_menu_chapter && guest::load16(guest_rdram, 0x800E1220) < 0) {
                if (profile_picked && !launch_path.empty()) {
                    // Back from a game (game over, Exit, an episode's end): the menus it started from.
                    restore_launch_path();
                } else if (profile_picked || !profile_store) {
                    open_main_menu();
                } else {
                    blockplanet::ui::set_list("profiles", profile_list(*profile_store, guest_session, true));
                    blockplanet::ui::request_open("profiles");
                }
            }
            blockplanet::set_game_menu_hidden(!original_menus && at_game_menu &&
                                              (blockplanet::ui::menus_open() || launching()));
        }
        // The game's attract demo starts after its menu sits idle; behind
        // BlockPlanet's menus it counts as play and launches were refused
        // (the owner's report). While BlockPlanet's menus are open over the
        // game's menu, its idle counters (frames on the page) are held at 10
        // seconds, past every page's own entrance timing.
        if (guest_rdram != nullptr && blockplanet::ui::menus_open() && !launching() &&
            guest::load16(guest_rdram, 0x800E121C) >= 0) {
            auto* rd = const_cast<std::uint8_t*>(guest_rdram);
            for (const std::uint32_t counter : {0x800E1208u, 0x800E120Cu})
                if (std::int32_t(guest::load32(rd, counter)) > 600) guest::store32(rd, counter, 600);
        }
        // The loading cover: fades in over 0.15 s when a mode starts, stays
        // while the game's menus are pressed through and the stage loads,
        // and fades out over 0.5 s once play has run 0.4 s with the game's
        // own fade clear (3 s of play at the latest; a pending Continue or
        // restart keeps it up too). At most 20 seconds. When the menu grid is
        // up as the mode starts, the grid is the loading screen instead (the
        // cover's text over it, no dark cover) and flies out once the load is
        // done (ui_system.h, set_loading).
        {
            static float opacity = 0.0f;
            static std::optional<std::chrono::steady_clock::time_point> playing_since;
            static auto last = std::chrono::steady_clock::now();
            const auto now_l = std::chrono::steady_clock::now();
            const float dt = std::min(0.1f, std::chrono::duration<float>(now_l - last).count());
            last = now_l;
            const bool in_play_now = guest_rdram != nullptr && guest::load16(guest_rdram, 0x800E121C) == -1;
            // The game's own fade (0x800E1202: 0 clear, 255 dark) has brought
            // the stage in: the load is only done once it is clear (the menu
            // grid, as the loading screen, then reveals the stage itself
            // rather than a black screen fading in).
            const bool faded_in = guest_rdram != nullptr && guest::load16(guest_rdram, 0x800E1202) == 0;
            static std::optional<std::chrono::steady_clock::time_point> clear_since;
            // How long after the fade reads clear the load is done: Story's
            // stage still fades in for about 1 s after 0x800E1202 reads 0
            // (recorded 2026-11-09), so the grid holds over it
            // (BLOCKPLANET_GRID_LOAD_MS, default 1200).
            static const int settle_ms = [] {
                const char* text = std::getenv("BLOCKPLANET_GRID_LOAD_MS");
                return text != nullptr ? std::max(0, std::atoi(text)) : 1200;
            }();
            if (loading_active) {
                if (launching() || continue_mode || !in_play_now) playing_since.reset();
                else if (!playing_since) playing_since = now_l;
                if (!playing_since || !faded_in) clear_since.reset();
                else if (!clear_since) clear_since = now_l;
                // Done settle_ms after the fade is clear in play; 4 s into
                // play in any case (a mode whose fade reads otherwise); 20 s
                // at most.
                if ((clear_since && now_l - *clear_since > std::chrono::milliseconds(settle_ms)) ||
                    (playing_since && now_l - *playing_since > std::chrono::seconds(4)) ||
                    now_l - loading_since > std::chrono::seconds(20))
                    loading_active = false;
            }
            opacity = loading_active ? std::min(1.0f, opacity + dt / 0.15f) : std::max(0.0f, opacity - dt / 0.5f);
            // The old menus' sound effects (their page transitions) are not
            // heard under the cover (the owner's report): effects are muted
            // while it is up, and the player's levels come back with play.
            static bool muted = false;
            if (loading_active && !muted) {
                muted = true;
                blockplanet::set_audio_levels(current_audio().music, 0);
            } else if (!loading_active && muted) {
                muted = false;
                blockplanet::set_audio_levels(current_audio().music, current_audio().effects);
            }
            blockplanet::ui::set_loading(loading_title, opacity, loading_active);
        }
        // The menu grid (docs/UI.md, Menu grid) over the game's own pause
        // animations, in the pause's colours: from the Start press that
        // pauses (before the game's own fade and tiles show), while paused,
        // and after Resume until the pause row 0x800E1228 clears and a moment
        // more (BLOCKPLANET_GRID_RESUME_MS, default 450); and after Quit until
        // BlockPlanet's menus open again. Measured on the test machine
        // (2026-11-09): after Resume the game's screen fade (0x800E1202, 255
        // while paused) is clear within about 0.4 s, then the game flies its
        // own pause tiles out over the play field for about 1.3 s before the
        // row clears, so Resume takes about 2 s (the owner finds it slow).
        // Research: BLOCKPLANET_GRID_RESUME=fade leaves once the fade is clear
        // and skips the pause background helper after Resume
        // (set_pause_exit_hidden); that helper is not where the game's pause
        // tiles are drawn (recorded 2026-11-10: they still fly out after our
        // grid), so it waits for the row until they are found.
        {
            static const bool after_row = [] {
                const char* text = std::getenv("BLOCKPLANET_GRID_RESUME");
                return text == nullptr || std::string(text) != "fade";
            }();
            // After Resume, end the game's own pause exit as soon as its
            // picture has faded in (PauseBridge::finish_resume), so the row
            // clears about 1.3 s sooner; the grid then leaves 150 ms after.
            // BLOCKPLANET_RESUME_SKIP=0 lets the exit run its full length.
            static const bool resume_skip = [] {
                const char* text = std::getenv("BLOCKPLANET_RESUME_SKIP");
                return text == nullptr || std::string_view(text) != "0";
            }();
            static const int resume_ms = [] {
                const char* text = std::getenv("BLOCKPLANET_GRID_RESUME_MS");
                return text != nullptr ? std::max(0, std::atoi(text)) : resume_skip ? 150 : (after_row ? 450 : 200);
            }();
            if (resume_skip && guest_rdram != nullptr) pause_bridge.finish_resume(const_cast<std::uint8_t*>(guest_rdram));
            static bool was_paused = false;
            static std::optional<std::chrono::steady_clock::time_point> unpaused_at, start_at;
            static std::int64_t start_seen = last_start_ms.load();
            const auto now_g = std::chrono::steady_clock::now();
            bool paused = false, playing = false;
            if (guest_rdram != nullptr) {
                playing = guest::load16(guest_rdram, 0x800E121C) == -1;
                const auto row = std::int16_t(guest::load16(guest_rdram, 0x800E1228));
                const bool game_over = std::int16_t(guest::load16(guest_rdram, 0x80112F9E)) == -1;
                // (The episode-end menu uses the row too, and the next
                // episode's card sets it to 100: neither is a pause, whose
                // rows are 0 to 3.)
                paused = playing && row >= 0 && row <= 3 && !game_over && !end_menu.episode_end() && !end_menu.puzzle_failed();
                // Resumed (BlockPlanet's pause screen was open in this pause
                // and is closed now) and the game's fade back in done: counted
                // as no longer paused, unless waiting for the row.
                static bool menu_seen = false;
                if (!paused) menu_seen = false;
                else if (blockplanet::ui::menus_open()) menu_seen = true;
                // Resumed: the game's own pause tiles are not drawn as they leave.
                blockplanet::set_pause_exit_hidden(!after_row && paused && menu_seen && !blockplanet::ui::menus_open());
                if (paused && menu_seen && !after_row && !blockplanet::ui::menus_open() &&
                    guest::load16(guest_rdram, 0x800E1202) == 0)
                    paused = false;
                // A new Start press in play: the game is about to pause.
                const auto start_ms = last_start_ms.load();
                if (start_ms != start_seen) {
                    start_seen = start_ms;
                    if (playing && !paused && !game_over && !blockplanet::ui::menus_open() && !launching()) start_at = now_g;
                }
            }
            if (paused || (start_at && now_g - *start_at > std::chrono::milliseconds(1200))) start_at.reset();  // paused, or it did not
            if (was_paused && !paused) unpaused_at = now_g;
            was_paused = paused;
            if (unpaused_at && now_g - *unpaused_at > std::chrono::milliseconds(resume_ms)) unpaused_at.reset();
            const auto screen = blockplanet::ui::current_screen();
            if (grid_quit_at && ((blockplanet::ui::menus_open() && !screen.empty() && screen != "pause" && screen != "confirm" &&
                                  screen != "gameover" && screen != "episode_end" && screen != "start_card") ||
                                 now_g - *grid_quit_at > std::chrono::seconds(8)))
                grid_quit_at.reset(), grid_quit_red = false;
            const bool over_pause = start_at || paused || unpaused_at || (grid_quit_at && !grid_quit_red);
            // The end menus (and Exit from them): red, as the game over screen.
            const bool over_end = (end_menu_active && !end_menu_cleared) || (grid_quit_at && grid_quit_red);
            // The box art shows over BlockPlanet's grid (tetrisphere/box_art.h),
            // which stays up as the title hands over to the menus (half a
            // second, until they open), on the way back to it from them, and
            // on the way to the demo, over the game's own menu.
            const bool box_art = tetrisphere::box_art::showing().load(std::memory_order_relaxed);
            static std::optional<std::chrono::steady_clock::time_point> box_art_left;
            if (box_art) box_art_left = now_g, box_art_returning_since.reset();
            const bool box_art_handover = box_art_left && now_g - *box_art_left < std::chrono::milliseconds(500);
            const bool box_art_returning = box_art_returning_since && now_g - *box_art_returning_since < std::chrono::seconds(3);
            const bool to_demo = tetrisphere::box_art::phase().load() == tetrisphere::box_art::Phase::ToDemo;
            const bool box_art_grid = box_art || box_art_handover || box_art_returning || to_demo;
            blockplanet::ui::set_grid_override(over_pause && !launching()            ? "pause"
                                               : over_end && !mode_launcher.active() ? "red"
                                               : end_menu_cleared && !mode_launcher.active() ? "green"
                                               : box_art_grid                        ? "blue"
                                                                                     : "");
        }
        // Research: BLOCKPLANET_HOLD_GAMEOVER=1 holds the fade after a game
        // over (0x800E1202 back to 0 while the end menu fades in), to see
        // whether the GAME OVER scene stays on screen.
        {
            static const bool hold = std::getenv("BLOCKPLANET_HOLD_GAMEOVER") != nullptr;
            if (hold && guest_rdram != nullptr && guest::load16(guest_rdram, 0x800E1228) >= 0 &&
                guest::load16(guest_rdram, 0x800DFF08) == 4 && std::int16_t(guest::load16(guest_rdram, 0x80112F9E)) < 0)
                guest::store16(const_cast<std::uint8_t*>(guest_rdram), 0x800E1202, 0);
        }
        const auto now = std::chrono::steady_clock::now();
        if (perf_log_enabled && now - perf_window_start >= perf_interval) {
            auto& metrics = perf_metrics();
            PerfWindow window;
            window.window_ms = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - perf_window_start).count());
            const auto vis = observed_vis.load(std::memory_order_relaxed);
            const auto rejections = tetrisphere_ai_fifo_rejections.load(std::memory_order_relaxed);
            window.vis = vis - perf_last_vis;
            window.audio_rejections = rejections - perf_last_rejections;
            window.display_list = metrics.display_list.take();
            window.display_list_rt64 = metrics.display_list_rt64.take();
            window.screen_update = metrics.screen_update.take();
            window.audio_wait = metrics.audio_wait.take();
            window.present_mode = perf_present_mode;
            const auto line = format_perf_report(window);
            std::fprintf(stdout, "%s\n", line.c_str());
            std::fflush(stdout);
            diagnostic_log().perf(line);
            perf_last_vis = vis;
            perf_last_rejections = rejections;
            perf_window_start = now;
        }
        if (now >= next_trophy_save) {
            save_trophies();
            save_profile();
            next_trophy_save = now + std::chrono::seconds(30);
        }
        if (report_audio && now >= next_audio_report) {
            const auto pacing = audio_output.pacing_snapshot();
            if (pacing.submissions != 0) {
                const auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - event_loop_start).count();
                std::fprintf(stdout,
                             "{\"event\":\"audio_pacing\",\"run_id\":\"%s\","
                             "\"uptime_ms\":%lld,\"requested_hz\":%u,\"obtained_hz\":%u,"
                             "\"device_buffer_frames\":%u,\"vis\":%llu,\"input_polls\":%llu,"
                             "\"audio_rsp_tasks\":%llu,\"audio_rsp_total_us\":%llu,\"audio_rsp_max_us\":%llu,"
                             "\"ai_attempts\":%llu,\"ai_fifo_rejections\":%llu,"
                             "\"ai_fifo_wait_events\":%llu,\"ai_fifo_waits_ms\":%llu,"
                             "\"ai_length_caps\":%llu,\"submissions\":%llu,"
                             "\"submitted_frames\":%llu,\"empty_software_queue_count\":%llu,"
                             "\"queued_frames\":%zu}\n",
                             run_id(), static_cast<long long>(uptime_ms),
                             pacing.requested_frequency, pacing.obtained_frequency,
                             static_cast<unsigned>(pacing.device_buffer_frames),
                             static_cast<unsigned long long>(observed_vis.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(input_polls.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(audio_rsp_tasks.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(audio_rsp_total_us.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(audio_rsp_max_us.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_attempts.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_fifo_rejections.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_fifo_wait_events.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_fifo_waits_ms.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_length_caps.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(pacing.submissions),
                             static_cast<unsigned long long>(pacing.submitted_frames),
                             static_cast<unsigned long long>(pacing.empty_software_queue_count),
                             pacing.current_software_queue_frames);
                std::fflush(stdout);
            }
            next_audio_report = now + std::chrono::seconds(10);
        }
        SDL_Delay(2);
    }
}

} // namespace tetrisphere

// Research aid (2026-11-08, Training's lost bricks): when the generated code is
// rebuilt with its stores routed here (a private, temporary rewrite of the
// generated C), BLOCKPLANET_STORE_TRAP=from-to logs each store into that guest
// address range (hex), with the storing function, the value and the time.
extern "C" const char* blockplanet_trap_caller = "";  // set before calls the research build marks
extern "C" const char* blockplanet_trap_caller2 = "";
extern "C" const char* blockplanet_trap_caller3 = "";
// Research: a function's arguments (a0 to a3), logged when BLOCKPLANET_STORE_TRAP is set.
extern "C" void blockplanet_arg_trap(const char* function, std::uint64_t a0, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3) {
    if (std::getenv("BLOCKPLANET_STORE_TRAP") == nullptr) return;
    std::fprintf(stdout, "{\"event\":\"args\",\"function\":\"%s\",\"caller\":\"%s\",\"a0\":\"0x%08X\",\"a1\":%lld,\"a2\":%lld,\"a3\":%lld}\n",
                 function, blockplanet_trap_caller3, std::uint32_t(a0), (long long)std::int64_t(a1), (long long)std::int64_t(a2), (long long)std::int64_t(a3));
}
extern "C" void blockplanet_store_trap(std::uint32_t address, std::int32_t value, int size, const char* function) {
    static const std::pair<std::uint32_t, std::uint32_t> range = [] {
        const char* spec = std::getenv("BLOCKPLANET_STORE_TRAP");
        if (spec == nullptr) return std::pair<std::uint32_t, std::uint32_t>{1, 0};
        const char* dash = std::strchr(spec, '-');
        return std::pair<std::uint32_t, std::uint32_t>{std::uint32_t(std::strtoul(spec, nullptr, 16)),
                                                       dash ? std::uint32_t(std::strtoul(dash + 1, nullptr, 16)) : 0u};
    }();
    if (address < range.first || address > range.second) return;
    static const auto started = std::chrono::steady_clock::now();
    std::fprintf(stdout, "{\"event\":\"store\",\"address\":\"0x%08X\",\"value\":%d,\"size\":%d,\"function\":\"%s\",\"caller\":\"%s\",\"caller2\":\"%s\",\"ms\":%lld}\n", address,
                 value, size, function, blockplanet_trap_caller, blockplanet_trap_caller2,
                 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()));
}
