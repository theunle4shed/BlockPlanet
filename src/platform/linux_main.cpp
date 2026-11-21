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
#include "tetrisphere/f3d_flat_shade.h"
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
#include "tetrisphere/frame_step.h"
#include "tetrisphere/magic_model.h"
#include "tetrisphere/audio_settings.h"
#include "tetrisphere/jukebox.h"
#include "tetrisphere/vortex_backdrop.h"
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
// The Campaign list the launch path came from ("campaign" or "lines_campaign";
// empty for other episode lists): rebuilt when the menus come back, so it
// moves on to the episode to carry on.
std::string campaign_source;
// Carry on (the episode end's): the game's CONTINUE then shows its own card
// for the next episode (the pause row at 100, "Congratulations ... Ready to
// try the next one?"), waiting for A; BlockPlanet presses it, unseen under
// the loading cover (its own panel has said the congratulations).
std::optional<std::chrono::steady_clock::time_point> episode_card_skip;
bool episode_card_up = false;  // the card is on screen now (the loading cover holds over it)
// An end screen's Exit: the game hidden until it is parked behind the menus
// again (4 s at most), so its own menu (the episode end's Exit runs through
// the game's routine) does not show for a moment (the owner, 2026-11-18).
std::optional<std::chrono::steady_clock::time_point> exit_hide_since;
bool exit_hide_let_out = false;
std::size_t campaign_resume_index(bool lines);  // (below) the Campaign list's place of the episode to carry on
void restore_launch_path() {
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(1)) return;
    last = now;
    for (const auto& [screen, focus] : launch_path) {
        if (screen == "stages" && launch_stages_chapter > 0)
            blockplanet::ui::send_command("stages:" + std::to_string(launch_stages_chapter) + ":refresh");
        if (screen == "chapters" && !campaign_source.empty()) blockplanet::ui::send_command(campaign_source + "_refresh");
    }
    // Back on the Campaign list: the episode to carry on chosen, not the one
    // last started (after clearing an episode, the next one).
    auto path = launch_path;
    for (auto& [screen, focus] : path)
        if (screen == "chapters" && !campaign_source.empty()) focus = campaign_resume_index(campaign_source == "lines_campaign");
    blockplanet::ui::request_restore(path);
}
std::string loading_title;
// Quit to menu was chosen from the pause: the menu grid stays up until the
// menus open again (docs/UI.md, Menu grid).
std::optional<std::chrono::steady_clock::time_point> grid_quit_at;
// The game's end menus under BlockPlanet's panel (EndMenuBridge): while they
// (or their fade) are up the grid shows in red, and after Exit too.
bool end_menu_active = false;
bool end_menu_cleared = false;  // the end menu is an episode's end (not a game over)
// Rescue's final episode: its last stage cleared (set by the HUD), and
// BlockPlanet's credits shown in place of the game's own (the owner,
// 2026-11-19: after Episode 10 the game went on to its credits with nothing
// seen; "we need to kill the original credits").
bool final_stage_cleared = false;
bool final_credits_open = false;
bool final_stage_cleared_seen = false;  // research (BLOCKPLANET_FINAL_RAW)
// Rescue's finale after 10:10 (the owner, 2026-11-19: "it would be a shame to
// hide it away"): the game's curtain call plays with no HUD and no cover;
// any button skips it; then BlockPlanet's credits, to Sappy.
bool final_finale = false;
// The magic lessons (BlockPlanet's own Training, 2026-11-20): each magic on a
// striped sphere (layers alternating blue squares and yellow bars, Casual 1:1,
// six layers), so its damage shows how deep it went. Phases: 0 armed, 1 board
// built and card up, 2 waiting for the magic, 3 used (finishing), -1 none.
int magic_lesson = -1;
int magic_lesson_phase = -1;
std::optional<std::chrono::steady_clock::time_point> magic_lesson_at;
int magic_lesson_cells = 0;  // the board's bricks as the lesson starts (to see that the magic broke some)
const char* const magic_lesson_names[6] = {"Firecracker", "Dynamite", "Magnet", "Atom", "Bomb", "Ray Gun"};
// What each magic does, in BlockPlanet's words (docs/research/blockplanet-score-and-magic.md).
const char* const magic_lesson_texts[6] = {
    "The Firecracker blasts a round patch of the top layer, about four bricks out from your shadow in every direction.",
    "The Dynamite throws five sticks in a ring round your shadow. Each blows a small hole in the top layer, leaving the middle standing.",
    "The Magnet pulls bricks out from under your shadow for about four seconds. Hold it still to dig deep, or move the shadow to dig where you want.",
    "The Atom clears the whole top layer of the sphere: the far side first, then the part you can see, from its edge to the middle.",
    "The Bomb blasts a wide patch of the top layer, about six bricks out in every direction, then a second layer in the middle.",
    "The Ray Gun drills straight down under your shadow, one layer at a time, for about seven seconds. Move the shadow to drill somewhere else.",
};
// The Drop lesson (BlockPlanet's own Training, 2026-11-20, docs/TRAINING_REDESIGN.md):
// show the move, reset the board, then the player's turn. Casual 1:1, its top
// layer walls but for a hole and a matching pair (build_pair_board). Phases:
// -1 none, 0 armed, 1 card up, 2 the demonstration, 3 its bricks breaking,
// 4 the player's turn, 5 a miss (board set up again), 6 done, 7 leaving.
int drop_lesson_phase = -1;
// Rescue's background for episode 1 while the Drop lesson's stage loads: the
// original Training's (index 25, its fixed table 0x800E949C) in place of 1:1's
// (Rescue's table by episode, 0x800E94C4); put back once in play (-1: not changed).
int drop_lesson_saved_background = -1;
// A new lesson's stage (Casual 1:1) waiting to start: tried each frame until
// the game takes it (the owner, 2026-11-21: once a lesson left him on Casual's
// episode list, the start refused while the game was still busy).
std::optional<std::chrono::steady_clock::time_point> lesson_launch_pending;
std::optional<std::chrono::steady_clock::time_point> drop_lesson_at;
int drop_lesson_home[2] = {0, 0};    // the cursor's cell as the lesson starts
int drop_lesson_target[2] = {0, 0};  // the hole beside the pair
int drop_lesson_bricks = 0;          // the board's bricks as the turn starts (more: a drop that missed)
std::uint32_t drop_lesson_score = 0;
std::string drop_lesson_say;         // the HUD's line: what is happening now
// The demonstration's presses: one each 400 ms, steered by where the cursor is.
int drop_demo_slot = -1;
std::uint16_t drop_demo_held = 0;
bool drop_demo_aimed = false, drop_demo_dropped = false;
// The choice before each go (the owner, 2026-11-20): A tries it, B watches the
// demonstration first; the game gets no input meanwhile. Waits for every button
// to be let go before taking a press.
bool drop_lesson_watched = false, drop_choice_armed = false;
// The Slide lesson (the owner's design, 2026-11-20): two squares far apart in
// a small pit, one to be slid across until they touch; dropping is off (the
// owner first tried six: too many for a first lesson; bigger groups come with
// combos).
// Phases as the Drop lesson's: -1 none, 0 armed, 1 choosing (A try, B watch),
// 2 the demonstration, 4 the player's turn, 6 done, 7 leaving.
int slide_lesson_phase = -1;
std::optional<std::chrono::steady_clock::time_point> slide_lesson_at;
int slide_origin[2] = {0, 0};  // where the layout's (0, 0) is on the board
int slide_square = -1;         // the stage's square piece (its queue index)
int slide_group = 0;           // squares in the biggest group now (shown in the panel)
bool slide_watched = false, slide_choice_armed = false;
// The panel's step while choosing (the owner's script, 2026-11-20): each A adds
// a line beneath the last; a new page clears the box. 0 and 1 "Slide and Drop"
// (one, two lines), 2 its third line on a page of its own, 3 to 5 "How to
// Slide" (one, two, three lines, the last with the choice: A try it, B show me).
// Every step that waits for A says so ([A] Next).
int slide_page = 0;
constexpr int slide_last_page = 5;
// The drop stage (the owner, 2026-11-20: Drop carries on from Slide on the
// same board, no new intro): phases 10 choosing (two lines, then A try it, B
// show me), 11 the demonstration, 12 the player's turn, 13 a miss, 14 done,
// 15 leaving. The board as the drop stage began, put back after the
// demonstration or a miss, with the shadow where it was.
int slide_drop_page = 0;
bool slide_drop_watched = false, slide_drop_start = false;  // start: begun at the drop stage (Drop chosen from the list)
int slide_drop_cursor[2] = {0, 0}, slide_drop_target[2] = {0, 0};
std::uint32_t slide_drop_score = 0;
int slide_drop_bricks = 0;
bool slide_drop_aimed = false, slide_drop_dropped = false;
int slide_drop_slot = -1;
std::uint16_t slide_drop_held = 0;
std::optional<std::chrono::steady_clock::time_point> slide_drop_dropped_at;
std::vector<std::uint8_t> slide_drop_board;
// After a choice in a lesson, the game gets no input until every button is let
// go (the A that chose Try it would otherwise drop at once).
bool lesson_wait_release = false;
constexpr std::uint32_t pc_ctx = 0x80110220u;  // player 1's context (for jobs on the game's thread)
// Hearts and the Drop Timer (2026-11-20), run by the same machinery
// (hearts_lesson): phases 20 choosing (pages 0 to 3), 21 the demonstration,
// 22 the player's turn, 23 after a drop (a pause, a new pair), 24 done,
// 25 leaving. Casual's heart refill is off meanwhile; the lesson refills them.
bool hearts_lesson = false;
int hearts_page = 0;
constexpr int hearts_last_page = 3;
bool hearts_watched = false;
int hearts_demo_part = 0;  // 0 the timer runs out on open floor, 1 a heart lost, 2 to the pair, 3 dropped
std::optional<std::chrono::steady_clock::time_point> hearts_demo_at;
int hearts_good = 0, hearts_before = 3, hearts_bricks = -1;
std::uint32_t hearts_score = 0;
std::string hearts_note;
constexpr int hearts_goal = 3;
int hearts_target[2] = {0, 0};
int hearts_slot = -1;
std::uint16_t hearts_held = 0;
bool hearts_aimed = false, hearts_aim_pending = false;
// The drop timer set by the lesson, so the bar runs out in a few seconds (the
// stage's own rate took over a minute): the game's value (+0x2408, counting up
// to its limit, 6000) set from the time since the piece came.
std::optional<std::chrono::steady_clock::time_point> hearts_timer_from;
// The three good drops each with another shape (the owner: a blue, a yellow and
// a red example): the stage's square (0) and bar (1), then the red T, put in
// the stage's shape table as entry 2 for the lesson (the six kinds' records are
// all built: 0x800E4508 points at them; the T is the sixth, material 37, red).
int hearts_round = 0;
std::uint32_t hearts_table2_saved = 0;
int hearts_kinds_saved = -1;  // the stage's number of shapes (0x800E44B8), raised to 3 for the T
int hearts_shape() { return hearts_round == 0 ? 0 : hearts_round == 1 ? 1 : 2; }
// Combos and Magic (2026-11-21, the owner's design; docs/TRAINING_REDESIGN.md),
// run by the same machinery (combos_lesson): phases 30 the pages and the
// choice, 31 the demonstration, 32 the player's turn, 33 between drops, 34 the
// ending's pages, 35 leaving. Three layers dug in turn in one pit: blue squares
// at the top, red Ts, then yellow bars on the floor, five of each; each drop on
// the group breaks six and tops up the magic meter, which starts at 11 of 14,
// so the third fills it and a power-up is unlocked.
bool combos_lesson = false;
int combos_page = 0;
constexpr int combos_last_page = 4;
bool combos_watched = false;
int combos_step = 0;               // the layer being broken (0 blue, 1 red, 2 yellow)
int combos_end_page = 0;
int combos_anchor[2] = {0, 0};     // the stack's place on the board (its first piece's cell)
int combos_target[2] = {0, 0};     // a piece of the current group (the demonstration aims there)
int combos_meter_before = 0, combos_bricks = -1;
std::uint32_t combos_score = 0;
std::string combos_note;
bool combos_dropped = false, combos_aimed = false, combos_demo_running = false;
int combos_slot = -1;
std::uint16_t combos_held = 0;
std::optional<std::chrono::steady_clock::time_point> combos_at;
constexpr int combos_shapes[3] = {0, 2, 1};  // the shape table's entries: the square (blue), the T (red, entry 2), the bar (yellow)
int combo_shape() { return combos_shapes[std::clamp(combos_step, 0, 2)]; }
// Rubble and Mystery Blocks (2026-11-21, the owner's idea; docs/TRAINING_REDESIGN.md),
// the same machinery (rubble_lesson): phases 40 the pages and the choice, 41
// the demonstration, 42 the player's turn, 43 the combo playing out; then the
// mystery block: 44 its pages and the choice, 45 the demonstration, 46 the
// player's turn, 47 the ending's pages, 48 leaving. Part 1: three yellow bars
// split by two lines of rubble; B over a line turns it into a bar, then a
// drop breaks all five and the dropped one. Part 2: a bar beside three lines of
// rubble with a mystery block in the middle one; sliding the bar through the
// rubble frees it (a power-up appears).
bool rubble_lesson = false;
int rubble_page = 0, rubble_end_page = 0;
bool rubble_watched = false, rubble_demo_running = false;
int rubble_anchor[2] = {0, 0};   // the column's middle (part 1) or the bar (part 2), a cursor cell
int rubble_across[2] = {0, 1};   // across the bar: the column's direction
int rubble_side = 1;             // the column's side of the shadow along the bars (+1: higher cells, left on screen)
int rubble_bricks = -1, rubble_combo = 0;
std::string rubble_note;
std::optional<std::chrono::steady_clock::time_point> rubble_at;
int rubble_slot = -1, rubble_step = 0, rubble_pushes = 0;
bool rubble_aimed = false;
std::uint16_t rubble_held = 0;
constexpr int rubble_shape = 1;  // the bar (yellow): part 2's piece
// Part 1 three times, a shape each (the owner, 2026-11-21: different block
// types): yellow bars (five groups: bar, crystals, bar, crystals, bar), then
// blue squares and red Ts (three groups: piece, crystals, piece; taller
// shapes, so shorter columns to stay in view). The T is put in the shape
// table as entry 2, as Combos and Magic does.
constexpr int rubble_round_shapes[3] = {1, 0, 2};
int rubble_round = 0;
int rubble_round_shape() { return rubble_round_shapes[std::clamp(rubble_round, 0, 2)]; }
int rubble_groups() { return rubble_round == 0 ? 5 : 3; }
std::vector<std::pair<int, int>> rubble_lines_at, rubble_pieces_at;  // origins on the board now (for the demonstration)
int rubble_step_size = 1;      // a group's depth across the column
int rubble_demos = 0;          // part 2's demonstrations so far: they alternate between sliding and a combo
bool rubble_saw_combo = false;
constexpr int rubble_last_page = 4, rubble_last_page2 = 5;
// A shape's cells as offsets from its origin (layout cell (x, y) sits at the
// origin + (x - 1, y - 1)), in layout order.
std::vector<std::pair<int, int>> lesson_shape_cells(int index) {
    std::vector<std::pair<int, int>> out;
    if (guest_rdram == nullptr) return out;
    const std::uint32_t rec = guest::load32(guest_rdram, 0x8014CBC8u + 4u * std::uint32_t(index));
    for (int q = 0; q < 9; ++q)
        if (std::int16_t(guest::load16(guest_rdram, rec + 0xA + 2u * std::uint32_t(q))) > 0) out.push_back({q / 3 - 1, q % 3 - 1});
    return out;
}

// A single cell to place on layer 1 of a lesson's pit: rubble or a mystery
// block (values and texture entries as the game's own stages have them: rubble
// 9, the dark tile with the orb; the mystery block 63, flickering yellow and red;
// found in a Rescue 3:1 board, the same entries in every stage).
struct LessonCell { int x, y; int value; std::uint32_t entry; };
constexpr int rubble_value = 9, mystery_value = 63;
constexpr std::uint32_t rubble_entry = 0x80131FF0u, mystery_entry = 0x80134C70u;

// Rubble lines left on the board (part 1's progress), by cells.
int rubble_cells_left() {
    if (guest_rdram == nullptr) return 0;
    const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u);
    int n = 0;
    for (int c = 0; c < 1024; ++c) n += std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(1024 + c))) == rubble_value;
    return n;
}

bool slide_lesson_shown() {
    return (slide_lesson_phase >= 1 && slide_lesson_phase <= 6) || (slide_lesson_phase >= 10 && slide_lesson_phase <= 14) ||
           (slide_lesson_phase >= 20 && slide_lesson_phase <= 24) || (slide_lesson_phase >= 30 && slide_lesson_phase <= 34) ||
           (slide_lesson_phase >= 40 && slide_lesson_phase <= 47);
}
// The layout: the first square stays, the second slides four cells across
// to touch it; the pit 8 x 6 round them, the shadow starting between them.
constexpr int slide_pieces = 2, slide_moves = 1;
constexpr int slide_starts[slide_pieces][2] = {{0, 0}, {6, 0}};
constexpr int slide_finals[slide_pieces][2] = {{0, 0}, {2, 0}};
constexpr int slide_order[slide_moves] = {1};
constexpr int slide_home[2] = {3, 0};  // the shadow's start, from the layout's (0, 0)
// The demonstration: piece slide_order[step], stage 0 to its start, 1 B held,
// 2 to its final place (B held), 3 let go.
int slide_demo_step = 0, slide_demo_stage = 0, slide_demo_slot = -1;
std::optional<std::chrono::steady_clock::time_point> slide_demo_stage_at;
std::uint16_t slide_demo_held = 0;
// Dropping switched off (the player's A taken out), in the Slide lesson.
std::atomic<bool> lesson_no_drop{false};
// Sliding switched off (the player's B taken out), in Combos and Magic (the
// owner, 2026-11-21: each layer laid out to break whole; a slide could spoil it).
std::atomic<bool> lesson_no_slide{false};
// Research (what each magic does): research_magic:K gives player 1 magic K
// (0 to 5) and presses the magic button (N64 0x0004) a moment later.
std::optional<std::chrono::steady_clock::time_point> research_magic_press;
// The fuse lab's automatic slide (research_fuse_auto:A|B): as soon as a combo
// starts, the cursor goes onto the chosen piece, B is held, up is pressed
// three times, and B is let go (the timing a script cannot hit).
int research_fuse_target = 0;  // 0 off, 1 B, 2 A (armed)
int research_fuse_xy[2][2] = {};
int research_fuse_steps = 3;  // up presses: the corridor's length
int research_fuse_delay = 0;  // ms after the combo starts before the grab
std::optional<std::chrono::steady_clock::time_point> research_fuse_at;
bool final_skip = false;
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
std::atomic<bool> minefield_end_chosen{false};  // a choice made on the game's end menu: no fallback panel
std::atomic<bool> game_over_frozen{false};  // the shell closing before the game's game over
// BlockPlanet's own pause (PauseBridge::set_parker): in play, Start is kept
// from the game (own_pause_ready) and asks for BlockPlanet's pause instead
// (own_pause_start); bp_paused while the game is parked for it.
std::atomic<bool> own_pause_ready{false};
std::atomic<bool> own_pause_mask{false};  // in play with BlockPlanet's pause on: Start never reaches the game
std::atomic<bool> own_pause_start{false};
std::atomic<bool> bp_paused{false};
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
std::size_t campaign_resume_index(bool lines) {
    const char* game = lines ? "lines" : "rescue";
    const int count = lines ? lines_chapters : 10;  // (Rescue: rescue_chapters, below)
    const auto& chapters = profile_data.progress.chapters_completed;
    const auto done = chapters.find(game);
    int resume = 1;
    while (resume < count && done != chapters.end() && done->second.count(resume)) ++resume;
    return std::size_t(resume - 1);  // the list starts at Episode 1
}
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
    bool episode = false;  // a whole episode (the attack modes)
    int lesson = -1;       // a magic lesson (0 to 5), shown with its own items
};
std::optional<StageResults> pending_results;
// A whole episode in an attack mode, its last stage cleared: shown once play
// is left (the owner, 2026-11-19: after the "well dones", back to the menu,
// no carrying on to the next episode).
std::optional<StageResults> attack_episode_results;
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
    {
        char line[120];
        std::snprintf(line, sizeof line, "{\"event\":\"results_shown\",\"lesson\":%d,\"chapter\":%d}", r.lesson, r.chapter);
        diagnostic_log().perf(line);
    }
    pending_results.reset();
    std::string line;
    if (r.value <= 0.0) {
    } else if (r.points) {
        line = hud_number(static_cast<long long>(r.value)) + " points";
        if (r.best_before <= 0.0 || r.value > r.best_before) line += "  \u00B7  New best";
        else line += "  \u00B7  Best " + hud_number(static_cast<long long>(r.best_before));
    } else {
        line = hud_time(r.value);
        if (r.best_before <= 0.0 || r.value < r.best_before) line += "  \u00B7  New best";
        else line += "  \u00B7  Best " + hud_time(r.best_before) + " (" + hud_delta(r.value - r.best_before) + ")";
    }
    if (r.lesson == 105) {
        slide_lesson_phase = -1, rubble_lesson = false;
        blockplanet::ui::set_text("results_title", "Crystals and Mystery Blocks Complete");
        blockplanet::ui::set_text("results_where", "Training \u00B7 The Basics");
        blockplanet::ui::set_text("results_line", "");
        std::vector<blockplanet::ui::ListItem> items;
        items.push_back({"again", "Try Again", "", "command:new_lesson:rubble", true, 1, false});
        items.push_back({"back", "Back to Training", "", "back", true, 1, true});
        blockplanet::ui::set_list("results", items);
        blockplanet::ui::request_show("results");
        return;
    }
    if (r.lesson == 104) {
        slide_lesson_phase = -1, combos_lesson = false;
        blockplanet::ui::set_text("results_title", "Combos and Magic Complete");
        blockplanet::ui::set_text("results_where", "Training \u00B7 The Basics");
        blockplanet::ui::set_text("results_line", "");
        std::vector<blockplanet::ui::ListItem> items;
        items.push_back({"again", "Try Again", "", "command:new_lesson:combos", true, 1, false});
        items.push_back({"back", "Back to Training", "", "back", true, 1, true});
        blockplanet::ui::set_list("results", items);
        blockplanet::ui::request_show("results");
        return;
    }
    if (r.lesson == 103) {
        slide_lesson_phase = -1, hearts_lesson = false;
        if (hearts_table2_saved && guest_rdram) guest::store32(const_cast<std::uint8_t*>(guest_rdram), 0x8014CBC8u + 8u, hearts_table2_saved);
        hearts_table2_saved = 0;
        if (hearts_kinds_saved >= 0 && guest_rdram) guest::store16(const_cast<std::uint8_t*>(guest_rdram), 0x800E44B8u, std::uint16_t(hearts_kinds_saved));
        hearts_kinds_saved = -1;
        blockplanet::ui::set_text("results_title", "Hearts and the Drop Timer Complete");
        blockplanet::ui::set_text("results_where", "Training \u00B7 The Basics");
        blockplanet::ui::set_text("results_line", "");
        std::vector<blockplanet::ui::ListItem> items;
        items.push_back({"again", "Try Again", "", "command:new_lesson:hearts", true, 1, false});
        items.push_back({"back", "Back to Training", "", "back", true, 1, true});
        blockplanet::ui::set_list("results", items);
        blockplanet::ui::request_show("results");
        return;
    }
    if (r.lesson == 102) {
        slide_lesson_phase = -1;
        blockplanet::ui::set_text("results_title", "Slide and Drop Complete");
        blockplanet::ui::set_text("results_where", "Training \u00B7 The Basics");
        blockplanet::ui::set_text("results_line", "");
        std::vector<blockplanet::ui::ListItem> items;
        items.push_back({"again", "Try Again", "", "command:new_lesson:slide", true, 1, false});
        items.push_back({"back", "Back to Training", "", "back", true, 1, true});
        blockplanet::ui::set_list("results", items);
        blockplanet::ui::request_show("results");
        return;
    }
    if (r.lesson == 101) {
        slide_lesson_phase = -1;
        blockplanet::ui::set_text("results_title", "Slide Complete");
        blockplanet::ui::set_text("results_where", "Training \u00B7 The Basics");
        blockplanet::ui::set_text("results_line", "");
        std::vector<blockplanet::ui::ListItem> items;
        items.push_back({"next", "Next: Drop", "", "command:new_lesson:drop", true, 1, true});
        items.push_back({"again", "Try Again", "", "command:new_lesson:slide", true, 1, false});
        items.push_back({"back", "Back to Training", "", "back", true});
        blockplanet::ui::set_list("results", items);
        blockplanet::ui::request_show("results");
        return;
    }
    if (r.lesson == 100) {
        // The Drop lesson (the next Basics lessons are still to be built).
        drop_lesson_phase = -1;
        blockplanet::ui::set_text("results_title", "Drop Complete");
        blockplanet::ui::set_text("results_where", "Training \u00B7 The Basics");
        blockplanet::ui::set_text("results_line", "");
        std::vector<blockplanet::ui::ListItem> items;
        items.push_back({"again", "Try Again", "", "command:new_lesson:drop", true, 1, false});
        items.push_back({"back", "Back to Training", "", "back", true, 1, true});
        blockplanet::ui::set_list("results", items);
        blockplanet::ui::request_show("results");
        return;
    }
    if (r.lesson >= 0 && r.lesson < 6) {
        // A magic lesson: its own title and items.
        const int k = r.lesson;
        magic_lesson = -1, magic_lesson_phase = -1;
        blockplanet::ui::set_text("results_title", std::string(magic_lesson_names[k]) + " Complete");
        blockplanet::ui::set_text("results_where", "Training \u00B7 Magic");
        blockplanet::ui::set_text("results_line", "");
        std::vector<blockplanet::ui::ListItem> items;
        if (k < 5)
            items.push_back({"next", std::string("Next: ") + magic_lesson_names[k + 1], "", "command:magic_lesson:" + std::to_string(k + 1), true, 1, true});
        items.push_back({"again", "Try Again", "", "command:magic_lesson:" + std::to_string(k), true, 1, k >= 5});
        items.push_back({"back", "Back to Training", "", "back", true});
        blockplanet::ui::set_list("results", items);
        blockplanet::ui::request_show("results");
        return;
    }
    blockplanet::ui::set_text("results_title", r.episode ? "Episode " + std::to_string(r.chapter) + " Cleared" : "Stage Cleared");
    blockplanet::ui::set_text("results_where", r.where);
    blockplanet::ui::set_text("results_line", line);
    // No Next Stage: a stage or episode chosen from the menus is played on
    // its own, then back to them (the owner, 2026-11-19).
    std::vector<blockplanet::ui::ListItem> items;
    items.push_back({"again", "Try Again", "",
                     r.episode ? "command:chapter:" + std::to_string(r.chapter)
                               : "command:stage:" + std::to_string(r.chapter) + ":" + std::to_string(r.level),
                     true, 1, true});
    items.push_back({"back", r.episode ? std::string("Back to Episodes") : "Back to Episode " + std::to_string(r.chapter), "", "back", true});
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
    case 11: return "Glass tower";
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
    if (final_finale) view.reset();  // the finale plays with no HUD
    // A magic lesson's HUD: only the magic (its box, with the model) and how
    // many are left (the owner, 2026-11-20: show only what the lesson needs).
    if (view && magic_lesson >= 0 && magic_lesson < 6 && magic_lesson_phase >= 1) {
        auto& v = *view;
        v.show_lives = false;
        v.show_next = false;
        v.mode = "Training";
        v.stage = magic_lesson_names[magic_lesson];
        v.goal_label = "Magic left";
        v.goal = magic_lesson_phase >= 3 ? "0" : "1";
        v.time.clear(), v.clock.clear(), v.score.clear(), v.story_score.clear(), v.story_gains.clear();
        v.score_best.clear(), v.best.clear(), v.time_delta.clear(), v.splits.clear();
        v.combo = 0, v.multiplier = -1, v.progress = -1.0f, v.drops_left = -1, v.slides_left = -1;
    }
    // The Drop lesson's HUD: Training's layout (the owner, 2026-11-20): the
    // next pieces in a row under the sphere (centred: Casual's camera), the
    // lesson's text in the panel to its right (publish_game_text).
    if (view && ((drop_lesson_phase >= 1 && drop_lesson_phase <= 6) || slide_lesson_shown())) {
        auto& v = *view;
        v.bottom_row = true;
        v.row_column = 94.0f;  // the sphere moved left, as Training's (update_lesson_viewport)
        v.show_lives = false;
        v.show_magic = false;
        v.mode = "Training";
        v.stage = rubble_lesson && slide_lesson_phase >= 1 ? "Crystals and Mystery Blocks"
                  : combos_lesson && slide_lesson_phase >= 1 ? "Combos and Magic"
                  : hearts_lesson && slide_lesson_phase >= 1 ? "Hearts" : slide_lesson_phase >= 1 ? "Slide and Drop" : "Drop";
        if (combos_lesson && slide_lesson_phase >= 30) v.show_magic = true;  // the magic meter's ring
        if (rubble_lesson && slide_lesson_phase >= 44) v.show_magic = true;  // where the freed power-up shows
        if (hearts_lesson && slide_lesson_phase >= 20) v.show_lives = true;  // the hearts and the drop timer's bar
        // Only as many next pieces as the lesson needs (the owner: Slide shows one).
        // (Combos and Magic shows three: the layers' shapes in order.)
        const std::size_t room = combos_lesson ? 3 : 1;
        if (slide_lesson_phase >= 1 && v.next.size() > room) v.next.resize(room);
        if (slide_lesson_phase >= 1) v.next_room = int(room);  // and the box only as wide as one (the owner: keep the HUD small)
        v.goal_label = "Now";
        v.goal = drop_lesson_say;
        v.time.clear(), v.clock.clear(), v.score.clear(), v.story_score.clear(), v.story_gains.clear();
        v.score_best.clear(), v.best.clear(), v.time_delta.clear(), v.splits.clear();
        v.combo = 0, v.multiplier = -1, v.progress = -1.0f, v.drops_left = -1, v.slides_left = -1;
    }
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
    if (state.drop_steps > 0) {
        // The two-stage bar: the calm stretch, then the fall with a count.
        view.drop_left = state.drop_stage_left;
        view.drop_phase = state.drop_phase;
        view.drop_seconds = state.drop_seconds;
        view.drop_notch = state.drop_fall_notch >= 0.0f ? std::round(100.0f * state.drop_fall_notch) / 100.0f : -1.0f;
        // A blink as each warning comes (into the fall, then fast).
        static int last_phase = 0;
        static std::chrono::steady_clock::time_point blink_at{};
        if (state.drop_phase > last_phase) blink_at = std::chrono::steady_clock::now();
        last_phase = state.drop_phase;
        view.drop_blink = std::chrono::steady_clock::now() - blink_at < std::chrono::milliseconds(700);
    }
    if (static const bool drop_log = std::getenv("BLOCKPLANET_DROP_LOG") != nullptr; drop_log) {
        static float logged = -1.0f;
        static bool zoom = false;
        if (std::fabs(state.drop_left - logged) >= 0.05f || state.drop_zoom != zoom || (state.drop_left == 0.0f) != (logged == 0.0f)) {
            logged = state.drop_left, zoom = state.drop_zoom;
            std::fprintf(stdout, "{\"event\":\"drop_bar\",\"ms\":%lld,\"left\":%.3f,\"zoom\":%d,\"hearts\":%d}\n",
                         static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),
                         state.drop_left, int(state.drop_zoom), state.hearts);
            std::fflush(stdout);
        }
    }
    view.magic = state.magic;
    view.magic_model = view.show_magic && blockplanet::magic_box_active();
    view.meter = state.meter;
    view.meter_count = state.meter_count, view.meter_steps = state.meter_steps;
    blockplanet::set_magic_charge(state.meter_steps > 0 ? 0.0f : state.meter);  // the ring shows the meter: no fill behind the model
    blockplanet::set_magic_box_plain(state.meter_steps > 0);
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

// A BlockPlanet lesson's sphere left of centre, as the original Training's
// (the owner, 2026-11-20: the sphere and the text sharing the screen). The
// game draws Casual's sphere through the viewport in its table near the
// scissors (scale 640 x 480, centre 640, 480 in quarter pixels), the one just
// before Versus' left half (centre 320); its centre is moved to Training's
// (377, the game's column 94). The background has its own viewport and stays.
void update_lesson_viewport(bool shifted) {
    static std::uint32_t found = 0;
    static bool moved = false;
    if (guest_rdram == nullptr) return;
    auto* rdram = const_cast<std::uint8_t*>(guest_rdram);
    const auto h = [&](std::uint32_t a) { return guest::load16(guest_rdram, a); };
    const auto valid = [&](std::uint32_t a) {
        return h(a) == 640 && h(a + 2) == 480 && h(a + 4) == 511 && (h(a + 8) == 640 || h(a + 8) == 377) && h(a + 10) == 480 &&
               h(a + 16) == 640 && h(a + 24) == 320;
    };
    if (found == 0 || !valid(found)) {
        found = 0;
        moved = false;
        for (std::uint32_t a = 0x80164000u; a < 0x80167000u; a += 8)
            if (valid(a)) { found = a; break; }
    }
    if (found == 0) return;
    if (shifted && !moved) guest::store16(rdram, found + 8, 377), moved = true;
    else if (!shifted && moved) guest::store16(rdram, found + 8, 640), moved = false;
}

void update_hud() {
    static const bool original = std::getenv("BLOCKPLANET_ORIGINAL_HUD") != nullptr;
    if (update_title_overlay()) return;
    // Research (the score formula and magic, 2026-11-19):
    // BLOCKPLANET_SCORE_LOG=1 logs player 1's scoring values whenever one
    // changes: the score (+0x2AF8), the combo's points so far (+0x2AFC, its
    // copy +0x2B00, the second total +0x2B04), the combo's step count
    // (+0x24BC), the bricks broken (+0x2AE0), the gravity and fuse counts
    // (+0x2AE6, +0x2AE8), the best combo points (+0x2C70), the magic meter
    // (+0x2AEC), the held magic (+0x702) and the hearts (+0x2D7E).
    if (static const bool score_log = std::getenv("BLOCKPLANET_SCORE_LOG") != nullptr; score_log && guest_rdram != nullptr) {
        const std::uint32_t p = 0x80110220u;
        char line[600];
        std::snprintf(line, sizeof line,
                      "\"score\":%u,\"combo_pts\":%u,\"copy\":%u,\"second\":%u,\"steps\":%d,\"bricks\":%d,\"gravity\":%d,"
                      "\"fuse\":%d,\"best\":%u,\"meter\":%d,\"magic\":%d,\"hearts\":%d,\"magic_on\":%d",
                      guest::load32(guest_rdram, p + 0x2AF8), guest::load32(guest_rdram, p + 0x2AFC), guest::load32(guest_rdram, p + 0x2B00),
                      guest::load32(guest_rdram, p + 0x2B04), std::int16_t(guest::load16(guest_rdram, p + 0x24BC)),
                      std::int16_t(guest::load16(guest_rdram, p + 0x2AE0)), std::int16_t(guest::load16(guest_rdram, p + 0x2AE6)),
                      std::int16_t(guest::load16(guest_rdram, p + 0x2AE8)), guest::load32(guest_rdram, p + 0x2C70),
                      std::int16_t(guest::load16(guest_rdram, p + 0x2AEC)), std::int8_t(guest::load8(guest_rdram, p + 0x702)),
                      std::int16_t(guest::load16(guest_rdram, p + 0x2D7E)), guest::load8(guest_rdram, p + 0x1269));
        // The piece queue's first 8 slots (+0x2374; 22 is the "?" piece).
        std::string queue;
        for (int k = 0; k < 8; ++k) queue += (k ? "," : "") + std::to_string(std::int16_t(guest::load16(guest_rdram, p + 0x2374 + 2u * k)));
        std::snprintf(line + std::strlen(line), sizeof line - std::strlen(line), ",\"queue\":[%s]", queue.c_str());
        static std::string last;
        if (last != line) {
            last = line;
            // (ms: for timing a magic's run, the Magnet's and Ray Gun's flag +0x1269.)
            static const auto t0 = std::chrono::steady_clock::now();
            std::fprintf(stdout, "{\"event\":\"score_state\",\"ms\":%lld,%s}\n",
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), line);
            std::fflush(stdout);
        }
    }
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
    const bool paused_now = story && (guest::load16(guest_rdram, 0x800E1228) >= 0 || bp_paused.load());
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
    update_lesson_viewport(((drop_lesson_phase >= 1 && drop_lesson_phase <= 7) || slide_lesson_shown() || slide_lesson_phase == 7 || slide_lesson_phase == 15 || slide_lesson_phase == 25 ||
                            slide_lesson_phase == 35 || slide_lesson_phase == 48) &&
                           state.mode == 1);
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
    const bool paused = guest::load16(guest_rdram, 0x800E1228) >= 0 || bp_paused.load();
    auto& episode = profile_data.progress.episode_records[variant_records(play_variant == PlayVariant::ScoreAttack ? PlayVariant::Story : play_variant)];
    const auto best_of = [&](int chapter) {
        const auto it = episode.find(chapter);
        return it == episode.end() ? std::vector<double>{} : it->second.best_levels;
    };
    if (state.mode != 1) {
    // The clock stops once the core is open (the owner, 2026-11-19): the
    // stage-end sequence (the core and the friend, NICE JOB, the fade, about
    // 10 s) is not play, so a stage's time ends as it is cleared, as Lines'
    // already does. It runs again when the next stage begins.
    } else if (const auto finished = splits.update(state.chapter, state.level,
                                                   !paused && elapsed < 1.0 && state.progress < 1.0f ? elapsed : 0.0,
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
        if (play_variant == PlayVariant::TimeAttack && !single_stage && finished->episode_done && finished->episode_total > 0.5)
            attack_episode_results = StageResults{finished->chapter, 10, false, finished->episode_total, best_total_before,
                                                  "Time Attack \u00B7 Episode " + std::to_string(finished->chapter), true};
        if (play_variant == PlayVariant::TimeAttack && finished->time > 0.5)
            attack_stage_done("time_attack", finished->chapter, finished->level, finished->time);
        // A single stage: done; back to the menu shortly.
        if (play_variant != PlayVariant::ScoreAttack && single_stage && finished->chapter == single_stage->first &&
            finished->level == single_stage->second) {
            single_stage_result = SingleStageResult{finished->time, best_before};
            single_stage_quit_at = std::chrono::steady_clock::now();  // at once: the grid already covers the next stage
        }
    }

    blockplanet::ui::HudView view;
    view.hearts = state.hearts;
    view.drop_left = state.drop_left;
    if (state.drop_steps > 0) {
        // The two-stage bar: the calm stretch, then the fall with a count.
        view.drop_left = state.drop_stage_left;
        view.drop_phase = state.drop_phase;
        view.drop_seconds = state.drop_seconds;
        view.drop_notch = state.drop_fall_notch >= 0.0f ? std::round(100.0f * state.drop_fall_notch) / 100.0f : -1.0f;
        // A blink as each warning comes (into the fall, then fast).
        static int last_phase = 0;
        static std::chrono::steady_clock::time_point blink_at{};
        if (state.drop_phase > last_phase) blink_at = std::chrono::steady_clock::now();
        last_phase = state.drop_phase;
        view.drop_blink = std::chrono::steady_clock::now() - blink_at < std::chrono::milliseconds(700);
    }
    if (static const bool drop_log = std::getenv("BLOCKPLANET_DROP_LOG") != nullptr; drop_log) {
        static float logged = -1.0f;
        static bool zoom = false;
        if (std::fabs(state.drop_left - logged) >= 0.05f || state.drop_zoom != zoom || (state.drop_left == 0.0f) != (logged == 0.0f)) {
            logged = state.drop_left, zoom = state.drop_zoom;
            std::fprintf(stdout, "{\"event\":\"drop_bar\",\"ms\":%lld,\"left\":%.3f,\"zoom\":%d,\"hearts\":%d}\n",
                         static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),
                         state.drop_left, int(state.drop_zoom), state.hearts);
            std::fflush(stdout);
        }
    }
    view.magic = state.magic;
    view.magic_model = blockplanet::magic_box_active();
    view.meter = state.meter;
    view.meter_count = state.meter_count, view.meter_steps = state.meter_steps;
    blockplanet::set_magic_charge(state.meter_steps > 0 ? 0.0f : state.meter);  // the ring shows the meter: no fill behind the model
    blockplanet::set_magic_box_plain(state.meter_steps > 0);
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
        // The core square to uncover, beneath the stage (the owner, 2026-11-19).
        const int square = guest::load16(guest_rdram, 0x80110220u + 0x23E4);
        if (state.mode == 1 && square > 0 && square < 10) {
            view.goal_label = "Clear";
            view.goal = std::to_string(square) + "\u00A0\u00D7\u00A0" + std::to_string(square);
        }
    }
    const bool single = single_stage && state.mode == 1 && state.chapter == single_stage->first;
    // A single stage cleared: the game plays its stage end, then builds the
    // next stage. The grid comes in over the stage end (after a moment of it),
    // so the next stage is never seen (the owner's report in Score Attack);
    // play is left as soon as the next stage begins.
    {
        static std::optional<std::chrono::steady_clock::time_point> cleared_at;
        const bool cleared_now = single && state.level == single_stage->second && state.progress >= 1.0f;
        if (!cleared_now) cleared_at.reset();
        else if (!cleared_at) cleared_at = std::chrono::steady_clock::now();
        else if (std::chrono::steady_clock::now() - *cleared_at > std::chrono::milliseconds(1800) && !grid_quit_at)
            grid_quit_at = std::chrono::steady_clock::now(), grid_quit_red = false;
    }
    // A whole episode in an attack mode is done when its last stage is
    // cleared (the core opened; the final episode has no next one to start).
    // Rescue's episode-end menu follows the last stage of a whole-episode
    // run (Story and Casual; the attack modes leave by themselves).
    final_stage_cleared = state.mode == 1 && !single_stage && !variant_competes(play_variant) && state.chapter == 10 && state.level >= 10 && state.progress >= 1.0f;
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
            // (No whole run timed, as when started part-way: the menus all the same.)
            if (!attack_episode_results)
                attack_episode_results = StageResults{state.chapter, 10, play_variant == PlayVariant::ScoreAttack, 0.0, 0.0,
                                                      std::string(state.mode == 9 ? "Lines " : "") + variant_title(play_variant) +
                                                          " \u00B7 Episode " +
                                                          std::to_string(state.chapter), true};
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
                single_stage_quit_at = std::chrono::steady_clock::now();  // at once: the grid already covers the next stage
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
            if (!single_stage && f->stage >= 10) {
                // The episode's points, against its best.
                const auto total = score_splits.view().total;
                auto& r = records[state.chapter];
                const double before = r.best_total;
                if (double(total) > before) r.best_total = double(total), profile_dirty = true, save_profile();
                attack_episode_results = StageResults{state.chapter, 10, true, double(total), before,
                                                      "Score Attack \u00B7 Episode " + std::to_string(state.chapter), true};
            }
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
        view.story_gain_live = !gains.empty() && gains.front().live;
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
                    single_stage_quit_at = std::chrono::steady_clock::now();  // at once: the grid already covers the next stage
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
        // then the next episode's card): the end menu bridge is told. (Not
        // in the attack modes, which go back to the menus with their results.)
        if (!variant_competes(play_variant) && !single_stage) {
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

// Research aid (docs/research/blockplanet-escape-mode.md): with
// BLOCKPLANET_BOARD_LOG=1, during play, each board cell that changes (layer,
// x, y, the block value before and after), each frame, and the cursor
// (+0x2B2A, +0x2B2E) and hearts (+0x2D7E) when they change; at most 200
// cells a pass. For finding the landing, the miss and gravity: a drop's
// cells, its layer, and the heart lost, frame by frame.
void poll_board_log() {
    static const bool on = std::getenv("BLOCKPLANET_BOARD_LOG") != nullptr;
    if (!on || guest_rdram == nullptr || guest::load16(guest_rdram, 0x800E121C) != -1) return;
    const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0);
    if (values < 0x80000000u || values >= 0x80800000u) return;
    static std::vector<std::int16_t> last;
    static const auto started = std::chrono::steady_clock::now();
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    constexpr int cells = 9 * 1024;
    const bool first = last.size() != std::size_t(cells);
    if (first) {
        last.assign(std::size_t(cells), 0);
        // The values in each layer, counted, once (what a stage is built from).
        for (int layer = 0; layer < 3; ++layer) {
            std::map<int, int> counts;
            for (int c = 0; c < 1024; ++c) ++counts[guest::load16(guest_rdram, values + 2u * std::uint32_t(layer * 1024 + c))];
            std::string text;
            for (const auto& [v, n] : counts) text += (text.empty() ? "" : ",") + std::string("\"") + std::to_string(v) + "\":" + std::to_string(n);
            std::fprintf(stdout, "{\"event\":\"board_counts\",\"layer\":%d,\"counts\":{%s}}\n", layer, text.c_str());
        }
    }
    int logged = 0;
    for (int i = 0; i < cells; ++i) {
        const std::int16_t v = guest::load16(guest_rdram, values + 2u * std::uint32_t(i));
        if (!first && v != last[std::size_t(i)] && logged < 200) {
            ++logged;
            std::fprintf(stdout, "{\"event\":\"board\",\"ms\":%lld,\"layer\":%d,\"x\":%d,\"y\":%d,\"from\":%d,\"to\":%d}\n", ms,
                         i / 1024, i % 32, (i % 1024) / 32, last[std::size_t(i)], v);
        }
        last[std::size_t(i)] = v;
    }
    constexpr std::uint32_t p1 = 0x80110220u;
    static int cursor_x = -1, cursor_y = -1, hearts = -99;
    const int cx = guest::load16(guest_rdram, p1 + 0x2B2A), cy = guest::load16(guest_rdram, p1 + 0x2B2E);
    const int h = guest::load16(guest_rdram, p1 + 0x2D7E);
    if (cx != cursor_x || cy != cursor_y || h != hearts) {
        cursor_x = cx, cursor_y = cy, hearts = h;
        std::fprintf(stdout, "{\"event\":\"board_player\",\"ms\":%lld,\"cursor_x\":%d,\"cursor_y\":%d,\"hearts\":%d}\n", ms, cx, cy, h);
    }
    if (logged || first) std::fflush(stdout);
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

extern "C" void blockplanet_sphere_play_log(std::uint8_t* rdram);  // src/graphics/sphere_build_research.cpp

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
    poll_board_log();
    poll_pokes();
    poll_type47_experiment();
    if (game_option_applier) game_option_applier->apply(const_cast<std::uint8_t*>(guest_rdram));
    tetrisphere::minefield_sphere::poll(const_cast<std::uint8_t*>(guest_rdram));
    research_hole(const_cast<std::uint8_t*>(guest_rdram));
    blockplanet_sphere_play_log(const_cast<std::uint8_t*>(guest_rdram));
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
        {"-1CONNECT-0", "Connected Pictures"}, {"-1TOWER-0", "Tower"}, {"-1CRYSTAL -1TOWER-0", "Glass Tower"}};
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
    case 11: return "Glass Tower: uncover the picture under the glass tower, without breaking the glass.";
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
    if (guest::load16(guest_rdram, 0x800E1228) >= 0 || bp_paused.load()) return 0;  // paused
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
    if ((!texts.empty() || box || (drop_lesson_phase >= 1 && drop_lesson_phase <= 6) || slide_lesson_shown()) && frame) {
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
                // As symbols (the owner, 2026-11-20): the arrow keys' cap on a
                // keyboard, the left stick or the d-pad on a controller.
                text.replace(at, 6, family == ControllerFamily::Keyboard ? "[k:Arrows]" : "[s:L] or [d:Pad]");
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
            // Wheels is no longer drawn (the owner, 2026-11-20: more room for
            // the instructions): the panel fills the right of the picture,
            // from the sphere's top (line 46) to the bottom of the HUD's row
            // (line 232), from column 184 (clear of the sphere, which reaches
            // about column 164, further as the lesson's camera moves) to 316;
            // no tail, nobody speaking.
            static const bool keep_wheels = std::getenv("BLOCKPLANET_KEEP_WHEELS") != nullptr;
            if (!keep_wheels && box->kind != 1) {
                panel.right = 1.0f - fx(316.0f);
                panel.w = fx(316.0f) - fx(184.0f);
                panel.top = fy(46.0f);
                panel.height = fy(232.0f) - fy(46.0f);
                panel.tail = -1.0f;
            }
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
        // The Drop lesson's text (BlockPlanet's own lesson): Training's panel,
        // right of the sphere (moved left, as Training's), from its top to the
        // HUD row's bottom, a little into a wide screen's margin: the lesson's
        // name, what it teaches, then what is happening now.
        if (!box && ((drop_lesson_phase >= 1 && drop_lesson_phase <= 6) || slide_lesson_shown())) {
            const auto fx = [&](float x) { return (area_x + x / 320.0f * area_w) / frame->window_w; };
            const auto fy = [&](float y) { return (frame->y + y / 240.0f * frame->h) / frame->window_h; };
            const float left = fx(184.0f), right = std::min(0.98f, fx(330.0f));
            blockplanet::ui::HudView::Panel panel;
            panel.right = 1.0f - right;
            panel.w = right - left;
            panel.top = fy(46.0f);  // growing down to fit its text (the owner: the box's bottom brought up)
            panel.tail = -1.0f;
            constexpr const char* yellow = "#e8b818ff";
            constexpr const char* orange = "#f5821fff";
            using Block = blockplanet::ui::HudView::Panel::Block;
            const auto heading = [&](const std::string& t) { panel.blocks.push_back(Block{true, {{t, yellow}}}); };
            const auto para = [&](const std::string& t) { panel.blocks.push_back(Block{false, {{t, orange}}}); };
            if (slide_lesson_phase >= 1) {
                // The owner's script (2026-11-20): "Slide and Drop", three lines
                // added one at a time with A; then "How to Slide", three more,
                // then the choice. The demonstration and the player's turn keep
                // the last instruction with what is happening now.
                const int ph = slide_lesson_phase;
                // The movement control in lower case ("the arrow keys", "the
                // stick or d-pad"), button symbols kept as they are.
                std::string move = buttons("%MOVE%");
                for (std::size_t c = 0, depth = 0; c < move.size(); ++c) {
                    if (move[c] == '[') ++depth;
                    else if (move[c] == ']' && depth) --depth;
                    else if (!depth) move[c] = char(std::tolower(static_cast<unsigned char>(move[c])));
                }
                const auto bullets = [&](const std::vector<std::string>& lines) {
                    Block block{false, {}};
                    for (std::size_t i = 0; i < lines.size(); ++i) {
                        if (i) block.runs.push_back({"\n", orange});
                        block.runs.push_back({"\u2022 " + lines[i], orange});
                    }
                    panel.blocks.push_back(block);
                };
                const int meter_now = guest_rdram ? int(guest::load16(guest_rdram, pc_ctx + 0x2AEC)) : 0;
                const int meter_full = guest_rdram ? int(guest::load16(guest_rdram, pc_ctx + 0x2AEE)) : 14;
                if (ph >= 40) {
                    // Crystals and Mystery Blocks (2026-11-21, the owner's script).
                    const std::string show = rubble_watched ? " Show me again" : " Show me";
                    const std::string choice = buttons("%A%") + " Try it\u00A0\u00A0\u00A0\u00A0\u00A0\u00A0" + buttons("%B%") + show;
                    const int shape_cells = int(std::max<std::size_t>(1, lesson_shape_cells(ph >= 44 ? rubble_shape : rubble_round_shape()).size()));
                    const int lines_left = rubble_cells_left() / shape_cells;
                    const char* pieces = rubble_round == 0 ? "yellow pieces" : rubble_round == 1 ? "blue squares" : "red Ts";
                    const bool held = guest_rdram && std::int8_t(guest::load8(guest_rdram, pc_ctx + 0x702)) >= 0;
                    if (ph == 40 && rubble_page <= 1) {
                        heading("Crystals and Mystery Blocks");
                        para("Crystals are single blocks, often found in patches between pieces. They never match anything.");
                        if (rubble_page >= 1) para("But you can turn crystals into pieces, to join groups up into bigger combos.");
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 40 && rubble_page <= 3) {
                        heading("How to Convert Crystals");
                        para("Move your shadow with " + move + " over crystals in the same shape as your piece.");
                        if (rubble_page >= 3) para("Press " + buttons("%B%") + " to turn the crystals into a piece like yours.");
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 40) {
                        heading("Convert the Crystals");
                        para("Three yellow pieces are split by two lines of crystals. Convert both lines, then drop on the yellow pieces to break them all in one combo.");
                        panel.hint = choice;
                    } else if (ph == 44 && rubble_page <= 1) {
                        heading("Mystery Blocks");
                        para("Glowing mystery blocks can be found throughout the sphere.");
                        if (rubble_page >= 1) para("Releasing these gives you an instant power-up.");
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 44 && rubble_page <= 4) {
                        heading("Releasing a Power-Up");
                        para("To release the power-up you can either:");
                        if (rubble_page >= 3) para(buttons("%A%") + " Clear blocks next to the mystery block.");
                        if (rubble_page >= 4) para(buttons("%B%") + " Slide a piece through the crystals and the mystery block.");
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 44) {
                        heading("Find the Power-Up");
                        para("A mystery block is buried among the crystals. Decide how to release the power-up:");
                        para("Convert the crystals with " + buttons("%B%") + " and clear them with " + buttons("%A%") + ".");
                        para("OR");
                        para("Slide the yellow piece through the crystals and the mystery block with " + buttons("%B%") + ".");
                        panel.hint = choice;
                    } else if (ph == 47) {
                        heading("Well Done");
                        para("You found a power-up!");
                        if (rubble_end_page >= 1) para("In the next lesson you'll find out about the different types of power-ups, and how to use them.");
                        panel.hint = buttons("%A%") + (rubble_end_page >= 1 ? " Done" : " Next");
                    } else if (ph >= 45) {
                        const bool watching = ph == 45;
                        heading(watching ? "Watch" : "Your Turn");
                        para("Release the power-up: convert the crystals with " + buttons("%B%") + " and clear them with " + buttons("%A%") +
                             ", or slide the yellow piece through with " + buttons("%B%") + ".");
                        if (held) para("A power-up! It's in your Magic box.");
                        else if (!rubble_note.empty() && !watching) para(buttons(rubble_note));
                        if (watching) panel.hint = "Watching\u2026";
                    } else {
                        const bool watching = ph == 41 || (ph == 43 && rubble_demo_running);
                        heading(watching ? "Watch" : "Your Turn");
                        para("Press " + buttons("%B%") + " over each line of crystals to convert it. Then drop on the " + pieces + ".");
                        if (ph == 43) para("Combo!");
                        else para(lines_left <= 0 ? "All the crystals are converted: now drop!" : "Crystal lines left: " + std::to_string(lines_left));
                        if (!rubble_note.empty() && ph == 42) para(rubble_note);
                        if (watching) panel.hint = "Watching\u2026";
                    }
                } else if (ph >= 30) {
                    // Combos and Magic (the owner's script, 2026-11-21).
                    if (ph == 30 && combos_page <= 1) {
                        heading("Combos and Magic");
                        para("Break more than three pieces at once and you've made a combo: the more you break together, the more rewarding it is.");
                        if (combos_page >= 1) bullets({"Fill your magic meter faster", "Clear the sphere faster", "Score more points"});
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 30 && combos_page == 2) {
                        heading("Earning Power-Ups");
                        bullets({"Combos of 6 or more top up your magic meter: fill it up for a power-up.",
                                 "Break 20 or more at once for an instant power-up."});
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 30 && combos_page == 3) {
                        heading("Combos and Magic");
                        para("In this lesson you'll top up your magic meter with combos and unlock a power-up.");
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 30) {
                        heading("Top Up Your Meter");
                        para("Drop your piece on the stack of the same shape: the whole stack breaks, six pieces in all. "
                             "Each combo of 6 or more tops up your magic meter.");
                        para("Your meter is nearly full: three combos will fill it.");
                        panel.hint = buttons("%A%") + " Try it\u00A0\u00A0\u00A0\u00A0\u00A0\u00A0" + buttons("%B%") +
                                     (combos_watched ? " Show me again" : " Show me");
                    } else if (ph == 34) {
                        heading("Well Done");
                        para("Congratulations, you've unlocked a power-up.");
                        if (combos_end_page >= 1) para("Get a combo of 20+ to unlock one instantly!");
                        panel.hint = buttons("%A%") + (combos_end_page >= 1 ? " Done" : " Next");
                    } else {
                        heading(ph == 31 || (ph == 33 && combos_demo_running) ? "Watch" : "Your Turn");
                        para("Drop on the matching stack: six break, and your magic meter tops up.");
                        // (The game empties the meter as it gives the power-up.)
                        if (meter_now == 0 && combos_step >= 2 && ph == 33) para("Magic meter full! A power-up is unlocked.");
                        else para("Magic meter: " + std::to_string(meter_now) + " of " + std::to_string(meter_full));
                        if (!combos_note.empty() && ph == 32) para(combos_note);
                        if (ph == 31 || (ph == 33 && combos_demo_running)) panel.hint = "Watching\u2026";
                    }
                } else if (ph >= 20) {
                    // Hearts and the Drop Timer (2026-11-20): why, then how,
                    // a line at a time, then try it or show me.
                    if (ph == 20 && hearts_page <= 1) {
                        heading("Hearts and the Drop Timer");
                        para("You start each stage with three hearts.");
                        if (hearts_page >= 1)
                            para("Dropping your piece where it doesn't make a match costs a heart. Lose them all and the game is over.");
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 20 && hearts_page == 2) {
                        heading("Hearts and the Drop Timer");
                        para("Your piece won't wait for ever. The bar under your hearts is the drop timer: when it runs out, your piece "
                             "drops by itself, wherever your shadow is.");
                        panel.hint = buttons("%A%") + " Next";
                    } else if (ph == 20) {
                        heading("How to Keep Your Hearts");
                        para("Watch the bar. Line your shadow up with a pair and drop before it runs out.");
                        panel.hint = buttons("%A%") + " Try it\u00A0\u00A0\u00A0\u00A0\u00A0\u00A0" + buttons("%B%") +
                                     (hearts_watched ? " Show me again" : " Show me");
                    } else if (ph == 21) {
                        heading("Watch");
                        if (hearts_demo_part <= 1)
                            para(hearts_demo_part == 0 ? "The shadow waits on open floor while the bar runs down."
                                                       : "The bar ran out, so the piece dropped by itself. No match there, so a heart was lost.");
                        else if (hearts_demo_part == 2) para("This time the shadow goes to the pair, and the piece is dropped in time.");
                        else para("All three break, and no heart is lost.");
                        panel.hint = "Watching\u2026";
                    } else if (ph == 22 || ph == 23) {
                        heading("Your Turn");
                        para("Make three good drops before the bar runs out.");
                        para("Good drops: " + std::to_string(hearts_good) + " of " + std::to_string(hearts_goal));
                        if (ph == 23 && !hearts_note.empty()) para(hearts_note);
                    } else {
                        heading("Well Done");
                        para("Three good drops, in time.");
                    }
                } else if (ph >= 10) {
                    // The drop stage (the owner, 2026-11-20): a new page, just how.
                    heading("How to Drop");
                    if (ph == 10) {
                        para("If your shadow isn't lined up with one of the pair, move it with " + move + " until it is.");
                        if (slide_drop_page >= 1) para("Then press " + buttons("%A%") + " to drop your piece. All three break.");
                        panel.hint = slide_drop_page < 1 ? buttons("%A%") + " Next"
                                                         : buttons("%A%") + " Try it\u00A0\u00A0\u00A0\u00A0\u00A0\u00A0" + buttons("%B%") + (slide_drop_watched ? " Show me again" : " Show me");
                    } else {
                        para("Line your shadow up with one of the pair, then press " + buttons("%A%") + ".");
                        if (ph == 11 && slide_drop_dropped) heading("Watch"), para("All three break.");
                        else if (ph == 11) heading("Watch"), para("The shadow lines up with a square, then " + buttons("%A%") + " drops the piece.");
                        else if (ph == 12) heading("Your turn"), para("Drop your piece onto the pair.");
                        else if (ph == 13) heading("Not quite"), para("No match there, so the piece stacked up. Line your shadow up with one of the pair first.");
                        else heading("Well done"), para("All three broke.");
                        if (ph == 11) panel.hint = "Watching\u2026";
                    }
                } else
                if (ph == 1 && slide_page <= 2) {
                    heading("Slide and Drop");
                    // A page break before the third line (the owner: it ran off the box).
                    if (slide_page <= 1) para("Ordinarily, you'll be required to clear pieces of the sphere to reach the core or an objective.");
                    if (slide_page == 1)
                        para("This is done by having at least two pieces of the same shape together and dropping a third on top of or next to "
                             "those pieces.");
                    if (slide_page == 2) para("In this lesson you will learn to slide pieces together to make the minimum requirement for a drop.");
                    panel.hint = buttons("%A%") + " Next";
                } else if (ph == 1) {
                    heading("How to Slide");
                    para("Move your shadow with " + move + " until it's over the top of a piece of the same shape.");
                    if (slide_page >= 4) para("Notice the shadow change when you are over a valid piece.");
                    if (slide_page >= 5) para("Hold " + buttons("%B%") + " and push " + move + " to slide one piece in line with another.");
                    panel.hint = slide_page < slide_last_page ? buttons("%A%") + " Next"
                                                              : buttons("%A%") + " Try it\u00A0\u00A0\u00A0\u00A0\u00A0\u00A0" + buttons("%B%") + (slide_watched ? " Show me again" : " Show me");
                } else {
                    heading("How to Slide");
                    para("Hold " + buttons("%B%") + " and push " + move + " to slide one piece in line with another.");
                    if (ph == 2 && slide_demo_step >= slide_moves) heading("Watch"), para("The two squares are together.");
                    else if (ph == 2) heading("Watch"), para(buttons("%B%") + " grabs a square and slides it across.");
                    else if (ph == 4) heading("Your turn"), para("Slide one square across until it touches the other. Dropping is off for now.");
                    else heading("Well done"), para("The two squares are together.");
                    if (ph == 2) panel.hint = "Watching\u2026";
                }
                view->card_panel = panel;
            } else {
            heading("Drop");
            para("Your shadow shows where your piece will land. Move it next to two matching pieces and drop: all three break.");
            std::string move = buttons("%MOVE%");  // a d-pad symbol, or words for the keyboard ("the arrow keys")
            if (move.find('[') == std::string::npos)
                for (auto& ch : move) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            const int ph = drop_lesson_phase;
            if (ph == 1 && !drop_lesson_watched) heading("Try it"), para("A matching pair is waiting nearby. Move your shadow next to it and drop your piece.");
            else if (ph == 1) heading("Your turn"), para("Now you try the same.");
            else if (ph == 2) heading("Watch"), para("The shadow moves next to the pair, then " + buttons("%A%") + " drops the piece.");
            else if (ph == 3) heading("Watch"), para("All three break.");
            else if (ph == 4) heading("Your turn"), para("Move your shadow next to the pair with " + move + ", then press " + buttons("%A%") + ".");
            else if (ph == 5) heading("Not quite"), para("No match there, so the piece stacked up. Line your shadow up with the pair first.");
            else heading("Well done"), para("All three broke.");
            if (ph == 2 || ph == 3) panel.hint = "Watching\u2026";
            if (ph == 1) panel.hint = buttons("%A%") + " Try it\u00A0\u00A0\u00A0\u00A0\u00A0\u00A0" + buttons("%B%") + (drop_lesson_watched ? " Watch again" : " Watch");
            view->card_panel = panel;
            }
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
// For placing the HUD: only the drop timer's zoom counts (the owner,
// 2026-11-19: the panels moved out and back during combos, as blocks cleared
// and the camera moved), so the panels move aside only while the timer pulls
// the view in, and the stage's own depth stands for the sphere's size.
float hud_sphere_radius(const tetrisphere::StoryHudState& state) {
    const int layers = state.layers > 0 ? state.layers : state.top_layer;
    const float distance = state.drop_zoom ? std::min(state.camera_distance, 2424.0f) : 2424.0f;
    return 1584.0f * (1.0f / 3.0f + 0.0875f * float(layers)) / distance;
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
bool developer_frozen_start = false;  // --developer-frozen: open the inspector on the first held frame
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
        // Bit 17: the D-pad's directions on the stick too (the game moves
        // the cursor by the stick; the Drop lesson's demonstration).
        if (port == 0 && (injected & 0x20000u)) {
            if (injected & 0x0200) *x = -1.0f;
            if (injected & 0x0100) *x = 1.0f;
            if (injected & 0x0800) *y = 1.0f;
            if (injected & 0x0400) *y = -1.0f;
        }
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
    auto controller = cycle.controllers[port];
    // The free camera on the sticks (frame_step.h): the game's directions from
    // the D-pad only, and the triggers (the camera's down and up) not the game's.
    if (port == 0 && tetrisphere::frame_step::camera_has_sticks()) {
        controller.sample.left_x = controller.sample.left_y = 0;
        controller.sample.trigger_left = controller.sample.trigger_right = 0;
    }
    const auto keys = cycle.keys;
    if (port == 1) cycle.valid = false;
    if (port == 1 && !controller.connected) return false;
    const auto mapped = map_port_input(port, controller, keys,
                                       controller_bindings(), magic_control());
    const std::uint16_t mouse_bits = port == 0 ? mouse_play.next_frame() : 0;
    if (minefield_frozen.load() || game_over_frozen.load()) {*buttons=0;*x=0;*y=0;return true;}
    *buttons = port == 0 ? tetrisphere::minefield_sphere::filter_input(mapped.buttons | mouse_bits) : mapped.buttons;
    if (port == 0 && lesson_no_drop.load()) *buttons = static_cast<std::uint16_t>(*buttons & ~0x8000u);  // the Slide lesson: no dropping
    if (port == 0 && lesson_no_slide.load()) *buttons = static_cast<std::uint16_t>(*buttons & ~0x4000u);  // Combos and Magic: no sliding
    note_start(mapped.buttons);
    if (own_pause_mask.load() && (*buttons & 0x1000u)) {  // Start in play: BlockPlanet's pause, not the game's
        *buttons = static_cast<std::uint16_t>(*buttons & ~0x1000u);
        own_pause_start.store(true);
    }
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
    tetrisphere::frame_step::set_enabled(run_options.developer);
    if (run_options.developer_frozen) tetrisphere::frame_step::set_frozen(true);
    developer_frozen_start = run_options.developer_frozen;
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

// The board's bricks (layers 1 to 8, walls left out): a magic lesson checks
// that the magic broke some.
int count_bricks() {
    if (guest_rdram == nullptr) return 0;
    const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u);
    int n = 0;
    for (int i = 1024; i < 9 * 1024; ++i) {
        const std::int16_t v = std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(i)));
        n += v > 0 && v != 423;
    }
    return n;
}

// Training's completed marks: the game's lessons (chapter 0 of a mode) and
// BlockPlanet's magic lessons (progress "magic_lessons": the magics done).
bool training_done(const char* mode) {
    const auto found = profile_data.progress.chapters_completed.find(mode);
    return profile_save && found != profile_data.progress.chapters_completed.end() && found->second.count(0) != 0;
}
std::set<int> magic_lessons_done() {
    const auto found = profile_data.progress.chapters_completed.find("magic_lessons");
    return found == profile_data.progress.chapters_completed.end() ? std::set<int>{} : found->second;
}
// BlockPlanet's own lessons done (progress "lessons": keys 100+, Drop 100).
std::set<int> new_lessons_done() {
    const auto found = profile_data.progress.chapters_completed.find("lessons");
    return found == profile_data.progress.chapters_completed.end() ? std::set<int>{} : found->second;
}

// The combo lab's board (research_lab:combo), also the Drop lesson's: layer 1
// all inert walls but for a hole of the piece in hand's shape at (cx, cy), down
// to the core, with two bricks of the same shape beside it at fitting entries
// fi and fj (signs sx, sy), so the piece dropped there breaks all three.
struct PairBoard { int index = 0, v0 = 0; int layout[9] = {}; std::vector<std::pair<int, int>> fit; };
// pit != 0: as the original Training's board (read in play, 2026-11-20), a
// square pit pit x pit cut through the wall round (px, py), its floor the core's
// plain white tile (87), so the pair sits on a blank page; 0: just the hole.
PairBoard build_pair_board(int cx, int cy, int fi, int fj, int sx, int sy, int pit = 0, int px = 0, int py = 0) {
    auto* rd = const_cast<std::uint8_t*>(guest_rdram);
    // The walls smooth, as Training's own (the owner, 2026-11-20): every face
    // cut from the sheet's flat first tile, so the cells join into one surface
    // (as Minefield's wall); the table in free upper memory.
    constexpr std::uint32_t flat_wall = 0x807F8600u;
    {
        const std::int16_t flat[8] = {1, 14, 14, 14, 1, 1, 14, 1};
        for (int i = 0; i < 48; ++i) guest::store16(rd, flat_wall + 2u * std::uint32_t(i), std::uint16_t(flat[i % 8]));
    }
    const std::uint32_t pc = 0x80110220u;
    // The piece about to be dropped: the queue's first entry (as the
    // cursor's shadow shows; +0x2386 is the piece taken last).
    const int index = std::int16_t(guest::load16(rd, pc + 0x2374));
    const std::uint32_t rec = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(index));
    const std::uint32_t values = guest::load32(rd, 0x800E44D0u), masks = guest::load32(rd, 0x800E44D4u),
                        entries = guest::load32(rd, 0x800E44D8u), columns = guest::load32(rd, 0x800E44DCu);
    const std::int16_t v0 = std::int16_t(guest::load16(rd, rec));
    int layout[9];
    for (int k = 0; k < 9; ++k) layout[k] = std::int16_t(guest::load16(rd, rec + 0xA + 2u * std::uint32_t(k)));
    const int fits = std::int16_t(guest::load16(rd, rec + 0x74A));
    std::vector<std::pair<int, int>> fit;
    for (int k = 0; k < fits && k < 14; ++k)
        fit.push_back({std::int8_t(guest::load8(rd, rec + 0x74C + 2u * std::uint32_t(k))),
                       std::int8_t(guest::load8(rd, rec + 0x74D + 2u * std::uint32_t(k)))});
    const auto cell = [](int x, int y) { return ((y % 32 + 32) % 32) * 32 + ((x % 32 + 32) % 32); };
    std::vector<int> filled(1024, 1);  // layer 1: 1 wall, 0 empty, 2 a target brick
    for (int c = 0; c < 1024; ++c) {
        guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 423);
        guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), flat_wall);
        guest::store8(rd, columns + std::uint32_t(c), 0x11);
        if (pit != 0) guest::store8(rd, masks + std::uint32_t(c), 0);  // the core, covered
        for (int layer = 2; layer < 9; ++layer) {
            guest::store16(rd, values + 2u * std::uint32_t(layer * 1024 + c), 0);
            guest::store8(rd, masks + std::uint32_t(layer * 1024 + c), 0);
        }
    }
    const auto place = [&](int ox, int oy, bool hole) {
        for (int x = 0; x < 3; ++x)
            for (int y = 0; y < 3; ++y) {
                const int n = layout[x * 3 + y];
                if (n <= 0) continue;
                const int c = cell(ox + x - 1, oy + y - 1);
                if (hole) {
                    filled[std::size_t(c)] = 0;
                    guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 0);
                    guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), 0);
                    guest::store8(rd, masks + std::uint32_t(c), 80);  // the core, open
                    guest::store8(rd, columns + std::uint32_t(c), 0x00);
                    if (pit != 0) guest::store16(rd, values + 2u * std::uint32_t(c), 87);
                } else {
                    filled[std::size_t(c)] = 2;
                    guest::store8(rd, masks + std::uint32_t(c), 0);
                    guest::store8(rd, columns + std::uint32_t(c), 0x11);
                    guest::store16(rd, values + 2u * std::uint32_t(1024 + c), std::int16_t(v0 + n - 1));
                    guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), rec + 0x78u + 96u * std::uint32_t(n - 1));
                }
            }
    };
    // fi < 0: the two fitting places that keep the three pieces most compact
    // (the pair beside the hole, not end to end), and pit < 0: a pit just
    // round them and (px, py), one cell to spare (the owner: a small space).
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0, pit_w = pit, pit_h = pit, pit_x0 = px - pit / 2, pit_y0 = py - pit / 2;
    {
        const auto cells_at = [&](int ox, int oy) {
            std::vector<std::pair<int, int>> out;
            for (int x = 0; x < 3; ++x)
                for (int y = 0; y < 3; ++y)
                    if (layout[x * 3 + y] > 0) out.push_back({ox + x - 1, oy + y - 1});
            return out;
        };
        if (fi < 0) {
            int best = 1 << 30;
            for (int i = 0; i < int(fit.size()); ++i)
                for (int j = i + 1; j < int(fit.size()); ++j) {
                    auto all = cells_at(cx, cy);
                    const auto a = cells_at(cx + fit[std::size_t(i)].first, cy + fit[std::size_t(i)].second);
                    const auto b = cells_at(cx + fit[std::size_t(j)].first, cy + fit[std::size_t(j)].second);
                    bool clash = false;
                    for (const auto& q : b)
                        for (const auto& r : a) clash |= q == r;
                    if (clash) continue;
                    all.insert(all.end(), a.begin(), a.end());
                    all.insert(all.end(), b.begin(), b.end());
                    int x0 = 99, y0 = 99, x1 = -99, y1 = -99;
                    for (const auto& [x, y] : all) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                    const int size = std::max(x1 - x0, y1 - y0) * 100 + (x1 - x0) * (y1 - y0);
                    if (size < best) best = size, fi = i, fj = j;
                }
            sx = sy = 1;
            if (fi < 0) fi = 0, fj = 1;
        }
        auto all = cells_at(cx, cy);
        for (const int k : {fi, fj})
            if (k >= 0 && k < int(fit.size())) {
                const auto a = cells_at(cx + sx * fit[std::size_t(k)].first, cy + sy * fit[std::size_t(k)].second);
                all.insert(all.end(), a.begin(), a.end());
            }
        all.push_back({px, py});
        bx0 = 99, by0 = 99, bx1 = -99, by1 = -99;
        for (const auto& [x, y] : all) bx0 = std::min(bx0, x), by0 = std::min(by0, y), bx1 = std::max(bx1, x), by1 = std::max(by1, y);
        if (pit < 0) pit_x0 = bx0 - 1, pit_y0 = by0 - 1, pit_w = bx1 - bx0 + 3, pit_h = by1 - by0 + 3;
    }
    if (pit != 0)
        for (int y = 0; y < pit_h; ++y)
            for (int x = 0; x < pit_w; ++x) {
                const int c = cell(pit_x0 + x, pit_y0 + y);
                filled[std::size_t(c)] = 0;
                guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 0);
                guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), 0);
                guest::store8(rd, masks + std::uint32_t(c), 80);
                guest::store8(rd, columns + std::uint32_t(c), 0x00);
                guest::store16(rd, values + 2u * std::uint32_t(c), 87);
            }
    place(cx, cy, true);
    for (const int k : {fi, fj})
        if (k >= 0 && k < int(fit.size())) place(cx + sx * fit[std::size_t(k)].first, cy + sy * fit[std::size_t(k)].second, false);
    if (pit != 0) place(cx + 16, cy, false);  // a spare on the far side, out of sight: the board never empty (see build_pit_board)
    // Face masks: the top, and each side facing an empty cell.
    for (int c = 0; c < 1024; ++c) {
        if (!filled[std::size_t(c)]) continue;
        const int x = c % 32, y = c / 32;
        std::uint8_t m = 16;
        if (!filled[std::size_t(cell(x + 1, y))]) m |= 8;
        if (!filled[std::size_t(cell(x - 1, y))]) m |= 4;
        if (!filled[std::size_t(cell(x, y + 1))]) m |= 2;
        if (!filled[std::size_t(cell(x, y - 1))]) m |= 1;
        guest::store8(rd, masks + std::uint32_t(1024 + c), m);
    }
    PairBoard out;
    out.index = index, out.v0 = v0, out.fit = fit;
    for (int k = 0; k < 9; ++k) out.layout[k] = layout[k];
    return out;
}

// Player 1's buttons now, mapped as the game would get them (N64 bits), read
// apart from what the game is given (a lesson's choice, while the game's input
// is held).
std::uint16_t player_buttons_now() {
    std::array<ControllerPortSnapshot, 2> controllers{};
    std::uint16_t keys = 0;
    {
        std::lock_guard lock(input_snapshot_mutex);
        controllers = input_snapshot;
        keys = keyboard_snapshot;
    }
    return map_port_input(0, controllers[0], keys, controller_bindings(), magic_control()).buttons;
}

// A lesson's pit board: walls everywhere (smooth, as Training's), a pit
// (x0, y0, w, h) cut down to a white floor (core tile 87, open), and pieces of
// the stage's shape `index` with their centres (cursor cells) at `origins`.
// Returns the shape's first value (its cells are v0 .. v0 + 8), -1 if none.
int build_pit_board(int x0, int y0, int w, int h, int index, const std::vector<std::pair<int, int>>& origins) {
    auto* rd = const_cast<std::uint8_t*>(guest_rdram);
    if (rd == nullptr || index < 0) return -1;
    constexpr std::uint32_t flat_wall = 0x807F8600u;  // as build_pair_board's
    {
        const std::int16_t flat[8] = {1, 14, 14, 14, 1, 1, 14, 1};
        for (int i = 0; i < 48; ++i) guest::store16(rd, flat_wall + 2u * std::uint32_t(i), std::uint16_t(flat[i % 8]));
    }
    const std::uint32_t rec = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(index));
    const std::uint32_t values = guest::load32(rd, 0x800E44D0u), masks = guest::load32(rd, 0x800E44D4u),
                        entries = guest::load32(rd, 0x800E44D8u), columns = guest::load32(rd, 0x800E44DCu);
    const std::int16_t v0 = std::int16_t(guest::load16(rd, rec));
    int layout[9];
    for (int k = 0; k < 9; ++k) layout[k] = std::int16_t(guest::load16(rd, rec + 0xA + 2u * std::uint32_t(k)));
    const auto cell = [](int x, int y) { return ((y % 32 + 32) % 32) * 32 + ((x % 32 + 32) % 32); };
    std::vector<int> filled(1024, 1);
    for (int c = 0; c < 1024; ++c) {
        guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 423);
        guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), flat_wall);
        guest::store8(rd, columns + std::uint32_t(c), 0x11);
        guest::store8(rd, masks + std::uint32_t(c), 0);
        for (int layer = 2; layer < 9; ++layer) {
            guest::store16(rd, values + 2u * std::uint32_t(layer * 1024 + c), 0);
            guest::store8(rd, masks + std::uint32_t(layer * 1024 + c), 0);
        }
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int c = cell(x0 + x, y0 + y);
            filled[std::size_t(c)] = 0;
            guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 0);
            guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), 0);
            guest::store8(rd, masks + std::uint32_t(c), 80);
            guest::store8(rd, columns + std::uint32_t(c), 0x00);
            guest::store16(rd, values + 2u * std::uint32_t(c), 87);
        }
    // A spare piece on the far side of the sphere, out of sight: with every
    // piece in the pit broken the board would be empty, which the game takes as
    // the stage cleared (the owner saw its transition start mid-lesson).
    auto all = origins;
    all.push_back({x0 + w / 2 + 16, y0 + h / 2});
    for (const auto& [ox, oy] : all)
        for (int x = 0; x < 3; ++x)
            for (int y = 0; y < 3; ++y) {
                const int n = layout[x * 3 + y];
                if (n <= 0) continue;
                const int c = cell(ox + x - 1, oy + y - 1);
                filled[std::size_t(c)] = 2;
                guest::store16(rd, values + 2u * std::uint32_t(1024 + c), std::int16_t(v0 + n - 1));
                guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), rec + 0x78u + 96u * std::uint32_t(n - 1));
                guest::store8(rd, masks + std::uint32_t(c), 0);
                guest::store8(rd, columns + std::uint32_t(c), 0x11);
            }
    for (int c = 0; c < 1024; ++c) {
        if (!filled[std::size_t(c)]) continue;
        const int x = c % 32, y = c / 32;
        std::uint8_t m = 16;
        if (!filled[std::size_t(cell(x + 1, y))]) m |= 8;
        if (!filled[std::size_t(cell(x - 1, y))]) m |= 4;
        if (!filled[std::size_t(cell(x, y + 1))]) m |= 2;
        if (!filled[std::size_t(cell(x, y - 1))]) m |= 1;
        guest::store8(rd, masks + std::uint32_t(1024 + c), m);
    }
    return v0;
}

// The stage's square piece (a 2 x 2 layout), as a queue index (0 or 1 in 1:1), or -1.
int square_piece_index() {
    if (guest_rdram == nullptr) return -1;
    for (int index = 0; index < 4; ++index) {
        const std::uint32_t rec = guest::load32(guest_rdram, 0x8014CBC8u + 4u * std::uint32_t(index));
        if ((rec >> 24) != 0x80u) continue;
        int count = 0, x0 = 9, y0 = 9, x1 = -1, y1 = -1;
        for (int k = 0; k < 9; ++k)
            if (std::int16_t(guest::load16(guest_rdram, rec + 0xA + 2u * std::uint32_t(k))) > 0)
                ++count, x0 = std::min(x0, k / 3), x1 = std::max(x1, k / 3), y0 = std::min(y0, k % 3), y1 = std::max(y1, k % 3);
        if (count == 4 && x1 - x0 == 1 && y1 - y0 == 1) return index;
    }
    return -1;
}

// The biggest group of the square's cells on layer 1 (cells touching by an
// edge), in squares: 6 when all are gathered.
int slide_biggest_group() {
    if (guest_rdram == nullptr || slide_square < 0) return 0;
    const std::uint32_t rec = guest::load32(guest_rdram, 0x8014CBC8u + 4u * std::uint32_t(slide_square));
    const int v0 = std::int16_t(guest::load16(guest_rdram, rec));
    const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u);
    std::vector<char> is(1024, 0), seen(1024, 0);
    for (int c = 0; c < 1024; ++c) {
        const int v = std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(1024 + c)));
        is[std::size_t(c)] = v >= v0 && v < v0 + 9;
    }
    int best = 0;
    for (int c = 0; c < 1024; ++c) {
        if (!is[std::size_t(c)] || seen[std::size_t(c)]) continue;
        int size = 0;
        std::vector<int> stack{c};
        seen[std::size_t(c)] = 1;
        while (!stack.empty()) {
            const int at = stack.back();
            stack.pop_back();
            ++size;
            const int x = at % 32, y = at / 32;
            const int next[4] = {y * 32 + (x + 1) % 32, y * 32 + (x + 31) % 32, ((y + 1) % 32) * 32 + x, ((y + 31) % 32) * 32 + x};
            for (const int n : next)
                if (is[std::size_t(n)] && !seen[std::size_t(n)]) seen[std::size_t(n)] = 1, stack.push_back(n);
        }
        best = std::max(best, size);
    }
    return best / 4;
}

// The board's layers (values, face masks, texture entries, columns) saved and
// put back: the drop stage's board, after the demonstration or a miss.
void lesson_board_save(std::vector<std::uint8_t>& out) {
    out.clear();
    if (guest_rdram == nullptr) return;
    const std::uint32_t parts[4][2] = {{guest::load32(guest_rdram, 0x800E44D0u), 9u * 1024u * 2u},
                                       {guest::load32(guest_rdram, 0x800E44D4u), 9u * 1024u},
                                       {guest::load32(guest_rdram, 0x800E44D8u), 9u * 1024u * 4u},
                                       {guest::load32(guest_rdram, 0x800E44DCu), 1024u}};
    for (const auto& [at, n] : parts)
        for (std::uint32_t i = 0; i < n; ++i) out.push_back(guest::load8(guest_rdram, at + i));
}
// Put back moved by (dx, dy) cells (the board wraps every 32), so it can be
// laid round the shadow where it is now: no jump, no swing of the camera.
void lesson_board_restore(const std::vector<std::uint8_t>& in, int dx = 0, int dy = 0) {
    if (guest_rdram == nullptr || in.empty()) return;
    auto* rd = const_cast<std::uint8_t*>(guest_rdram);
    struct Part { std::uint32_t at; std::uint32_t layers, bytes; };  // bytes per cell
    const Part parts[4] = {{guest::load32(rd, 0x800E44D0u), 9u, 2u}, {guest::load32(rd, 0x800E44D4u), 9u, 1u},
                           {guest::load32(rd, 0x800E44D8u), 9u, 4u}, {guest::load32(rd, 0x800E44DCu), 1u, 1u}};
    std::size_t k = 0;
    for (const auto& part : parts)
        for (std::uint32_t layer = 0; layer < part.layers; ++layer)
            for (std::uint32_t c = 0; c < 1024; ++c) {
                const int x = int(c % 32), y = int(c / 32);
                const std::uint32_t to = std::uint32_t(((y + dy) % 32 + 32) % 32 * 32 + ((x + dx) % 32 + 32) % 32);
                for (std::uint32_t b = 0; b < part.bytes && k < in.size(); ++b)
                    guest::store8(rd, part.at + (layer * 1024u + to) * part.bytes + b, in[k++]);
            }
}

// The nearest place (a cursor cell) where the shadow lines up with one of the
// squares on layer 1: each square's top-left cell (its cell number tells which)
// less the layout's offset. False if none.
bool slide_nearest_square(int cx, int cy, int out[2]) {
    if (guest_rdram == nullptr || slide_square < 0) return false;
    const std::uint32_t rec = guest::load32(guest_rdram, 0x8014CBC8u + 4u * std::uint32_t(slide_square));
    const int v0 = std::int16_t(guest::load16(guest_rdram, rec));
    int ax = 9, ay = 9;
    for (int k = 0; k < 9; ++k)
        if (std::int16_t(guest::load16(guest_rdram, rec + 0xA + 2u * std::uint32_t(k))) > 0) ax = std::min(ax, k / 3), ay = std::min(ay, k % 3);
    const int corner = std::int16_t(guest::load16(guest_rdram, rec + 0xA + 2u * std::uint32_t(ax * 3 + ay)));
    const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u);
    int best = 1 << 30;
    for (int c = 0; c < 1024; ++c) {
        if (std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(1024 + c))) != v0 + corner - 1) continue;
        // The cell's place nearest the cursor (the board wraps every 32).
        int ox = c % 32 - (ax - 1), oy = c / 32 - (ay - 1);
        ox += 32 * int(std::lround((cx - ox) / 32.0)), oy += 32 * int(std::lround((cy - oy) / 32.0));
        const int d = std::abs(ox - cx) + std::abs(oy - cy);
        if (d < best) best = d, out[0] = ox, out[1] = oy;
    }
    return best < (1 << 30);
}

// A place for the shadow on the pit's open floor, clear of the squares (no
// square cell under it or beside it), nearest the cursor: where the drop stage
// starts, so the player has to line the shadow up first (the owner).
bool slide_free_spot(int cx, int cy, int out[2], std::vector<std::pair<int, int>>* all = nullptr) {
    if (guest_rdram == nullptr || slide_square < 0) return false;
    const std::uint32_t rec = guest::load32(guest_rdram, 0x8014CBC8u + 4u * std::uint32_t(slide_square));
    int ax = 9, ay = 9;
    for (int k = 0; k < 9; ++k)
        if (std::int16_t(guest::load16(guest_rdram, rec + 0xA + 2u * std::uint32_t(k))) > 0) ax = std::min(ax, k / 3), ay = std::min(ay, k % 3);
    const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u), masks = guest::load32(guest_rdram, 0x800E44D4u);
    const auto at = [](int x, int y) { return ((y % 32 + 32) % 32) * 32 + ((x % 32 + 32) % 32); };
    const auto floor = [&](int x, int y) {  // open floor: nothing on layer 1, the core open
        const int c = at(x, y);
        return guest::load16(guest_rdram, values + 2u * std::uint32_t(1024 + c)) == 0 && guest::load8(guest_rdram, masks + std::uint32_t(c)) == 80;
    };
    const auto value = [&](int x, int y) { return int(std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(1024 + at(x, y))))); };
    int best = 1 << 30;
    for (int oy = cy - 8; oy <= cy + 8; ++oy)
        for (int ox = cx - 8; ox <= cx + 8; ++ox) {
            const int x0 = ox + ax - 1, y0 = oy + ay - 1;  // the shadow's 2 x 2 cells
            bool ok = true;
            for (int y = y0; y < y0 + 2 && ok; ++y)
                for (int x = x0; x < x0 + 2 && ok; ++x) ok = floor(x, y);
            for (int y = y0 - 1; y < y0 + 3 && ok; ++y)
                for (int x = x0 - 1; x < x0 + 3 && ok; ++x) ok = value(x, y) == 0 || value(x, y) == 423;
            if (!ok) continue;
            if (all) all->push_back({ox, oy});
            const int d = std::abs(ox - cx) + std::abs(oy - cy);
            if (d < best) best = d, out[0] = ox, out[1] = oy;
        }
    return best < (1 << 30);
}

// The hearts as at a stage's start (as Casual's refill does): three, shown full.
void lesson_refill_hearts(std::uint8_t* g) {
    guest::store16(g, pc_ctx + 0x2D7E, 3);
    guest::store16(g, pc_ctx + 0x30FC, 3);
    for (std::uint32_t i = 0; i < 3; ++i) guest::store8(g, pc_ctx + 0x2FFC + 0x20 * i, 0);
}

// A new pair for Hearts and the Drop Timer, built on the game's thread round
// the shadow: two squares side by side in an 8 x 6 pit, the shadow on open
// floor beside them at one of several places (a bit of variety).
void hearts_new_pair() {
    tetrisphere::minefield_sphere::queue_game_thread_job([](std::uint8_t* g) {
        static std::mt19937 pick(std::random_device{}());
        const int cx = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2E));
        // The gap the piece should land in, four or five cells from the shadow,
        // on the side towards the board's middle (column 16), so the play area
        // does not creep to the seam at 31 and 0; a row up or down for variety.
        int dx = std::uniform_int_distribution<int>(4, 5)(pick), dy = std::uniform_int_distribution<int>(-1, 1)(pick);
        if (const char* fixed = std::getenv("BLOCKPLANET_HEARTS_SPOT")) std::sscanf(fixed, "%d,%d", &dx, &dy);  // tests: a fixed gap
        // On the shadow's left on screen (higher columns; the owner: the pair on
        // the right sat at the sphere's edge by the text panel); the lesson
        // starts at column 10, so three drops reach about 22, clear of the seam;
        // past 20, the right instead.
        if (cx > 20) dx = -dx;
        for (int q = 0; q < 4; ++q) guest::store16(g, pc_ctx + 0x2374 + 2u * std::uint32_t(q), std::uint16_t(hearts_shape()));
        hearts_target[0] = cx + dx, hearts_target[1] = cy + dy;
        build_pair_board(hearts_target[0], hearts_target[1], -1, -1, 1, 1, -1, cx, cy);
        guest::store32(g, pc_ctx + 0x2408, 0);  // the timer from the start
    });
}

// Hearts' demonstration, its second part: the shadow steered onto a square,
// a pause, then A.
std::uint16_t hearts_demo_buttons() {
    if (slide_lesson_phase != 21 || hearts_demo_part != 2 || hearts_aim_pending || guest_rdram == nullptr || !hearts_demo_at) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *hearts_demo_at).count();
    if (ms < 600) return 0;
    const int slot = int((ms - 600) / 300), within = int((ms - 600) % 300);
    if (slot != hearts_slot) {
        hearts_slot = slot;
        const int dx = hearts_target[0] - (8 + std::int16_t(guest::load16(guest_rdram, pc_ctx + 0x2B2A)));
        const int dy = hearts_target[1] - (8 + std::int16_t(guest::load16(guest_rdram, pc_ctx + 0x2B2E)));
        if (dx != 0) hearts_held = dx > 0 ? 0x0200 : 0x0100;
        else if (dy != 0) hearts_held = dy > 0 ? 0x0800 : 0x0400;
        else if (!hearts_aimed) hearts_aimed = true, hearts_held = 0;
        else hearts_held = 0x8000;
    }
    if (hearts_held == 0x8000 && within >= 150) hearts_demo_part = 3, hearts_demo_at = std::chrono::steady_clock::now();
    return within < 150 ? hearts_held : 0;
}

// A compact group of `count` pieces of the shape table's entry `index`, each
// touching another (at the shape's own fitting places, record +0x74A/+0x74C),
// as cursor cells from (0, 0): each piece added where the group stays smallest.
std::vector<std::pair<int, int>> compact_group(int index, int count) {
    std::vector<std::pair<int, int>> group{{0, 0}};
    if (guest_rdram == nullptr) return group;
    const std::uint32_t rec = guest::load32(guest_rdram, 0x8014CBC8u + 4u * std::uint32_t(index));
    int layout[9];
    for (int k = 0; k < 9; ++k) layout[k] = std::int16_t(guest::load16(guest_rdram, rec + 0xA + 2u * std::uint32_t(k)));
    std::vector<std::pair<int, int>> fit;
    const int fits = std::int16_t(guest::load16(guest_rdram, rec + 0x74A));
    for (int k = 0; k < fits && k < 14; ++k)
        fit.push_back({std::int8_t(guest::load8(guest_rdram, rec + 0x74C + 2u * std::uint32_t(k))),
                       std::int8_t(guest::load8(guest_rdram, rec + 0x74D + 2u * std::uint32_t(k)))});
    const auto cells = [&](int ox, int oy) {
        std::vector<std::pair<int, int>> out;
        for (int x = 0; x < 3; ++x)
            for (int y = 0; y < 3; ++y)
                if (layout[x * 3 + y] > 0) out.push_back({ox + x - 1, oy + y - 1});
        return out;
    };
    std::set<std::pair<int, int>> taken;
    for (const auto& c : cells(0, 0)) taken.insert(c);
    while (int(group.size()) < count) {
        int best = 1 << 30;
        std::pair<int, int> pick{0, 0};
        for (const auto& [gx, gy] : group)
            for (const auto& [fx, fy] : fit)
                for (const int sgn : {1, -1}) {
                    const int ox = gx + sgn * fx, oy = gy + sgn * fy;
                    bool clash = false;
                    const auto mine = cells(ox, oy);
                    for (const auto& c : mine) clash |= taken.count(c) != 0;
                    if (clash) continue;
                    int x0 = ox, y0 = oy, x1 = ox, y1 = oy;
                    for (const auto& c : taken) x0 = std::min(x0, c.first), y0 = std::min(y0, c.second), x1 = std::max(x1, c.first), y1 = std::max(y1, c.second);
                    for (const auto& c : mine) x0 = std::min(x0, c.first), y0 = std::min(y0, c.second), x1 = std::max(x1, c.first), y1 = std::max(y1, c.second);
                    const int size = std::max(x1 - x0, y1 - y0) * 1000 + (x1 - x0 + 1) * (y1 - y0 + 1) * 10 + std::abs(ox) + std::abs(oy);
                    if (size < best) best = size, pick = {ox, oy};
                }
        if (best == (1 << 30)) break;
        group.push_back(pick);
        for (const auto& c : cells(pick.first, pick.second)) taken.insert(c);
    }
    return group;
}

// Stacks in a shallow pit (Combos and Magic, the owner's layout, 2026-11-21):
// walls one layer high round the pit (x0, y0, w, h), its floor the white
// core, and each stack a piece of one shape piled `height` high at its
// origin (so a drop on top makes a combo going down). A spare piece far
// round the sphere keeps the board from emptying.
void build_stack_pit(int x0, int y0, int w, int h, int height, const std::vector<std::pair<int, std::pair<int, int>>>& stacks,
                     const std::vector<LessonCell>& singles = {}) {
    auto* rd = const_cast<std::uint8_t*>(guest_rdram);
    if (rd == nullptr) return;
    constexpr std::uint32_t flat_wall = 0x807F8600u;
    {
        const std::int16_t flat[8] = {1, 14, 14, 14, 1, 1, 14, 1};
        for (int i = 0; i < 48; ++i) guest::store16(rd, flat_wall + 2u * std::uint32_t(i), std::uint16_t(flat[i % 8]));
    }
    const std::uint32_t values = guest::load32(rd, 0x800E44D0u), masks = guest::load32(rd, 0x800E44D4u),
                        entries = guest::load32(rd, 0x800E44D8u), columns = guest::load32(rd, 0x800E44DCu);
    const auto cell = [](int x, int y) { return ((y % 32 + 32) % 32) * 32 + ((x % 32 + 32) % 32); };
    std::vector<int> top(1024, 1);
    std::vector<std::array<std::int16_t, 9>> value(1024);
    std::vector<std::array<std::uint32_t, 9>> entry(1024);
    for (int c = 0; c < 1024; ++c) value[std::size_t(c)][1] = 423, entry[std::size_t(c)][1] = flat_wall;
    std::vector<char> in_pit(1024, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int c = cell(x0 + x, y0 + y);
            top[std::size_t(c)] = 0, value[std::size_t(c)][1] = 0, entry[std::size_t(c)][1] = 0, in_pit[std::size_t(c)] = 1;
        }
    auto all = stacks;
    all.push_back({stacks.empty() ? 0 : stacks.front().first, {x0 + w / 2 + 16, y0 + h / 2}});  // the spare
    for (std::size_t s2 = 0; s2 < all.size(); ++s2) {
        const int index = all[s2].first;
        const std::uint32_t rec = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(index));
        const std::int16_t v0 = std::int16_t(guest::load16(rd, rec));
        const bool spare = s2 + 1 == all.size();
        for (int q = 0; q < 9; ++q) {
            const int n = std::int16_t(guest::load16(rd, rec + 0xA + 2u * std::uint32_t(q)));
            if (n <= 0) continue;
            const int c = cell(all[s2].second.first + q / 3 - 1, all[s2].second.second + q % 3 - 1);
            const int from = spare ? 2 : 1, to = spare ? 2 : height;
            for (int layer = from; layer <= to; ++layer) {
                value[std::size_t(c)][std::size_t(layer)] = std::int16_t(v0 + n - 1);
                entry[std::size_t(c)][std::size_t(layer)] = rec + 0x78u + 96u * std::uint32_t(n - 1);
            }
            top[std::size_t(c)] = std::max(top[std::size_t(c)], to);
        }
    }
    for (const auto& one : singles) {
        const int c = cell(one.x, one.y);
        value[std::size_t(c)][1] = std::int16_t(one.value), entry[std::size_t(c)][1] = one.entry;
        top[std::size_t(c)] = std::max(top[std::size_t(c)], 1);
    }
    for (int c = 0; c < 1024; ++c) {
        const int x = c % 32, y = c / 32, t = top[std::size_t(c)];
        for (int layer = 1; layer < 9; ++layer) {
            guest::store16(rd, values + 2u * std::uint32_t(layer * 1024 + c), std::uint16_t(value[std::size_t(c)][std::size_t(layer)]));
            guest::store32(rd, entries + 4u * std::uint32_t(layer * 1024 + c), entry[std::size_t(c)][std::size_t(layer)]);
            std::uint8_t m = 0;
            if (value[std::size_t(c)][std::size_t(layer)] != 0) {
                if (layer == t) m |= 16;
                if (top[std::size_t(cell(x + 1, y))] < layer) m |= 8;
                if (top[std::size_t(cell(x - 1, y))] < layer) m |= 4;
                if (top[std::size_t(cell(x, y + 1))] < layer) m |= 2;
                if (top[std::size_t(cell(x, y - 1))] < layer) m |= 1;
            }
            guest::store8(rd, masks + std::uint32_t(layer * 1024 + c), m);
        }
        guest::store8(rd, columns + std::uint32_t(c), std::uint8_t((t << 4) | t));
        guest::store8(rd, masks + std::uint32_t(c), t == 0 ? 80 : 0);
        // The white floor under the whole pit, so it stays white as pieces and
        // rubble go (the stage's own core showed through where they had been).
        if (t == 0 || in_pit[std::size_t(c)]) guest::store16(rd, values + 2u * std::uint32_t(c), 87);
    }
}

// Rubble and Mystery Blocks' board, built on the game's thread round the
// anchor (rubble_anchor): part 1, a column of yellow bars and rubble lines
// across them (bar, rubble, bar, rubble, bar); part 2, a bar with three lines
// of rubble beyond it, a mystery block in the middle of the middle line. The
// pit runs past the column along the bars, where the shadow starts.
// Laid out from where the shadow is (the board moves, not the camera): the
// column three cells along the bars from it (the shadow beside its middle bar,
// on the floor; further and the column was half out of view), on the side away from the board's
// seam (cells 31 and 0).
void rubble_build(int part) {
    tetrisphere::minefield_sphere::queue_game_thread_job([part](std::uint8_t* g) {
        const int shape = part == 1 ? rubble_round_shape() : rubble_shape;
        const auto cells = lesson_shape_cells(shape);
        int x0 = 9, x1 = -9, y0 = 9, y1 = -9;
        for (const auto& [x, y] : cells) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
        // The column runs across the shape's long side (a lying bar: up the screen).
        const bool across_y = x1 - x0 >= y1 - y0;
        rubble_across[0] = across_y ? 0 : 1, rubble_across[1] = across_y ? 1 : 0;
        const int step = across_y ? y1 - y0 + 1 : x1 - x0 + 1, width = across_y ? x1 - x0 + 1 : y1 - y0 + 1;
        rubble_step_size = step;
        const int cx = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2E));
        rubble_side = (across_y ? cx : cy) + 8 > 29 ? -1 : 1;
        rubble_anchor[0] = cx + (across_y ? width * rubble_side : 0);
        rubble_anchor[1] = cy + (across_y ? 0 : width * rubble_side);
        const int ax = rubble_anchor[0], ay = rubble_anchor[1], px = rubble_across[0], py = rubble_across[1];
        std::vector<std::pair<int, std::pair<int, int>>> pieces;
        std::vector<LessonCell> singles;
        rubble_lines_at.clear(), rubble_pieces_at.clear();
        const auto line = [&](int ox, int oy, int mystery_at) {
            rubble_lines_at.push_back({ox, oy});
            for (std::size_t i = 0; i < cells.size(); ++i) {
                const bool m = int(i) == mystery_at;
                singles.push_back({ox + cells[i].first, oy + cells[i].second, m ? mystery_value : rubble_value, m ? mystery_entry : rubble_entry});
            }
        };
        const auto piece = [&](int ox, int oy) { pieces.push_back({shape, {ox, oy}}); rubble_pieces_at.push_back({ox, oy}); };
        if (part == 1) {
            const int groups = rubble_groups();
            for (int i = 0; i < groups; ++i) {
                const int k = i - (groups - 1) / 2;
                if (i % 2 == 0) piece(ax + px * k * step, ay + py * k * step);
                else line(ax + px * k * step, ay + py * k * step, -1);
            }
        } else {
            piece(ax - 2 * px * step, ay - 2 * py * step);
            for (int k = -1; k <= 1; ++k) line(ax + px * k * step, ay + py * k * step, k == 0 ? int(cells.size()) / 2 : -1);
        }
        // The pit: round everything and the shadow, a cell spare, at least 8 x 8.
        int bx0 = cx + x0, bx1 = cx + x1, by0 = cy + y0, by1 = cy + y1;
        for (const auto& pc : pieces)
            for (const auto& [x, y] : cells) bx0 = std::min(bx0, pc.second.first + x), bx1 = std::max(bx1, pc.second.first + x),
                                             by0 = std::min(by0, pc.second.second + y), by1 = std::max(by1, pc.second.second + y);
        for (const auto& one : singles) bx0 = std::min(bx0, one.x), bx1 = std::max(bx1, one.x), by0 = std::min(by0, one.y), by1 = std::max(by1, one.y);
        --bx0, ++bx1, --by0, ++by1;
        for (bool lo = true; bx1 - bx0 + 1 < 8; lo = !lo) (lo ? --bx0 : ++bx1);
        for (bool lo = true; by1 - by0 + 1 < 8; lo = !lo) (lo ? --by0 : ++by1);
        build_stack_pit(bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1, 1, pieces, singles);
        guest::store16(g, pc_ctx + 0x23E4, 16);  // no win from the open pit
        for (int q = 0; q < 4; ++q) guest::store16(g, pc_ctx + 0x2374 + 2u * std::uint32_t(q), std::uint16_t(shape));
        guest::store32(g, pc_ctx + 0x2408, 0);
    });
}

// The demonstration, one step at a time (one press each 300 ms): steer the
// shadow to the step's place, a pause, then its action: part 1, B on each
// rubble line, then A on the middle bar; part 2, B held on the bar and pushed
// through the rubble three times, then let go.
std::uint16_t rubble_demo_buttons() {
    if ((slide_lesson_phase != 41 && slide_lesson_phase != 45) || guest_rdram == nullptr || !rubble_at ||
        tetrisphere::minefield_sphere::game_thread_job_pending())
        return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *rubble_at).count();
    if (ms < 700) return 0;
    struct Step { int x, y, action; };  // 0: press B, 1: press A, 2: slide across
    const int px = rubble_across[0];
    std::vector<Step> steps;
    if (rubble_pieces_at.empty() || rubble_lines_at.empty()) return 0;
    if (slide_lesson_phase == 41) {
        for (const auto& [x, y] : rubble_lines_at) steps.push_back({x, y, 0});
        const auto& mid = rubble_pieces_at[rubble_pieces_at.size() / 2];
        steps.push_back({mid.first, mid.second, 1});
    } else if (rubble_demos % 2 == 1) {
        // Convert the crystals beside the piece, then drop on them: the combo
        // is next to the mystery block.
        steps = {{rubble_lines_at[0].first, rubble_lines_at[0].second, 0}, {rubble_lines_at[0].first, rubble_lines_at[0].second, 1}};
    } else {
        steps = {{rubble_pieces_at[0].first, rubble_pieces_at[0].second, 2}};
    }
    if (rubble_step >= int(steps.size())) return 0;
    const Step& st = steps[std::size_t(rubble_step)];
    const std::uint16_t push = px != 0 ? 0x0200 : 0x0800;  // +x is left, +y is up
    const int slot = int((ms - 700) / 300), within = int((ms - 700) % 300);
    if (slot != rubble_slot) {
        rubble_slot = slot;
        if (rubble_pushes > 0) {
            if (st.action == 2 && rubble_pushes <= 3) rubble_held = std::uint16_t(0x4000 | push), ++rubble_pushes;
            else if (st.action == 2 && rubble_pushes == 4) rubble_held = 0x4000, ++rubble_pushes;
            else rubble_held = 0, rubble_pushes = 0, rubble_aimed = false, ++rubble_step;
        } else {
            const int dx = st.x - (8 + std::int16_t(guest::load16(guest_rdram, pc_ctx + 0x2B2A)));
            const int dy = st.y - (8 + std::int16_t(guest::load16(guest_rdram, pc_ctx + 0x2B2E)));
            if (dx != 0) rubble_held = dx > 0 ? 0x0200 : 0x0100;
            else if (dy != 0) rubble_held = dy > 0 ? 0x0800 : 0x0400;
            else if (!rubble_aimed) rubble_aimed = true, rubble_held = 0;
            else rubble_held = st.action == 1 ? 0x8000 : 0x4000, rubble_pushes = 1;
        }
    }
    if (rubble_pushes > 0 && st.action == 2) return within < 150 ? rubble_held : std::uint16_t(rubble_held & 0x4000);
    return within < 150 ? rubble_held : 0;
}

// Combos and Magic's demonstration: the shadow steered onto a piece of the
// group (one press each 300 ms), a pause, then A.
std::uint16_t combos_demo_buttons() {
    if (slide_lesson_phase != 31 || combos_dropped || guest_rdram == nullptr || !combos_at) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *combos_at).count();
    if (ms < 700) return 0;
    const int slot = int((ms - 700) / 300), within = int((ms - 700) % 300);
    if (slot != combos_slot) {
        combos_slot = slot;
        const int dx = combos_target[0] - (8 + std::int16_t(guest::load16(guest_rdram, pc_ctx + 0x2B2A)));
        const int dy = combos_target[1] - (8 + std::int16_t(guest::load16(guest_rdram, pc_ctx + 0x2B2E)));
        if (dx != 0) combos_held = dx > 0 ? 0x0200 : 0x0100;
        else if (dy != 0) combos_held = dy > 0 ? 0x0800 : 0x0400;
        else if (!combos_aimed) combos_aimed = true, combos_held = 0;
        else combos_held = 0x8000;
    }
    if (combos_held == 0x8000 && within >= 150) combos_dropped = true, combos_at = std::chrono::steady_clock::now();
    return within < 150 ? combos_held : 0;
}

// Combos and Magic's board (the owner's layout, 2026-11-21): an 8 x 8 pit with
// the white floor, one wall high, and the stacks still to break, each a shape
// piled five high (five and the dropped piece: a combo of six, which tops up
// the meter), apart from each other in a triangle: the blue squares and the
// red Ts side by side, the yellow bars beyond them, the shadow below.
// Built on the game's thread; the queue set to the stacks' shapes in order.
void combos_build_step() {
    tetrisphere::minefield_sphere::queue_game_thread_job([](std::uint8_t* g) {
        // Each stack's place, by its shape's outline's top-left corner in the pit.
        // In the pit's upper half on screen (its higher rows), the shadow
        // starting below them: the stacks stand tall, and low in the view they
        // were half behind the HUD's row. (Higher columns are on screen left.)
        constexpr int corner[3][2] = {{5, 4}, {1, 4}, {3, 7}};
        const int x0 = combos_anchor[0] - 4, y0 = combos_anchor[1] - 1;
        std::vector<std::pair<int, std::pair<int, int>>> stacks;
        for (int k = std::clamp(combos_step, 0, 3); k < 3; ++k) {
            const std::uint32_t rec = guest::load32(g, 0x8014CBC8u + 4u * std::uint32_t(combos_shapes[k]));
            int ax = 9, ay = 9;
            for (int q = 0; q < 9; ++q)
                if (std::int16_t(guest::load16(g, rec + 0xA + 2u * std::uint32_t(q))) > 0) ax = std::min(ax, q / 3), ay = std::min(ay, q % 3);
            // A cell sits at origin + (x - 1, y - 1): the outline's corner at origin + (ax - 1, ay - 1).
            const int ox = x0 + corner[k][0] - (ax - 1), oy = y0 + corner[k][1] - (ay - 1);
            stacks.push_back({combos_shapes[k], {ox, oy}});
            if (k == combos_step) combos_target[0] = ox, combos_target[1] = oy;
        }
        build_stack_pit(x0, y0, 8, 8, 5, stacks);
        for (int q = 0; q < 4; ++q)
            guest::store16(g, pc_ctx + 0x2374 + 2u * std::uint32_t(q), std::uint16_t(combos_shapes[std::min(2, combos_step + q)]));
        guest::store32(g, pc_ctx + 0x2408, 0);
    });
}

// The drop stage's demonstration: the shadow steered onto a square (one press
// each 300 ms), a pause, then A.
std::uint16_t slide_drop_buttons() {
    if (slide_lesson_phase != 11 || guest_rdram == nullptr || !slide_lesson_at || slide_drop_dropped) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *slide_lesson_at).count();
    if (ms < 800) return 0;
    const int slot = int((ms - 800) / 300), within = int((ms - 800) % 300);
    if (slot != slide_drop_slot) {
        slide_drop_slot = slot;
        const std::uint32_t pc = 0x80110220u;
        const int dx = slide_drop_target[0] - (8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2A)));
        const int dy = slide_drop_target[1] - (8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2E)));
        if (dx != 0) slide_drop_held = dx > 0 ? 0x0200 : 0x0100;
        else if (dy != 0) slide_drop_held = dy > 0 ? 0x0800 : 0x0400;
        else if (!slide_drop_aimed) slide_drop_aimed = true, slide_drop_held = 0;
        else slide_drop_held = 0x8000;
    }
    if (slide_drop_held == 0x8000 && within >= 150) {
        slide_drop_dropped = true;
        slide_drop_dropped_at = std::chrono::steady_clock::now();
    }
    return within < 150 ? slide_drop_held : 0;
}

// The Slide lesson's demonstration: for each square in turn, the shadow
// steered over it (one D-pad press each 300 ms, by where the cursor is), B
// held, the shadow steered to the square's place in the block (dragging it),
// B let go. D-pad right moves the cursor -1 in x, down -1 in y.
std::uint16_t slide_lesson_buttons() {
    if (slide_lesson_phase != 2 || guest_rdram == nullptr || !slide_lesson_at || slide_demo_step >= slide_moves) return 0;
    const auto now = std::chrono::steady_clock::now();
    if (now - *slide_lesson_at < std::chrono::milliseconds(800)) return 0;
    if (!slide_demo_stage_at) slide_demo_stage_at = now;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - *slide_demo_stage_at).count();
    const int k = slide_order[slide_demo_step];
    const int* goal = slide_demo_stage <= 1 ? slide_starts[k] : slide_finals[k];
    const int gx = slide_origin[0] + goal[0], gy = slide_origin[1] + goal[1];
    const std::uint32_t pc = 0x80110220u;
    const int cx = 8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2E));
    const std::uint16_t b = slide_demo_stage == 1 || slide_demo_stage == 2 ? 0x4000 : 0;
    const auto next_stage = [&] { ++slide_demo_stage, slide_demo_stage_at = now, slide_demo_slot = -1, slide_demo_held = 0; };
    if (slide_demo_stage == 1) {  // B held a moment before moving
        if (ms > 350) next_stage();
        return b;
    }
    if (slide_demo_stage == 3 && slide_demo_slot != -2) {
        slide_demo_slot = -2;
        std::fprintf(stdout, "{\"event\":\"slide_demo_piece\",\"piece\":%d,\"cursor\":[%d,%d],\"goal\":[%d,%d],\"group\":%d}\n", k, cx, cy, gx, gy,
                     slide_biggest_group());
        std::fflush(stdout);
    }
    if (slide_demo_stage == 3) {  // let go, a pause, the next square
        if (ms > 500) {
            ++slide_demo_step, slide_demo_stage = 0, slide_demo_stage_at = now, slide_demo_slot = -1, slide_demo_held = 0;
        }
        return 0;
    }
    const int slot = int(ms / 300), within = int(ms % 300);
    if (slot != slide_demo_slot) {
        slide_demo_slot = slot;
        const int dx = gx - cx, dy = gy - cy;
        if (dx != 0) slide_demo_held = dx > 0 ? 0x0200 : 0x0100;
        else if (dy != 0) slide_demo_held = dy > 0 ? 0x0800 : 0x0400;
        else {
            slide_demo_held = 0;
            next_stage();
            return b;
        }
        if (ms > 8000) next_stage();  // never stuck
    }
    return std::uint16_t(b | (within < 120 ? slide_demo_held : 0));
}

// The Drop lesson's demonstration: the D-pad, one press at a time, until the
// shadow is over the hole (D-pad right moves the cursor -1 in x, down -1 in
// y, checked in play), a pause, then A.
std::uint16_t drop_lesson_buttons() {
    if (drop_lesson_phase != 2 || guest_rdram == nullptr || !drop_lesson_at || drop_demo_dropped) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *drop_lesson_at).count();
    // A moment to look first, once the sphere has settled after its fly-in.
    constexpr long long look = 800;
    if (ms < look) return 0;
    const int slot = int((ms - look) / 400), within = int((ms - look) % 400);
    if (slot != drop_demo_slot) {
        drop_demo_slot = slot;
        const std::uint32_t pc = 0x80110220u;
        const int dx = drop_lesson_target[0] - (8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2A)));
        const int dy = drop_lesson_target[1] - (8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2E)));
        if (dx != 0) drop_demo_held = dx > 0 ? 0x0200 : 0x0100;
        else if (dy != 0) drop_demo_held = dy > 0 ? 0x0800 : 0x0400;
        else if (!drop_demo_aimed) drop_demo_aimed = true, drop_demo_held = 0, drop_lesson_say = "Lined up: press A";
        else drop_demo_held = 0x8000;
    }
    if (drop_demo_held == 0x8000 && within >= 150) drop_demo_dropped = true;
    return within < 150 ? drop_demo_held : 0;
}

// The fuse lab's automatic slide: the buttons to press now (see research_fuse_target).
std::uint16_t research_fuse_buttons() {
    if (research_fuse_target == 0 || guest_rdram == nullptr) return 0;
    auto* rd = const_cast<std::uint8_t*>(guest_rdram);
    const std::uint32_t pc = 0x80110220u;
    const auto now = std::chrono::steady_clock::now();
    if (!research_fuse_at) {
        static std::optional<std::chrono::steady_clock::time_point> combo_seen;
        if (guest::load32(rd, pc + 0x2AFC) == 0 && !combo_seen) return 0;  // waiting for the combo
        if (!combo_seen) combo_seen = now;
        if (now - *combo_seen < std::chrono::milliseconds(research_fuse_delay)) return 0;
        combo_seen.reset();
        research_fuse_at = now;
        const int* xy = research_fuse_xy[research_fuse_target - 1];
        guest::store16(rd, pc + 0x2B2A, std::uint16_t(std::int16_t(xy[0] - 8)));
        guest::store16(rd, pc + 0x2B2E, std::uint16_t(std::int16_t(xy[1] - 8)));
        std::fprintf(stdout, "{\"event\":\"research_fuse_grab\",\"target\":%d,\"at\":[%d,%d]}\n", research_fuse_target, xy[0], xy[1]);
        std::fflush(stdout);
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - *research_fuse_at).count();
    if (ms > 1600) {
        research_fuse_target = 0;
        return 0;
    }
    std::uint16_t b = ms >= 60 && ms < 1400 ? 0x4000 : 0;  // B held
    for (int k = 0; k < research_fuse_steps; ++k)
        if (ms >= 300 + 300 * k && ms < 420 + 300 * k) b |= 0x0800;  // D-pad up, once per corridor cell
    return b;
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
    static std::string last_bricks;
    if (const auto bricks = profile_data.preferences.bricks.value_or("crafted"); bricks != last_bricks) {
        blockplanet::ui::set_setting("bricks", last_bricks = bricks);
        tetrisphere::set_rt64_brick_style(bricks);
    }
    static std::string last_lighting;
    if (const auto lighting = profile_data.preferences.lighting.value_or("smooth"); lighting != last_lighting) {
        blockplanet::ui::set_setting("lighting", last_lighting = lighting);
        tetrisphere::set_smooth_shading(lighting == "smooth");
    }
    if (text != last_text) blockplanet::ui::set_setting("text_size", last_text = text);
    if (value == last) return;
    last = value;
    blockplanet::ui::set_setting("hud_distance", value);
}

// Graphics > Bricks: the bricks' look (sharp_bricks in rt64_context.cpp),
// saved in the profile.
bool handle_bricks_setting(const std::string& command) {
    const std::string prefix = "setting:bricks=";
    if (command.rfind(prefix, 0) != 0) return false;
    const auto value = command.substr(prefix.size());
    if (value != "original" && value != "sharp" && value != "crafted") return true;
    profile_data.preferences.bricks = value;
    profile_dirty = true;
    save_profile();
    blockplanet::ui::set_setting("bricks", value);
    tetrisphere::set_rt64_brick_style(value);
    return true;
}

// Graphics > Lighting: smooth or the original's flat shading, saved in the profile.
bool handle_lighting_setting(const std::string& command) {
    const std::string prefix = "setting:lighting=";
    if (command.rfind(prefix, 0) != 0) return false;
    const auto value = command.substr(prefix.size());
    if (value != "smooth" && value != "original") return true;
    profile_data.preferences.lighting = value;
    profile_dirty = true;
    save_profile();
    blockplanet::ui::set_setting("lighting", value);
    tetrisphere::set_smooth_shading(value == "smooth");
    return true;
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
    blockplanet::jukebox::on_launch();
    blockplanet::ui::request_close();
    // A grid held after leaving play (Exit, New Field, Try Again) hands over to
    // the loading cover, which keeps it up with the mode's name until play has
    // started (it held for up to 8 s, over the new stage; the owner's report).
    grid_quit_at.reset(), grid_quit_red = false;
    // The loading cover (the owner's request): over the game's own menus
    // while they are pressed through, or the stage's first frames after a
    // direct start, until play has started.
    static const char* names[] = {"Rescue", "Hide & Seek", "Puzzle", "Time Trial", "Versus", "Lines",
                                  "Training", "Training", "Puzzle Training"};
    loading_title = names[std::min<std::size_t>(std::size_t(mode), 8)];
    // Minefield plays in Rescue's mode: its own name on the loading cover.
    if (tetrisphere::minefield_sphere::armed()) loading_title = "Minefield";
    else if (mode == LaunchMode::Rescue && (slide_lesson_phase >= 0 || magic_lesson_phase >= 0 || drop_lesson_phase >= 0)) loading_title = "Training";
    else if (mode == LaunchMode::Rescue) loading_title = variant_title(play_variant);
    // A restart from the pause keeps the menus the game was first started
    // from: the pause screen is not a place to come back to.
    if (const auto path = blockplanet::ui::menu_path();
        !path.empty() && std::none_of(path.begin(), path.end(), [](const auto& s) {
            // (Nor the end screens: New Field and Try Again start from there.)
            return s.first == "pause" || s.first == "gameover" || s.first == "episode_end" || s.first == "puzzle_results";
        }))
        launch_path = path, launch_place = path.back().first == "stages" ? stages_title_now : std::string();
    // A Rescue game always goes back to the Rescue menu (the owner,
    // 2026-11-18), even when started from elsewhere (the main menu's
    // Continue): Main Menu, Solo, Rescue. (Not Minefield, which plays in
    // Rescue's mode from its own menus.)
    // Training's own lessons play on a Rescue stage but go back to Training's
    // list (the owner, 2026-11-20: Back to Training went to the Rescue menu).
    const bool lesson = slide_lesson_phase == 0 || drop_lesson_phase == 0 || magic_lesson_phase == 0;
    if (mode == LaunchMode::Rescue && !tetrisphere::minefield_sphere::armed() && !lesson &&
        std::none_of(launch_path.begin(), launch_path.end(), [](const auto& s) { return s.first == "story"; })) {
        launch_path = {{"main", 0}, {"solo", 0}, {"story", 0}};
        launch_place.clear();
        campaign_source.clear();
    }
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
    // A Rescue or Lines start after an episode end's Exit: the game still
    // shows its card for the next episode first (the pause row at 100),
    // waiting for A, unseen under the loading cover (the owner, 2026-11-18:
    // "our custom tiles were remaining on screen. And I couldn't pause").
    // Pressed away as after Carry on.
    if (mode == LaunchMode::Rescue || mode == LaunchMode::Lines) episode_card_skip = std::chrono::steady_clock::now();
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
    // Frozen by the developer tools: let the game go, or it never stops.
    tetrisphere::frame_step::set_frozen(false);
    tetrisphere::frame_step::set_enabled(false);
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
    // The volume in use now: a profile with no audio settings of its own (a
    // new one) starts from it, rather than full volume (the owner's note).
    const AudioPreferences audio_before = current_audio();
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
        blockplanet::set_audio_levels(audio_before.music, audio_before.effects);
        blockplanet::set_music_random(audio_before.random_music);
        if (!guest) {
            profile_data.preferences.audio = audio_before;
            profile_dirty = true;
            save_profile();
        }
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
    // Research (the test harness): show a screen by its name, as an
    // "open:name" action does (for captures of menus).
    if (const auto screen = argument("show_screen:")) {
        blockplanet::ui::request_show(*screen);
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
            // Testing (the owner, 2026-11-19): W in play (not an F key: the owner's keyboard needs Fn for those) wins the stage at
            // once (the core opened), as the harness's research_win. On for
            // every build while BlockPlanet is being tested; to be put behind a
            // developer switch before a release.
            if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_W &&
                !blockplanet::ui::menus_open()) {
                blockplanet::ui::send_command("research_win");
                continue;
            }
            // The finale after 10:10: any key or button skips to the credits.
            if (final_finale && ((event.type == SDL_KEYDOWN && !event.key.repeat) || event.type == SDL_CONTROLLERBUTTONDOWN ||
                                 event.type == SDL_JOYBUTTONDOWN)) {
                final_skip = true;
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
            if (handle_bricks_setting(*command)) continue;
            if (handle_lighting_setting(*command)) continue;
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
            // The jukebox (main menu's music icon): docs/UI.md, Media Player.
            if (blockplanet::jukebox::command(*command, guest_rdram)) continue;
            if (command->rfind("research_peek:", 0) == 0 && guest_rdram != nullptr) {
                // Research: reads a word (research_peek:ADDR, hex), to the log.
                const auto address = static_cast<std::uint32_t>(std::strtoul(command->c_str() + 14, nullptr, 16));
                std::fprintf(stdout, "{\"event\":\"peek\",\"address\":\"%08X\",\"word\":\"%08X\"}\n", address,
                             guest::load32(guest_rdram, address & ~3u));
                std::fflush(stdout);
                continue;
            }
            if (*command == "research_vortex" && guest_rdram != nullptr) {
                // Research (docs/research/jukebox.md, VORTEX): as if the name code
                // had been typed and Reset were held: the reset screen starts,
                // and after 300 frames plays the vortex scene and its song.
                auto* ram = const_cast<std::uint8_t*>(guest_rdram);
                guest::store16(ram, 0x800E12F4u, 1);
                guest::store32(ram, 0x800E1208u, 0);
                guest::store32(ram, 0x800E120Cu, 0);
                guest::store16(ram, 0x800E11F8u, 1);
                continue;
            }
            if (*command == "research_minefield_lose") {
                tetrisphere::minefield_sphere::research_lose();
                continue;
            }
            if (*command == "minefield_again_end") {
                // From the game's end menu: leave through its Exit, then a new field.
                tetrisphere::minefield_sphere::again();
                continue_mode = PendingLaunch{LaunchMode::Rescue, std::pair{1, 1}, true};
                grid_quit_at = std::chrono::steady_clock::now(), grid_quit_red = true;
                minefield_end_chosen.store(true);
                end_menu.choose(end_menu.exit_row(), std::chrono::steady_clock::now());
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
                // A BlockPlanet lesson starts again from its beginning (the
                // owner, 2026-11-20: a restart left the stage's own sphere,
                // the lesson still thinking its board was built): armed again,
                // with Training's background for the stage it restarts.
                if (slide_lesson_phase >= 0 || magic_lesson_phase >= 0 || drop_lesson_phase >= 0) {
                    if (slide_lesson_phase >= 0) slide_lesson_phase = 0, slide_lesson_at.reset();
                    if (magic_lesson_phase >= 0) magic_lesson_phase = 0, magic_lesson_at.reset();
                    if (drop_lesson_phase >= 0) drop_lesson_phase = 0, drop_lesson_at.reset();
                    pending_results.reset();
                    if (slide_lesson_phase >= 0 || drop_lesson_phase >= 0) {  // (the magic lessons keep the stage's own)
                        auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                        if (drop_lesson_saved_background < 0) drop_lesson_saved_background = guest::load16(rd, 0x800E94C4u);
                        guest::store16(rd, 0x800E94C4u, guest::load16(rd, 0x800E949Cu));
                    }
                }
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
                minefield_end_chosen.store(true);
                // Carry on to the next episode (the owner, 2026-11-18): the
                // game's CONTINUE, which goes on into the next episode, but its
                // own card for it ("Congratulations ... Ready to try the next
                // one?") is pressed away unseen, under BlockPlanet's loading
                // cover; the congratulations are on BlockPlanet's panel. (Leaving
                // through Exit and starting the episode from the menus instead
                // was tried: it left the game's card waiting now and then.)
                if (end_menu.episode_end() && row != end_menu.exit_row() && guest_rdram != nullptr) {
                    const bool lines = guest::load16(guest_rdram, 0x800E44A8) == 9;
                    const int next = read_game_state(guest_rdram).chapter + 2;  // (0-based there)
                    episode_card_skip = std::chrono::steady_clock::now();
                    loading_title = std::string(lines ? "Lines" : variant_title(play_variant)) + " \u00B7 Episode " + std::to_string(next);
                    loading_since = std::chrono::steady_clock::now();
                    loading_active = true;
                }
                if (row == end_menu.exit_row()) exit_hide_since = std::chrono::steady_clock::now(), exit_hide_let_out = false;
                // Try Again (and a game over's other choices back into play):
                // under BlockPlanet's loading cover, as a start is, until the
                // stage has faded in; the game's own fade showed for a moment
                // with its original HUD models.
                if (row != end_menu.exit_row() && !end_menu.episode_end() && guest_rdram != nullptr) {
                    const auto state = read_game_state(guest_rdram);
                    loading_title = state.mode == 1 ? std::string(variant_title(play_variant)) + " \u00B7 " + std::to_string(state.chapter + 1) + ":" +
                                                          std::to_string(state.level + 1)
                                                    : std::string();
                    loading_since = std::chrono::steady_clock::now();
                    loading_active = true;
                }
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
                    // The episode from its first stage (the owner, 2026-11-18: an
                    // episode left part-way starts again), with the score and
                    // hearts it began with (the checkpoint taken at its start).
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
                campaign_source.clear();
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
                campaign_source.clear();
                blockplanet::ui::set_text("chapters_title", std::string("Lines ") + variant_title(menu_variant));
                blockplanet::ui::set_text("chapters_subtitle", "Choose an episode");
                blockplanet::ui::set_list("chapters", story_chapters(completed, lines_chapters, unlock_all_testing));
                blockplanet::ui::request_show("chapters");
                continue;
            }
            if (*command == "campaign" || *command == "lines_campaign" || command->rfind("campaign_episode:", 0) == 0 ||
                command->rfind("lines_campaign_episode:", 0) == 0 || *command == "campaign_refresh" || *command == "lines_campaign_refresh") {
                const bool refresh = command->size() > 8 && command->compare(command->size() - 8, 8, "_refresh") == 0;
                // Campaign (the owner, 2026-11-18): a list of the episodes, in
                // order, with the one to carry on chosen ("Continue Episode N"
                // once some of its stages are done, "Play Episode N" before);
                // the earlier ones can be played again from their start.
                // Rescue's, or Lines' (lines_campaign). All open for testing.
                const bool lines = command->rfind("lines_", 0) == 0;
                menu_lines = lines;
                const char* game = lines ? "lines" : "rescue";
                const int episodes_in_game = lines ? lines_chapters : rescue_chapters;
                menu_variant = PlayVariant::Story;
                const auto& levels = profile_data.progress.levels_completed;
                const auto& chapters = profile_data.progress.chapters_completed;
                const auto episodes = chapters.find(game);
                const auto completed = [&](int e) { return episodes != chapters.end() && episodes->second.count(e) != 0; };
                int resume = 1;
                while (resume <= episodes_in_game && completed(resume)) ++resume;
                const std::string prefix = lines ? "lines_campaign_episode:" : "campaign_episode:";
                if (command->rfind(prefix, 0) == 0) {
                    const int episode = std::clamp(std::atoi(command->c_str() + prefix.size()), 1, episodes_in_game);
                    // Always from the episode's first stage (the owner, 2026-11-18):
                    // an episode left part-way is not completed, and starts again.
                    const int stage = 1;
                    blockplanet::ui::send_command("chapter:" + std::to_string(episode) + ":" + std::to_string(stage));
                    continue;
                }
                const int focus = std::min(resume, episodes_in_game);
                const int shown = unlock_all_testing ? episodes_in_game : focus;
                std::vector<blockplanet::ui::ListItem> items;
                for (int e = 1; e <= shown; ++e) {
                    const bool here = e == focus && !completed(e);
                    // The one to carry on marked in the note, as "completed" is (the owner, 2026-11-18).
                    const std::string name = "Episode " + std::to_string(e);
                    blockplanet::ui::ListItem item{prefix + std::to_string(e), name,
                                                   completed(e) ? "completed" : here ? "next" : "",
                                                   "command:" + prefix + std::to_string(e), true, 1, e == focus};
                    item.description = completed(e) ? "Play this episode again from its first stage."
                                       : here ? "Start this episode, from its first stage." : "Open for testing.";
                    items.push_back(std::move(item));
                }
                blockplanet::ui::set_text("chapters_title", lines ? "Lines Campaign" : "Campaign");
                blockplanet::ui::set_text("chapters_subtitle", "Choose an episode");
                blockplanet::ui::set_list("chapters", items);
                campaign_source = lines ? "lines_campaign" : "campaign";
                if (!refresh) blockplanet::ui::request_show("chapters");
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
            if (command->rfind("research_find:", 0) == 0 && guest_rdram != nullptr) {
                // Research: the addresses (0x80000000 to 0x80400000) whose word
                // equals a value ("research_find:8018B550"), the first 32.
                const std::uint32_t value = std::uint32_t(std::strtoul(command->c_str() + 14, nullptr, 16));
                std::string out;
                int n = 0;
                for (std::uint32_t a = 0x80000000u; a < 0x80400000u && n < 32; a += 4u)
                    if (guest::load32(guest_rdram, a) == value) {
                        char hex[16];
                        std::snprintf(hex, sizeof hex, "%08X", a);
                        out += (n++ ? "," : "") + std::string("\"") + hex + "\"";
                    }
                std::fprintf(stdout, "{\"event\":\"research_find\",\"value\":\"%08X\",\"at\":[%s]}\n", value, out.c_str());
                std::fflush(stdout);
                continue;
            }
            if (command->rfind("research_hex:", 0) == 0 && guest_rdram != nullptr) {
                // Research: N bytes of guest memory as numbers ("research_hex:800E9190:256").
                const std::uint32_t at = std::uint32_t(std::strtoul(command->c_str() + 13, nullptr, 16));
                const auto colon = command->find(':', 13);
                const int n = colon == std::string::npos ? 16 : std::atoi(command->c_str() + colon + 1);
                std::string out;
                for (int k = 0; k < n && k < 4096; ++k) out += (k ? "," : "") + std::to_string(guest::load8(guest_rdram, at + std::uint32_t(k)));
                std::fprintf(stdout, "{\"event\":\"research_hex\",\"at\":\"%08X\",\"bytes\":[%s]}\n", at, out.c_str());
                std::fflush(stdout);
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
            if (*command == "research_magic_retry" && guest_rdram != nullptr) {
                // Presses the magic button again only if the magic is still held
                // (a press while another magic works is ignored).
                if (std::int8_t(guest::load8(guest_rdram, 0x80110220u + 0x702)) >= 0)
                    research_magic_press = std::chrono::steady_clock::now();
                continue;
            }
            if (command->rfind("research_magic:", 0) == 0 && guest_rdram != nullptr) {
                const int kind = std::atoi(command->c_str() + 15);
                guest::store8(const_cast<std::uint8_t*>(guest_rdram), 0x80110220u + 0x702, std::uint8_t(std::int8_t(kind)));
                research_magic_press = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
                continue;
            }
            if ((*command == "research_snap" || *command == "research_diff") && guest_rdram != nullptr) {
                // Research: research_snap keeps the board; research_diff logs each
                // cell emptied since, as [x, y, layer], with the cursor and the
                // magic's aim (+0x2B48/+0x2B4C, if set).
                static std::vector<std::int16_t> snap;
                const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u);
                if (*command == "research_snap") {
                    snap.assign(9 * 1024, 0);
                    for (int i = 0; i < 9 * 1024; ++i) snap[std::size_t(i)] = std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(i)));
                    continue;
                }
                const std::uint32_t pc = 0x80110220u;
                std::string out = "{\"event\":\"research_diff\",\"cursor\":[" +
                                  std::to_string(8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2A))) + "," +
                                  std::to_string(8 + std::int16_t(guest::load16(guest_rdram, pc + 0x2B2E))) + "],\"aim\":[" +
                                  std::to_string(std::int16_t(guest::load16(guest_rdram, pc + 0x2B48))) + "," +
                                  std::to_string(std::int16_t(guest::load16(guest_rdram, pc + 0x2B4C))) + "],\"gone\":[";
                bool first = true;
                for (int i = 0; i < 9 * 1024 && i < int(snap.size()); ++i) {
                    const std::int16_t v = std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(i)));
                    if (snap[std::size_t(i)] > 0 && v <= 0) {
                        out += std::string(first ? "" : ",") + "[" + std::to_string(i % 32) + "," + std::to_string((i % 1024) / 32) + "," +
                               std::to_string(i / 1024) + "]";
                        first = false;
                    }
                }
                out += "],\"added\":[";
                first = true;
                for (int i = 1024; i < 9 * 1024 && i < int(snap.size()); ++i) {
                    const std::int16_t v = std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(i)));
                    if (snap[std::size_t(i)] <= 0 && v > 0) {
                        out += std::string(first ? "" : ",") + "[" + std::to_string(i % 32) + "," + std::to_string((i % 1024) / 32) + "," +
                               std::to_string(i / 1024) + "]";
                        first = false;
                    }
                }
                out += "]}";
                std::fprintf(stdout, "%s\n", out.c_str());
                std::fflush(stdout);
                continue;
            }
            if (command->rfind("research_lab:combo", 0) == 0 && guest_rdram != nullptr) {
                // Research (2026-11-20, the combo lab): rebuilds stage 1:1's top
                // layer round the cursor so one drop makes a combo. Every cell
                // of layer 1 becomes an inert wall; the piece in hand's shape is
                // cut out of it at the cursor (down to the core), and two bricks
                // of the same shape are set beside the hole at two of the
                // shape's fitting positions (record +0x74C), so the piece
                // dropped there lands in the hole touching both.
                // research_lab:combo:I:J:SX:SY picks fitting entries I and J and
                // the signs for their column and row (default 0:1:1:1).
                int fi = 0, fj = 1, sx = 1, sy = 1;
                std::sscanf(command->c_str() + 13, "combo:%d:%d:%d:%d", &fi, &fj, &sx, &sy);
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t pc = 0x80110220u;
                const int cx = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                const auto board = build_pair_board(cx, cy, fi, fj, sx, sy);
                const int index = board.index, v0 = board.v0;
                const auto& fit = board.fit;
                const int* layout = board.layout;
                std::string fl;
                for (const auto& [fx, fy] : fit) fl += (fl.empty() ? "" : ",") + std::string("[") + std::to_string(fx) + "," + std::to_string(fy) + "]";
                std::string lay;
                for (int k = 0; k < 9; ++k) lay += (k ? "," : "") + std::to_string(layout[k]);
                std::fprintf(stdout, "{\"event\":\"research_lab\",\"index\":%d,\"value\":%d,\"cursor\":[%d,%d],\"layout\":[%s],\"fits\":[%s],\"used\":[%d,%d],\"signs\":[%d,%d]}\n",
                             index, v0, cx, cy, lay.c_str(), fl.c_str(), fi, fj, sx, sy);
                std::fflush(stdout);
                continue;
            }
            if (command->rfind("research_lab:stack", 0) == 0 && guest_rdram != nullptr) {
                // Research (2026-11-21): a stack of the piece in hand's shape,
                // H high (default 5), two cells right of the cursor in a pit.
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                int height = 5;
                std::sscanf(command->c_str(), "research_lab:stack:%d", &height);
                const std::uint32_t pc = 0x80110220u;
                const int cx = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                const int index = std::int16_t(guest::load16(rd, pc + 0x2374));
                guest::store16(rd, pc + 0x23E4, 16);
                build_stack_pit(cx - 4, cy - 4, 8, 8, std::clamp(height, 1, 7), {{index, {cx - 2, cy}}});
                continue;
            }
            if (command->rfind("research_lab:rubble", 0) == 0 && guest_rdram != nullptr) {
                // Research (2026-11-21, the rubble lesson): the bar (shape 1)
                // in hand. Mode 1: bar, rubble line under the shadow, bar,
                // rubble line, bar (convert both lines, then drop: one combo).
                // Mode 2: a bar at the shadow and three lines of rubble beyond
                // it with a mystery block in the middle (slide the bar through).
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                int mode = 1;
                std::sscanf(command->c_str(), "research_lab:rubble:%d", &mode);
                const std::uint32_t pc = 0x80110220u;
                const int cx = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                const auto cells = lesson_shape_cells(1);
                const bool along_x = cells.size() > 1 && cells[0].first != cells[1].first;
                const int px = along_x ? 0 : 1, py = along_x ? 1 : 0;  // across the bar
                std::vector<std::pair<int, std::pair<int, int>>> pieces;
                std::vector<LessonCell> singles;
                const auto rubble_line = [&](int ox, int oy, int mystery_at) {
                    for (std::size_t i = 0; i < cells.size(); ++i)
                        singles.push_back({ox + cells[i].first, oy + cells[i].second, int(i) == mystery_at ? mystery_value : rubble_value,
                                           int(i) == mystery_at ? mystery_entry : rubble_entry});
                };
                if (mode == 2) {
                    pieces.push_back({1, {cx, cy}});
                    for (int k = 1; k <= 3; ++k) rubble_line(cx + px * k, cy + py * k, k == 2 ? 1 : -1);
                } else {
                    for (int k = -1; k <= 3; ++k)
                        if (k % 2 != 0) pieces.push_back({1, {cx + px * k, cy + py * k}});
                        else rubble_line(cx + px * k, cy + py * k, -1);
                }
                guest::store16(rd, pc + 0x23E4, 16);
                for (int q = 0; q < 4; ++q) guest::store16(rd, pc + 0x2374 + 2u * std::uint32_t(q), 1);
                build_stack_pit(cx - 4, cy - 4, 8, 9, 1, pieces, singles);
                std::fprintf(stdout, "{\"event\":\"research_lab\",\"lab\":\"rubble\",\"mode\":%d,\"cursor\":[%d,%d],\"across\":[%d,%d]}\n", mode, cx, cy, px, py);
                std::fflush(stdout);
                continue;
            }
            if (*command == "research_lab:gravity" && guest_rdram != nullptr) {
                // Research (2026-11-20, the gravity lab): stage 1:1 rebuilt so
                // one drop makes a combo that drops a piece into a second one.
                // Layer 1 is walls; the piece about to drop (shape K) is cut out
                // at the cursor with two K pieces beside it (A, B: the first
                // combo). A piece of the stage's other shape (L), F, rests on
                // layer 2 over A (its other cells over holes); two L pieces (C,
                // D) wait on layer 1 at L's fitting positions round A's place,
                // so when A breaks, F falls there and matches them: a gravity
                // combo. (They are another shape, so the first combo leaves them.)
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t pc = 0x80110220u;
                const std::uint32_t values = guest::load32(rd, 0x800E44D0u), masks = guest::load32(rd, 0x800E44D4u),
                                    entries = guest::load32(rd, 0x800E44D8u), columns = guest::load32(rd, 0x800E44DCu);
                const int cx = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                struct Shape { std::uint32_t rec; std::int16_t v0; int layout[9]; std::vector<std::pair<int, int>> fit; };
                const auto shape = [&](int index) {
                    Shape sh{};
                    sh.rec = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(index));
                    sh.v0 = std::int16_t(guest::load16(rd, sh.rec));
                    for (int k = 0; k < 9; ++k) sh.layout[k] = std::int16_t(guest::load16(rd, sh.rec + 0xA + 2u * std::uint32_t(k)));
                    const int fits = std::int16_t(guest::load16(rd, sh.rec + 0x74A));
                    for (int k = 0; k < fits && k < 14; ++k)
                        sh.fit.push_back({std::int8_t(guest::load8(rd, sh.rec + 0x74C + 2u * std::uint32_t(k))),
                                          std::int8_t(guest::load8(rd, sh.rec + 0x74D + 2u * std::uint32_t(k)))});
                    return sh;
                };
                const int ki = std::int16_t(guest::load16(rd, pc + 0x2374));
                const int li = ki == 0 ? 1 : 0;  // 1:1 has two shapes, indexes 0 and 1
                const Shape K = shape(ki), L = shape(li);
                const auto cell = [](int x, int y) { return ((y % 32 + 32) % 32) * 32 + ((x % 32 + 32) % 32); };
                const auto cells_of = [&](const Shape& sh, int ox, int oy) {
                    std::vector<std::pair<int, int>> out;  // (board cell, cell number)
                    for (int x = 0; x < 3; ++x)
                        for (int y = 0; y < 3; ++y)
                            if (sh.layout[x * 3 + y] > 0) out.push_back({cell(ox + x - 1, oy + y - 1), sh.layout[x * 3 + y]});
                    return out;
                };
                std::vector<int> l1(1024, 1), l2(1024, 0);  // layer 1: 0 empty, 1 wall, 2 piece; layer 2: 1 piece
                for (int c = 0; c < 1024; ++c) {
                    guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 423);
                    for (int layer = 2; layer < 9; ++layer) {
                        guest::store16(rd, values + 2u * std::uint32_t(layer * 1024 + c), 0);
                        guest::store8(rd, masks + std::uint32_t(layer * 1024 + c), 0);
                    }
                }
                const auto put = [&](const Shape& sh, int ox, int oy, int layer) {
                    for (const auto& [c, n] : cells_of(sh, ox, oy)) {
                        guest::store16(rd, values + 2u * std::uint32_t(layer * 1024 + c), std::int16_t(sh.v0 + n - 1));
                        guest::store32(rd, entries + 4u * std::uint32_t(layer * 1024 + c), sh.rec + 0x78u + 96u * std::uint32_t(n - 1));
                        (layer == 1 ? l1 : l2)[std::size_t(c)] = layer == 1 ? 2 : 1;
                    }
                };
                const auto hole = [&](int c) {
                    l1[std::size_t(c)] = 0;
                    guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 0);
                    guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), 0);
                };
                // The first combo: the hole at the cursor, A and B.
                for (const auto& [c, n] : cells_of(K, cx, cy)) hole(c);
                const int ax = cx + K.fit[0].first, ay = cy + K.fit[0].second;
                put(K, ax, ay, 1);
                put(K, cx + K.fit[1].first, cy + K.fit[1].second, 1);
                // F: an L piece on layer 2 resting on A (at least one cell over
                // A) and clear of the hole and of B, found by trying origins
                // round A; its cells not over A are cut out of layer 1 beneath.
                std::vector<int> a_cells;
                for (const auto& [c, n] : cells_of(K, ax, ay)) a_cells.push_back(c);
                const auto in_a = [&](int c) { return std::find(a_cells.begin(), a_cells.end(), c) != a_cells.end(); };
                int fx = ax, fy = ay;
                bool found = false;
                for (int r = 0; r <= 2 && !found; ++r)
                    for (int dy = -r; dy <= r && !found; ++dy)
                        for (int dx = -r; dx <= r && !found; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                            bool over_a = false, clear = true;
                            for (const auto& [c, n] : cells_of(L, ax + dx, ay + dy)) {
                                over_a = over_a || in_a(c);
                                clear = clear && (in_a(c) || l1[std::size_t(c)] == 1);
                            }
                            if (over_a && clear) fx = ax + dx, fy = ay + dy, found = true;
                        }
                for (const auto& [c, n] : cells_of(L, fx, fy))
                    if (!in_a(c)) hole(c);
                put(L, fx, fy, 2);
                // C and D: the first two of L's fitting positions round F's
                // landing place whose cells are all walls now.
                int placed = 0;
                std::string used;
                for (std::size_t k = 0; k < L.fit.size() && placed < 2; ++k) {
                    const int ox = fx + L.fit[k].first, oy = fy + L.fit[k].second;
                    bool free = true;
                    for (const auto& [c, n] : cells_of(L, ox, oy)) free = free && l1[std::size_t(c)] == 1;
                    if (!free) continue;
                    put(L, ox, oy, 1);
                    used += (used.empty() ? "" : ",") + std::to_string(k);
                    ++placed;
                }
                // Columns and face masks.
                for (int c = 0; c < 1024; ++c) {
                    const int x = c % 32, y = c / 32;
                    guest::store8(rd, masks + std::uint32_t(c), l1[std::size_t(c)] ? 0 : 80);
                    guest::store8(rd, columns + std::uint32_t(c),
                                  l2[std::size_t(c)] ? 0x22 : l1[std::size_t(c)] ? 0x11 : 0x00);
                    for (int layer = 1; layer <= 2; ++layer) {
                        const auto& g = layer == 1 ? l1 : l2;
                        if (!g[std::size_t(c)]) continue;
                        std::uint8_t m = (layer == 1 && l2[std::size_t(c)]) ? 0 : 16;
                        if (!g[std::size_t(cell(x + 1, y))]) m |= 8;
                        if (!g[std::size_t(cell(x - 1, y))]) m |= 4;
                        if (!g[std::size_t(cell(x, y + 1))]) m |= 2;
                        if (!g[std::size_t(cell(x, y - 1))]) m |= 1;
                        guest::store8(rd, masks + std::uint32_t(layer * 1024 + c), m);
                    }
                }
                std::fprintf(stdout, "{\"event\":\"research_lab\",\"lab\":\"gravity\",\"k\":%d,\"l\":%d,\"cursor\":[%d,%d],\"a\":[%d,%d],\"f\":[%d,%d],\"f_found\":%d,\"cd_fits\":[%s]}\n",
                             ki, li, cx, cy, ax, ay, fx, fy, found, used.c_str());
                std::fflush(stdout);
                continue;
            }
            if ((command->rfind("research_lab:fuse", 0) == 0 || *command == "research_lab_cursor") && guest_rdram != nullptr) {
                // Options after "fuse": B (B a power piece too), H (the drop
                // hole's cells flagged power, for a combo started on one).
                const std::string opts = command->size() > 17 ? command->substr(17) : std::string();
                const bool power_b = opts.find('B') != std::string::npos, power_hole = opts.find('H') != std::string::npos;
                // Research (2026-11-20, the fuse lab): the combo lab's first
                // combo with A a power piece (attribute 0x40 on its cells), and,
                // beside B, an empty corridor running up (+y) to a place where
                // two more pieces of B's shape (E, G) fit B but not B's start.
                // Sliding B (a fuse, flashing in the combo) up the corridor and
                // letting go should set off a fuse combo. research_lab_cursor
                // puts the cursor on B (for the harness, after the drop).
                static int bx = 0, by = 0;
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t pc = 0x80110220u;
                if (*command == "research_lab_cursor") {
                    guest::store16(rd, pc + 0x2B2A, std::uint16_t(std::int16_t(bx - 8)));
                    guest::store16(rd, pc + 0x2B2E, std::uint16_t(std::int16_t(by - 8)));
                    continue;
                }
                const std::uint32_t values = guest::load32(rd, 0x800E44D0u), masks = guest::load32(rd, 0x800E44D4u),
                                    entries = guest::load32(rd, 0x800E44D8u), columns = guest::load32(rd, 0x800E44DCu);
                const int cx = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                const int ki = std::int16_t(guest::load16(rd, pc + 0x2374));
                const std::uint32_t rec = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(ki));
                const std::int16_t v0 = std::int16_t(guest::load16(rd, rec));
                int layout[9];
                for (int k = 0; k < 9; ++k) layout[k] = std::int16_t(guest::load16(rd, rec + 0xA + 2u * std::uint32_t(k)));
                std::vector<std::pair<int, int>> fit;
                for (int k = 0; k < std::int16_t(guest::load16(rd, rec + 0x74A)) && k < 14; ++k)
                    fit.push_back({std::int8_t(guest::load8(rd, rec + 0x74C + 2u * std::uint32_t(k))),
                                   std::int8_t(guest::load8(rd, rec + 0x74D + 2u * std::uint32_t(k)))});
                const auto cell = [](int x, int y) { return ((y % 32 + 32) % 32) * 32 + ((x % 32 + 32) % 32); };
                const auto cells_at = [&](int ox, int oy) {
                    std::vector<std::pair<int, int>> out;
                    for (int x = 0; x < 3; ++x)
                        for (int y = 0; y < 3; ++y)
                            if (layout[x * 3 + y] > 0) out.push_back({cell(ox + x - 1, oy + y - 1), layout[x * 3 + y]});
                    return out;
                };
                std::vector<int> l1(1024, 1), power(1024, 0);
                for (int c = 0; c < 1024; ++c) {
                    guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 423);
                    for (int layer = 2; layer < 9; ++layer) {
                        guest::store16(rd, values + 2u * std::uint32_t(layer * 1024 + c), 0);
                        guest::store8(rd, masks + std::uint32_t(layer * 1024 + c), 0);
                    }
                }
                const auto put = [&](int ox, int oy, bool is_power) {
                    for (const auto& [c, n] : cells_at(ox, oy)) {
                        guest::store16(rd, values + 2u * std::uint32_t(1024 + c), std::int16_t(v0 + n - 1));
                        guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), rec + 0x78u + 96u * std::uint32_t(n - 1));
                        l1[std::size_t(c)] = 2;
                        power[std::size_t(c)] = is_power;
                    }
                };
                const auto hole = [&](int ox, int oy) {
                    for (const auto& [c, n] : cells_at(ox, oy))
                        if (l1[std::size_t(c)] == 1) {
                            l1[std::size_t(c)] = 0;
                            guest::store16(rd, values + 2u * std::uint32_t(1024 + c), 0);
                            guest::store32(rd, entries + 4u * std::uint32_t(1024 + c), 0);
                        }
                };
                hole(cx, cy);
                if (power_hole)
                    for (const auto& [c, n] : cells_at(cx, cy)) power[std::size_t(c)] = 1;
                // A on the -x side (a fitting position with dx < 0, else the
                // first), B on the +x side.
                std::pair<int, int> fa = fit[0], fb = fit.size() > 1 ? fit[1] : fit[0];
                for (const auto& f : fit) if (f.first < 0 && f.second == 0) fa = f;
                for (const auto& f : fit) if (f.first > 0 && f.second == 0) fb = f;
                put(cx + fa.first, cy + fa.second, true);
                bx = cx + fb.first, by = cy + fb.second;
                research_fuse_xy[0][0] = bx, research_fuse_xy[0][1] = by;
                research_fuse_xy[1][0] = cx + fa.first, research_fuse_xy[1][1] = cy + fa.second;
                put(bx, by, power_b);
                // The corridor: B's shape moved up 1 to s cells, s one more than
                // the largest row offset among the fits (so the end fits nothing
                // B touches at its start).
                int s_up = 1;
                for (const auto& f : fit) s_up = std::max(s_up, std::abs(f.second) + 1);
                for (int k = 1; k <= s_up; ++k) hole(bx, by + k);
                research_fuse_steps = s_up;
                const int dx = bx, dy = by + s_up;
                // E: the fit with the largest row offset (above the end); G: one
                // with dx > 0 and no row offset (beside it).
                std::pair<int, int> fe = fit[0], fg = fit[0];
                for (const auto& f : fit) if (f.second > fe.second) fe = f;
                for (const auto& f : fit) if (f.first > 0 && f.second == 0) fg = f;
                put(dx + fe.first, dy + fe.second, false);
                put(dx + fg.first, dy + fg.second, false);
                for (int c = 0; c < 1024; ++c) {
                    const int x = c % 32, y = c / 32;
                    guest::store8(rd, masks + std::uint32_t(c), l1[std::size_t(c)] ? 0 : 80);
                    guest::store8(rd, columns + std::uint32_t(c), l1[std::size_t(c)] ? 0x11 : 0x00);
                    if (!l1[std::size_t(c)]) continue;
                    std::uint8_t m = 16;
                    if (!l1[std::size_t(cell(x + 1, y))]) m |= 8;
                    if (!l1[std::size_t(cell(x - 1, y))]) m |= 4;
                    if (!l1[std::size_t(cell(x, y + 1))]) m |= 2;
                    if (!l1[std::size_t(cell(x, y - 1))]) m |= 1;
                    if (power[std::size_t(c)]) m |= 0x40;
                    guest::store8(rd, masks + std::uint32_t(1024 + c), m);
                }
                if (power_hole)  // the hole's cells: empty, flagged power
                    for (const auto& [c, n] : cells_at(cx, cy)) guest::store8(rd, masks + std::uint32_t(1024 + c), 0x40);
                std::fprintf(stdout, "{\"event\":\"research_lab\",\"lab\":\"fuse\",\"k\":%d,\"cursor\":[%d,%d],\"a\":[%d,%d],\"b\":[%d,%d],\"slide_up\":%d,\"e\":[%d,%d],\"g\":[%d,%d]}\n",
                             ki, cx, cy, cx + fa.first, cy + fa.second, bx, by, s_up, dx + fe.first, dy + fe.second, dx + fg.first, dy + fg.second);
                std::fflush(stdout);
                continue;
            }
            if (command->rfind("research_fuse_auto:", 0) == 0) {
                research_fuse_target = (*command)[19] == 'A' ? 2 : 1;
                research_fuse_delay = command->size() > 21 ? std::atoi(command->c_str() + 21) : 0;  // research_fuse_auto:B:150
                research_fuse_at.reset();
                continue;
            }
            if (command->rfind("research_lab:stripes2", 0) == 0 && guest_rdram != nullptr) {
                // research_lab:stripes2:N builds N layers (more than the stage's own);
                // ":patch" keeps the stripes to an 18 x 16 patch round the cursor
                // (six bars or nine squares across), walls elsewhere (the owner,
                // 2026-11-20: half the sphere is enough, the rest pink walls).
                const int forced_top = command->size() > 22 ? std::atoi(command->c_str() + 22) : 0;
                const bool patch = command->find(":patch") != std::string::npos;
                // Second try (single cells were cleaned up by the game, and the
                // stage counted as won): real pieces. Layers alternate between
                // 2 x 2 squares (the one shape that tiles the board) and inert
                // walls (value 423), to see whether a magic digs through walls.
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t values = guest::load32(rd, 0x800E44D0u), masks = guest::load32(rd, 0x800E44D4u),
                                    entries = guest::load32(rd, 0x800E44D8u), columns = guest::load32(rd, 0x800E44DCu);
                std::string kinds;
                std::uint32_t square = 0, bar = 0;
                for (int i = 0; i < 8; ++i) {
                    const std::uint32_t r = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(i));
                    const int m = std::int16_t(guest::load16(rd, r + 2));
                    kinds += (i ? "," : "") + std::to_string(m);
                    if (m == 33 && !square) square = r;
                    if (m == 32 && !bar) bar = r;  // the lying bar (3 across)
                }
                if (!square) {
                    std::fprintf(stdout, "{\"event\":\"research_lab\",\"lab\":\"stripes2\",\"error\":\"no square\",\"kinds\":[%s]}\n", kinds.c_str());
                    std::fflush(stdout);
                    continue;
                }
                const std::int16_t v0 = std::int16_t(guest::load16(rd, square));
                int layout[9];
                for (int k = 0; k < 9; ++k) layout[k] = std::int16_t(guest::load16(rd, square + 0xA + 2u * std::uint32_t(k)));
                int top = 1;
                for (int layer = 1; layer < 9; ++layer)
                    for (int c = 0; c < 1024; ++c)
                        if (std::int16_t(guest::load16(rd, values + 2u * std::uint32_t(layer * 1024 + c))) > 0) top = std::max(top, layer);
                if (forced_top >= 1 && forced_top <= 8) top = forced_top;
                // The bar's cell numbers along its row, and the cursor (the
                // squares' strip that makes up the width goes round the back).
                int bar_num[3] = {};
                if (bar)
                    for (int x = 0; x < 3; ++x)
                        for (int y = 0; y < 3; ++y) {
                            const int n = std::int16_t(guest::load16(rd, bar + 0xA + 2u * std::uint32_t(x * 3 + y)));
                            if (n > 0) bar_num[x] = n;
                        }
                const std::int16_t bar_v0 = bar ? std::int16_t(guest::load16(rd, bar)) : 0;
                const int cur_x = 8 + std::int16_t(guest::load16(rd, 0x80110220u + 0x2B2A));
                const int cur_y = 8 + std::int16_t(guest::load16(rd, 0x80110220u + 0x2B2E));
                const int strip = (cur_x + 16) % 32;
                const int px0 = cur_x - 9, py0 = cur_y - 8;  // the patch's corner
                constexpr std::uint32_t cube_table = 0x807F8400u;  // a plain cube, for the walls
                {
                    const std::int16_t corners[8] = {81, 14, 94, 14, 81, 1, 94, 1};
                    for (int i = 0; i < 48; ++i) guest::store16(rd, cube_table + 2u * std::uint32_t(i), std::uint16_t(corners[i % 8]));
                }
                // The square's cell number at each (x, y) within a 2 x 2 tile.
                int num[2][2] = {};
                for (int x = 0; x < 3; ++x)
                    for (int y = 0; y < 3; ++y)
                        if (layout[x * 3 + y] > 0 && x < 2 && y < 2) num[x][y] = layout[x * 3 + y];
                for (int c = 0; c < 1024; ++c) {
                    const int x = c % 32, y = c / 32;
                    for (int layer = 1; layer < 9; ++layer) {
                        const std::uint32_t at = std::uint32_t(layer * 1024 + c);
                        if (layer > top) {
                            guest::store16(rd, values + 2u * at, 0);
                            guest::store32(rd, entries + 4u * at, 0);
                            guest::store8(rd, masks + at, 0);
                            continue;
                        }
                        const bool squares = (top - layer) % 2 == 0;
                        if (patch) {
                            const int pxr = ((x - px0) % 32 + 32) % 32, pyr = ((y - py0) % 32 + 32) % 32;
                            if (pxr >= 18 || pyr >= 16 || !bar) {
                                guest::store16(rd, values + 2u * at, 423);
                                guest::store32(rd, entries + 4u * at, cube_table);
                            } else if (squares) {
                                const int n = num[pxr % 2][pyr % 2];
                                guest::store16(rd, values + 2u * at, std::int16_t(v0 + n - 1));
                                guest::store32(rd, entries + 4u * at, square + 0x78u + 96u * std::uint32_t(n - 1));
                            } else {
                                const int n = bar_num[pxr % 3];
                                guest::store16(rd, values + 2u * at, std::int16_t(bar_v0 + n - 1));
                                guest::store32(rd, entries + 4u * at, bar + 0x78u + 96u * std::uint32_t(n - 1));
                            }
                            guest::store8(rd, masks + at, layer == top ? 16 : 0);
                            continue;
                        }
                        const int rel = ((x - strip) % 32 + 32) % 32;  // 0, 1: the squares' strip; 2 to 31: ten bars a row
                        if (squares || rel < 2 || !bar) {
                            if (!squares && !bar) {
                                guest::store16(rd, values + 2u * at, 423);
                            } else {
                                const int n = squares ? num[x % 2][y % 2] : num[rel % 2][y % 2];
                                guest::store16(rd, values + 2u * at, std::int16_t(v0 + n - 1));
                                guest::store32(rd, entries + 4u * at, square + 0x78u + 96u * std::uint32_t(n - 1));
                            }
                        } else {
                            const int n = bar_num[(rel - 2) % 3];
                            guest::store16(rd, values + 2u * at, std::int16_t(bar_v0 + n - 1));
                            guest::store32(rd, entries + 4u * at, bar + 0x78u + 96u * std::uint32_t(n - 1));
                        }
                        guest::store8(rd, masks + at, layer == top ? 16 : 0);
                    }
                    guest::store8(rd, columns + std::uint32_t(c), std::uint8_t((top << 4) | top));
                }
                std::fprintf(stdout, "{\"event\":\"research_lab\",\"lab\":\"stripes2\",\"top\":%d,\"kinds\":[%s]}\n", top, kinds.c_str());
                std::fflush(stdout);
                continue;
            }
            if (*command == "research_lab:stripes" && guest_rdram != nullptr) {
                // Research (2026-11-20, the owner's idea for the magic lessons):
                // every layer of the stage one colour, alternating between the
                // stage's first two piece colours, each cell a plain cube (the
                // smooth single-block look, tile 81, as Minefield's cells), so a
                // magic's damage shows how many layers down it went.
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t values = guest::load32(rd, 0x800E44D0u), masks = guest::load32(rd, 0x800E44D4u),
                                    entries = guest::load32(rd, 0x800E44D8u), columns = guest::load32(rd, 0x800E44DCu);
                constexpr std::uint32_t cube_table = 0x807F8400u;  // free upper memory, after Minefield's tables
                const std::int16_t corners[8] = {81, 14, 94, 14, 81, 1, 94, 1};
                for (int i = 0; i < 48; ++i) guest::store16(rd, cube_table + 2u * std::uint32_t(i), std::uint16_t(corners[i % 8]));
                int top = 1;
                for (int layer = 1; layer < 9; ++layer)
                    for (int c = 0; c < 1024; ++c)
                        if (std::int16_t(guest::load16(rd, values + 2u * std::uint32_t(layer * 1024 + c))) > 0) top = std::max(top, layer);
                const std::int16_t mat[2] = {
                    std::int16_t(guest::load16(rd, guest::load32(rd, 0x8014CBC8u) + 2)),
                    std::int16_t(guest::load16(rd, guest::load32(rd, 0x8014CBC8u + 4u) + 2))};
                for (int c = 0; c < 1024; ++c) {
                    for (int layer = 1; layer < 9; ++layer) {
                        const bool filled = layer <= top;
                        guest::store16(rd, values + 2u * std::uint32_t(layer * 1024 + c), filled ? std::int16_t(mat[(top - layer) % 2] * 9) : 0);
                        guest::store32(rd, entries + 4u * std::uint32_t(layer * 1024 + c), filled ? cube_table : 0u);
                        guest::store8(rd, masks + std::uint32_t(layer * 1024 + c), layer == top ? 16 : 0);
                    }
                    guest::store8(rd, masks + std::uint32_t(c), 0);
                    guest::store8(rd, columns + std::uint32_t(c), std::uint8_t((top << 4) | top));
                }
                std::fprintf(stdout, "{\"event\":\"research_lab\",\"lab\":\"stripes\",\"top\":%d,\"materials\":[%d,%d]}\n", top, mat[0], mat[1]);
                std::fflush(stdout);
                continue;
            }
            if (*command == "research_board" && guest_rdram != nullptr) {
                // Filled cells per layer (0 the core), with the count of each value on the top layers.
                const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u);
                std::string out = "{\"event\":\"research_board\",\"layers\":[";
                for (int layer = 0; layer < 9; ++layer) {
                    int filled = 0;
                    for (int c = 0; c < 1024; ++c) {
                        const std::int16_t v = std::int16_t(guest::load16(guest_rdram, values + 2u * std::uint32_t(layer * 1024 + c)));
                        filled += v > 0;
                    }
                    out += (layer ? "," : "") + std::to_string(filled);
                }
                // Power and crystal pieces: the per-cell attribute bytes (0x800E44D4), mask 0x60.
                const std::uint32_t attrs = guest::load32(guest_rdram, 0x800E44D4u);
                int power = 0, crystal = 0;
                for (int i = 0; i < 9 * 1024; ++i) {
                    const int a = guest::load8(guest_rdram, attrs + std::uint32_t(i)) & 0x60;
                    power += a == 0x40, crystal += a == 0x20;
                }
                out += "],\"power\":" + std::to_string(power) + ",\"crystal\":" + std::to_string(crystal) + "}";
                std::fprintf(stdout, "%s\n", out.c_str());
                std::fflush(stdout);
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
                // Every playfield layer (1 to 8), so a stage of many layers
                // clears down to the core too.
                for (int c = 0; c < 8 * 1024; ++c) {
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
                blockplanet::ui::set_list("chapters", training_lessons(done("rescue"), done("training_advanced"), magic_lessons_done(), new_lessons_done()));
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
                blockplanet::ui::set_list("stages", puzzle_list(profile_data.progress.puzzles_solved, times, moves, 100, unlock_all_testing));
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
            if (*command == "new_lesson:slide" || *command == "new_lesson:drop" || *command == "new_lesson:hearts" ||
                *command == "new_lesson:combos" || *command == "new_lesson:rubble") {
                hearts_lesson = *command == "new_lesson:hearts";
                combos_lesson = *command == "new_lesson:combos";
                rubble_lesson = *command == "new_lesson:rubble";
                // Slide and Drop: Casual 1:1 on its own, taken over once in play;
                // Drop (from the list) starts at the drop stage, the pair together.
                magic_lesson = -1, magic_lesson_phase = -1, drop_lesson_phase = -1;
                slide_drop_start = *command == "new_lesson:drop";
                slide_lesson_phase = 0;
                slide_lesson_at.reset();
                if (guest_rdram != nullptr) {
                    auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                    if (drop_lesson_saved_background < 0) drop_lesson_saved_background = guest::load16(rd, 0x800E94C4u);
                    guest::store16(rd, 0x800E94C4u, guest::load16(rd, 0x800E949Cu));
                }
                // Casual 1:1 started directly, not through Casual's episode list
                // (which showed if the start was refused).
                menu_lines = false;
                menu_variant = PlayVariant::Casual;
                campaign_source.clear();
                lesson_launch_pending = std::chrono::steady_clock::now();
                continue;
            }
            if (*command == "new_lesson:drop") {
                slide_lesson_phase = -1;
                // The Drop lesson: Casual 1:1 on its own, taken over once in play.
                magic_lesson = -1, magic_lesson_phase = -1;
                drop_lesson_phase = 0;
                drop_lesson_at.reset();
                if (guest_rdram != nullptr) {
                    auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                    if (drop_lesson_saved_background < 0) drop_lesson_saved_background = guest::load16(rd, 0x800E94C4u);
                    guest::store16(rd, 0x800E94C4u, guest::load16(rd, 0x800E949Cu));
                }
                blockplanet::ui::send_command("casual");
                blockplanet::ui::send_command("stage:1:1");
                continue;
            }
            if (command->rfind("magic_lesson:", 0) == 0) {
                // A magic lesson: Casual 1:1 on its own, taken over once in play.
                const int k = std::atoi(command->c_str() + 13);
                if (k >= 0 && k < 6) {
                    drop_lesson_phase = -1;
                    magic_lesson = k, magic_lesson_phase = 0;
                    magic_lesson_at.reset();
                    blockplanet::ui::send_command("casual");
                    blockplanet::ui::send_command("stage:1:1");
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
                    attack_episode_results.reset();
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
                attack_episode_results.reset();
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
        // A new lesson's stage: started as stage:1:1 does, again each frame
        // until the game takes it (10 s at most).
        if (lesson_launch_pending && game_option_applier && guest_rdram != nullptr) {
            const auto now_l = std::chrono::steady_clock::now();
            static std::optional<std::chrono::steady_clock::time_point> tried;
            if (launching() || slide_lesson_phase > 0) {
                // Started, or the lesson took over a stage already in play.
                lesson_launch_pending.reset(), tried.reset();
            } else if (now_l - *lesson_launch_pending > std::chrono::seconds(10)) {
                std::fprintf(stdout, "{\"event\":\"lesson_launch\",\"status\":\"gave_up\"}\n");
                std::fflush(stdout);
                lesson_launch_pending.reset(), tried.reset();
                show_error("The lesson could not start. Please try again.");
            } else if (!tried || now_l - *tried > std::chrono::milliseconds(500)) {
                if (tried) {
                    std::fprintf(stdout, "{\"event\":\"lesson_launch\",\"status\":\"retry\"}\n");
                    std::fflush(stdout);
                }
                tried = now_l;
                menu_lines = false;
                menu_variant = PlayVariant::Casual;
                continue_mode.reset();
                attack_episode_results.reset();
                single_stage = std::pair{1, 1};
                play_variant = menu_variant;
                single_stage_quit_at.reset();
                single_stage_result.reset();
                game_option_applier->choose_start(1, 1);
                launch_mode(LaunchMode::Rescue);
                if (launching()) lesson_launch_pending.reset(), tried.reset();
            }
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
            // A magic lesson in play (see magic_lesson).
            if (magic_lesson >= 0 && magic_lesson_phase >= 0) {
                const auto now_m = std::chrono::steady_clock::now();
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t pc = 0x80110220u;
                const bool in_play = guest::load16(rd, 0x800E121C) == -1 && guest::load16(rd, 0x800E44A8) == 1 &&
                                     std::int16_t(guest::load16(rd, 0x800E1228)) < 0 && std::int16_t(guest::load16(rd, pc + 0x2D7E)) >= 0;
                start_card_pending.reset();  // the lesson's own card instead
                // Left before the magic was used (Quit): the lesson ends there.
                {
                    static std::optional<std::chrono::steady_clock::time_point> out_since;
                    if (magic_lesson_phase >= 1 && magic_lesson_phase <= 3 && !pending_results &&
                        std::int16_t(guest::load16(rd, 0x800E121C)) >= 0) {
                        if (!out_since) out_since = now_m;
                        else if (now_m - *out_since > std::chrono::milliseconds(1500)) magic_lesson = -1, magic_lesson_phase = -1;
                    } else {
                        out_since.reset();
                    }
                }
                if (magic_lesson_phase == 0) {
                    if (!in_play || blockplanet::ui::menus_open()) magic_lesson_at.reset();
                    else if (!magic_lesson_at) magic_lesson_at = now_m;
                    else if (now_m - *magic_lesson_at > std::chrono::milliseconds(800)) {
                        blockplanet::ui::send_command("research_lab:stripes2:6:patch");
                        blockplanet::ui::set_text("chapters_title", "Training");  // the list it returns to
                        blockplanet::ui::set_text("chapters_subtitle", "Choose a lesson");
                        blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                               magic_lessons_done(), new_lessons_done()));
                        guest::store8(rd, pc + 0x702, std::uint8_t(magic_lesson));
                        // The core square needed to win raised to 8 x 8 (the
                        // largest), so no magic ends the lesson by opening it.
                        guest::store16(rd, pc + 0x23E4, 8);
                        magic_lesson_phase = 1;
                        start_card_requested = now_m;
                        pause_bridge.request_pause(now_m);
                    }
                } else if (magic_lesson_phase == 1) {
                    if (in_play && !blockplanet::ui::menus_open()) magic_lesson_phase = 2, magic_lesson_cells = count_bricks();
                } else if (magic_lesson_phase >= 2) {
                    // The drop timer held at its start while the lesson runs.
                    if (in_play) guest::store32(rd, pc + 0x2408, 0);
                    if (magic_lesson_phase == 2 && std::int8_t(guest::load8(rd, pc + 0x702)) < 0) {
                        magic_lesson_phase = 3;
                        magic_lesson_at = now_m;
                    }
                    // Gone without a blast (lost with a heart, say): given back
                    // once it is clear no bricks broke, to try again.
                    if (magic_lesson_phase == 3 && now_m - *magic_lesson_at > std::chrono::milliseconds(6000) && !pending_results &&
                        count_bricks() >= magic_lesson_cells) {
                        guest::store8(rd, pc + 0x702, std::uint8_t(magic_lesson));
                        magic_lesson_phase = 2;
                    } else if (magic_lesson_phase == 3 && now_m - *magic_lesson_at > std::chrono::milliseconds(6000) && !pending_results) {
                        // Done: marked in the profile, and the Training list
                        // (the menus it returns to) refreshed.
                        if (profile_save && profile_data.progress.chapters_completed["magic_lessons"].insert(magic_lesson).second) {
                            profile_dirty = true;
                            save_profile();
                        }
                        blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                               magic_lessons_done(), new_lessons_done()));
                        // The results to show once play is left, whichever way.
                        pending_results = StageResults{};
                        pending_results->lesson = magic_lesson;
                    }
                    // The magic keeps working up to about 7 s (the Ray Gun, the Atom).
                    if (magic_lesson_phase == 3 && now_m - *magic_lesson_at > std::chrono::milliseconds(9000) && !single_stage_quit_at &&
                        !single_stage_quitting) {
                        magic_lesson_phase = 4;
                        single_stage_quit_at = now_m;
                    }
                }
            }
            // The background table put back once the lesson's stage is in play
            // (or the lesson was dropped before it started).
            if (drop_lesson_saved_background >= 0 && drop_lesson_phase != 0 && slide_lesson_phase != 0) {
                guest::store16(const_cast<std::uint8_t*>(guest_rdram), 0x800E94C4u, std::uint16_t(drop_lesson_saved_background));
                drop_lesson_saved_background = -1;
            }
            // The Slide lesson in play (see slide_lesson_phase).
            lesson_no_drop.store((slide_lesson_phase >= 1 && slide_lesson_phase <= 6) ||  // the drop stage (10 on) drops
                                 // Crystals: no drop until every line is converted (the owner),
                                 // nor on the pages
                                 (rubble_lesson && (slide_lesson_phase == 41 || slide_lesson_phase == 42) && rubble_cells_left() > 0) ||
                                 (rubble_lesson && (slide_lesson_phase == 44 || slide_lesson_phase == 47)));
            lesson_no_slide.store(combos_lesson && slide_lesson_phase >= 30 && slide_lesson_phase <= 35);
            if (lesson_wait_release && (player_buttons_now() & 0xF000u) == 0) lesson_wait_release = false;
            // The shadow hidden while a lesson's text is read and the choice
            // waits (and after a step is done): shown only to watch or to play.
            tetrisphere::minefield_sphere::set_lesson_hides_marker(slide_lesson_phase == 1 || slide_lesson_phase == 6 || slide_lesson_phase == 10 ||
                                                                   slide_lesson_phase == 14 || slide_lesson_phase == 20 || slide_lesson_phase == 24 ||
                                                                   slide_lesson_phase == 30 || slide_lesson_phase == 34 ||
                                                                   slide_lesson_phase == 40 || slide_lesson_phase == 44 || slide_lesson_phase == 47 ||
                                                                   drop_lesson_phase == 1);
            // The stage's shape table and count put back once Hearts is over,
            // however it ended (quit, restart into another lesson, done).
            if (!((hearts_lesson || combos_lesson || rubble_lesson) && slide_lesson_phase >= 0) && guest_rdram != nullptr) {
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                if (hearts_table2_saved) guest::store32(rd, 0x8014CBC8u + 8u, hearts_table2_saved), hearts_table2_saved = 0;
                if (hearts_kinds_saved >= 0) guest::store16(rd, 0x800E44B8u, std::uint16_t(hearts_kinds_saved)), hearts_kinds_saved = -1;
            }
            // Casual's heart refill off while Hearts runs (the lesson keeps them
            // from running out), back on after.
            {
                static bool refill_off = false;
                const bool want_off = hearts_lesson && slide_lesson_phase >= 20 && slide_lesson_phase <= 25;
                if (want_off != refill_off && game_option_applier) {
                    refill_off = want_off;
                    game_option_applier->set_menu_casual(!want_off && play_variant == PlayVariant::Casual);
                }
            }
            if (slide_lesson_phase >= 0) {
                const auto now_m = std::chrono::steady_clock::now();
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t pc = 0x80110220u;
                const bool in_play = guest::load16(rd, 0x800E121C) == -1 && guest::load16(rd, 0x800E44A8) == 1 &&
                                     std::int16_t(guest::load16(rd, 0x800E1228)) < 0 && std::int16_t(guest::load16(rd, pc + 0x2D7E)) >= 0;
                const auto since = [&] {
                    return slide_lesson_at ? std::chrono::duration_cast<std::chrono::milliseconds>(now_m - *slide_lesson_at).count() : 0;
                };
                {
                    static int last = -9;
                    if (slide_lesson_phase != last) {
                        last = slide_lesson_phase;
                        char line[200];
                        std::snprintf(line, sizeof line, "{\"event\":\"lesson_step\",\"lesson\":\"%s\",\"phase\":%d,\"cursor\":[%d,%d],\"hearts\":%d}",
                                      hearts_lesson ? "hearts" : combos_lesson ? "combos" : rubble_lesson ? "rubble" : "slide_drop", last, 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)),
                                      8 + std::int16_t(guest::load16(rd, pc + 0x2B2E)), int(std::int16_t(guest::load16(rd, pc + 0x2D7E))));
                        // In the diagnostics file too, for the owner's reports (a
                        // possible hang at Hearts' Complete, 2026-11-20).
                        diagnostic_log().perf(line);
                        std::fprintf(stdout, "{\"event\":\"slide_phase\",\"phase\":%d,\"cursor\":[%d,%d],\"group\":%d,\"square\":%d}\n", last,
                                     8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E)),
                                     slide_biggest_group(), slide_square);
                        std::fflush(stdout);
                    }
                }
                // The board: the squares at their starts, the pit round them
                // (8 x 6), laid out round where the shadow is now (no jump, so
                // the camera does not swing round the sphere).
                const auto set_up = [&] {
                    slide_origin[0] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)) - slide_home[0];
                    slide_origin[1] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E)) - slide_home[1];
                    std::vector<std::pair<int, int>> origins;
                    for (const auto& st : slide_starts) origins.push_back({slide_origin[0] + st[0], slide_origin[1] + st[1]});
                    // The pit: the squares' cells (a 2 x 2 layout's cells sit at
                    // its centre plus 0 or 1, less one, per axis).
                    const std::uint32_t rec = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(slide_square));
                    int ax = 9, ay = 9;
                    for (int k = 0; k < 9; ++k)
                        if (std::int16_t(guest::load16(rd, rec + 0xA + 2u * std::uint32_t(k))) > 0) ax = std::min(ax, k / 3), ay = std::min(ay, k % 3);
                    build_pit_board(slide_origin[0] + ax - 1, slide_origin[1] + ay - 1 - 2, 8, 6, slide_square, origins);
                    guest::store16(rd, pc + 0x2D7E, 3);
                };
                start_card_pending.reset();
                {
                    static std::optional<std::chrono::steady_clock::time_point> out_since;
                    if (slide_lesson_shown() && !pending_results &&
                        std::int16_t(guest::load16(rd, 0x800E121C)) >= 0) {
                        if (!out_since) out_since = now_m;
                        else if (now_m - *out_since > std::chrono::milliseconds(1500)) slide_lesson_phase = -1;
                    } else {
                        out_since.reset();
                    }
                }
                if (slide_lesson_phase >= 1 && in_play) {
                    // No drop timer, but in Hearts while it is being shown or played:
                    // there the lesson's own (6 s in the demonstration, 8 s a piece in
                    // the player's turn), once the board is in place.
                    const bool timed = slide_lesson_phase == 22 || (slide_lesson_phase == 21 && hearts_demo_part == 0);
                    if (!timed || tetrisphere::minefield_sphere::game_thread_job_pending()) {
                        guest::store32(rd, pc + 0x2408, 0);
                        hearts_timer_from.reset();
                    } else {
                        if (!hearts_timer_from) hearts_timer_from = now_m;
                        const float seconds = slide_lesson_phase == 21 ? 6.0f : 8.0f;
                        const float done = std::chrono::duration<float>(now_m - *hearts_timer_from).count() / seconds;
                        const float value = std::min(6000.0f, 6000.0f * done);
                        std::uint32_t raw;
                        std::memcpy(&raw, &value, 4);
                        guest::store32(rd, pc + 0x2408, raw);
                    }
                    // The piece in hand always the square, so every square can be grabbed.
                    const int held = hearts_lesson ? hearts_shape() : slide_square;
                    if (rubble_lesson) {
                        // The piece in hand the round's shape; the drop timer held
                        // (the owner: the piece dropped by itself while thinking).
                        if (!tetrisphere::minefield_sphere::game_thread_job_pending()) {
                            const int shape = slide_lesson_phase >= 44 ? rubble_shape : rubble_round_shape();
                            for (int q = 0; q < 4; ++q) guest::store16(rd, pc + 0x2374 + 2u * std::uint32_t(q), std::uint16_t(shape));
                        }
                        guest::store32(rd, pc + 0x2408, 0);
                    } else if (combos_lesson) {
                        // The layers' shapes in order (the Next row shows what is coming).
                        if (!tetrisphere::minefield_sphere::game_thread_job_pending())
                            for (int q = 0; q < 4; ++q)
                                guest::store16(rd, pc + 0x2374 + 2u * std::uint32_t(q), std::uint16_t(combos_shapes[std::min(2, combos_step + q)]));
                    } else if (held >= 0)
                        for (int q = 0; q < 4; ++q) guest::store16(rd, pc + 0x2374 + 2u * std::uint32_t(q), std::uint16_t(held));
                }
                // The drop stage begins: the board kept as it is (the player's
                // pair), the shadow where it is.
                const auto enter_drop = [&] {
                    // The shadow off the squares: the board moved under it (the
                    // camera stays still) so it sits on open floor near the pair;
                    // done on the game's thread, then the board kept as it is.
                    tetrisphere::minefield_sphere::queue_game_thread_job([](std::uint8_t* g) {
                        const int cx = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2E));
                        int spot[2];
                        if (slide_free_spot(cx, cy, spot) && (spot[0] != cx || spot[1] != cy)) {
                            std::vector<std::uint8_t> board;
                            lesson_board_save(board);
                            lesson_board_restore(board, cx - spot[0], cy - spot[1]);
                        }
                        lesson_board_save(slide_drop_board);
                        slide_drop_cursor[0] = std::int16_t(guest::load16(g, pc_ctx + 0x2B2A));
                        slide_drop_cursor[1] = std::int16_t(guest::load16(g, pc_ctx + 0x2B2E));
                    });
                    slide_lesson_phase = 10, slide_lesson_at = now_m;
                    slide_drop_page = 0, slide_drop_watched = false, slide_choice_armed = false;
                };
                // The pair put back round where the shadow is now, as it stood
                // from the shadow when the stage began (the owner: the jump back
                // spun the sphere round).
                // Then the shadow on another open spot, chosen at random (the
                // owner: a bit of variety), the board moved under it again.
                const auto restore_drop = [&] {
                    tetrisphere::minefield_sphere::queue_game_thread_job([](std::uint8_t* g) {
                        const int dx = std::int16_t(guest::load16(g, pc_ctx + 0x2B2A)) - slide_drop_cursor[0];
                        const int dy = std::int16_t(guest::load16(g, pc_ctx + 0x2B2E)) - slide_drop_cursor[1];
                        lesson_board_restore(slide_drop_board, dx, dy);
                        const int cx = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(g, pc_ctx + 0x2B2E));
                        std::vector<std::pair<int, int>> spots;
                        int nearest[2];
                        slide_free_spot(cx, cy, nearest, &spots);
                        spots.erase(std::remove_if(spots.begin(), spots.end(),
                                                   [&](const auto& p) { return std::abs(p.first - cx) + std::abs(p.second - cy) < 2; }),
                                    spots.end());
                        if (!spots.empty()) {
                            static std::mt19937 pick(std::random_device{}());
                            const auto& spot = spots[std::uniform_int_distribution<std::size_t>(0, spots.size() - 1)(pick)];
                            std::vector<std::uint8_t> board;
                            lesson_board_save(board);
                            lesson_board_restore(board, cx - spot.first, cy - spot.second);
                            std::fprintf(stdout, "{\"event\":\"drop_spot\",\"cursor\":[%d,%d],\"spot\":[%d,%d],\"spots\":%zu,\"bricks\":%d}\n", cx, cy,
                                         spot.first, spot.second, spots.size(), count_bricks());
                            std::fflush(stdout);
                        }
                        guest::store16(g, pc_ctx + 0x2D7E, 3);
                    });
                };
                if (slide_lesson_phase == 0) {
                    if (!in_play || blockplanet::ui::menus_open()) slide_lesson_at.reset();
                    else if (!slide_lesson_at) slide_lesson_at = now_m;
                    else if (since() > 200) {
                        // The menus the lesson returns to: Training's list (the
                        // Casual stage it plays on set the list's titles).
                        blockplanet::ui::set_text("chapters_title", "Training");
                        blockplanet::ui::set_text("chapters_subtitle", "Choose a lesson");
                        blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                               magic_lessons_done(), new_lessons_done()));
                        // Away from the board's seam (cells 31 and 0, where it wraps
                        // round the back): the lesson's pit is laid round the
                        // shadow and moved with it, and a square left across the
                        // seam was removed by the game (a false miss). Moved
                        // while the sphere flies in, so the swing is not seen.
                        guest::store16(rd, pc + 0x2B2A, std::uint16_t(std::int16_t((hearts_lesson ? 10 : combos_lesson || rubble_lesson ? 12 : 16) - 8)));
                        slide_square = square_piece_index();
                        if (slide_square < 0) slide_square = 0;
                        guest::store16(rd, pc + 0x23E4, 16);  // no win from the open pit
                        for (int q = 0; q < 4; ++q) guest::store16(rd, pc + 0x2374 + 2u * std::uint32_t(q), std::uint16_t(slide_square));
                        if (rubble_lesson) {
                            if (!hearts_table2_saved) hearts_table2_saved = guest::load32(rd, 0x8014CBC8u + 8u);
                            guest::store32(rd, 0x8014CBC8u + 8u, guest::load32(rd, 0x800E4508u + 4u * 5u));  // the red T, as Hearts
                            if (hearts_kinds_saved < 0) hearts_kinds_saved = guest::load16(rd, 0x800E44B8u);
                            guest::store16(rd, 0x800E44B8u, 3);
                            rubble_round = 0, rubble_demos = 0;
                            rubble_build(1);
                            guest::store8(rd, pc + 0x702, 0xFF);  // no power-up held
                            slide_lesson_phase = 40, slide_lesson_at = now_m;
                            rubble_page = 0, rubble_watched = false, slide_choice_armed = false;
                        } else if (combos_lesson) {
                            if (!hearts_table2_saved) hearts_table2_saved = guest::load32(rd, 0x8014CBC8u + 8u);
                            guest::store32(rd, 0x8014CBC8u + 8u, guest::load32(rd, 0x800E4508u + 4u * 5u));  // the red T, as Hearts
                            if (hearts_kinds_saved < 0) hearts_kinds_saved = guest::load16(rd, 0x800E44B8u);
                            guest::store16(rd, 0x800E44B8u, 3);
                            combos_anchor[0] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A));  // the pit centred on the shadow
                            combos_anchor[1] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                            combos_step = 0;
                            combos_build_step();
                            guest::store16(rd, pc + 0x2AEC, 11);  // the magic meter: three combos from full
                            guest::store8(rd, pc + 0x702, 0xFF);   // no power-up held
                            slide_lesson_phase = 30, slide_lesson_at = now_m;
                            combos_page = 0, combos_watched = false, slide_choice_armed = false;
                        } else if (hearts_lesson) {
                            if (!hearts_table2_saved) hearts_table2_saved = guest::load32(rd, 0x8014CBC8u + 8u);
                            guest::store32(rd, 0x8014CBC8u + 8u, guest::load32(rd, 0x800E4508u + 4u * 5u));
                            if (hearts_kinds_saved < 0) hearts_kinds_saved = guest::load16(rd, 0x800E44B8u);
                            guest::store16(rd, 0x800E44B8u, 3);
                            hearts_round = 0;
                            hearts_new_pair();
                            lesson_refill_hearts(rd);
                            slide_lesson_phase = 20, slide_lesson_at = now_m;
                            hearts_page = 0, hearts_watched = false, slide_choice_armed = false;
                        } else if (slide_drop_start) {
                            // Drop chosen from the list: the pair already together,
                            // the shadow on the floor beside them.
                            slide_origin[0] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)) - 6;
                            slide_origin[1] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                            std::vector<std::pair<int, int>> origins;
                            for (const auto& f : slide_finals) origins.push_back({slide_origin[0] + f[0], slide_origin[1] + f[1]});
                            const std::uint32_t rec = guest::load32(rd, 0x8014CBC8u + 4u * std::uint32_t(slide_square));
                            int ax = 9, ay = 9;
                            for (int k = 0; k < 9; ++k)
                                if (std::int16_t(guest::load16(rd, rec + 0xA + 2u * std::uint32_t(k))) > 0) ax = std::min(ax, k / 3), ay = std::min(ay, k % 3);
                            build_pit_board(slide_origin[0] + ax - 1, slide_origin[1] + ay - 1 - 2, 8, 6, slide_square, origins);
                            guest::store16(rd, pc + 0x2D7E, 3);
                            enter_drop();
                        } else {
                            set_up();
                            slide_lesson_phase = 1, slide_lesson_at = now_m;
                            slide_watched = false, slide_choice_armed = false, slide_page = 0;
                        }
                    }
                } else if (slide_lesson_phase == 1) {
                    const std::uint16_t b = player_buttons_now();
                    if (!in_play || blockplanet::ui::menus_open()) slide_choice_armed = false;
                    else if (!slide_choice_armed) slide_choice_armed = (b & 0xF000u) == 0;
                    else if (b & 0x1000u) slide_choice_armed = false, pause_bridge.request_pause(now_m);
                    else if (slide_page < slide_last_page) {
                        if (b & 0x8000u) ++slide_page, slide_choice_armed = false;  // the next line, or page
                    } else if (b & 0x8000u) slide_lesson_phase = 4, slide_lesson_at = now_m, lesson_wait_release = true;
                    else if (b & 0x4000u) {
                        slide_lesson_phase = 2, slide_lesson_at = now_m;
                        slide_demo_step = 0, slide_demo_stage = 0, slide_demo_slot = -1, slide_demo_held = 0;
                        slide_demo_stage_at.reset();
                    }
                } else if (slide_lesson_phase == 2) {
                    // Done once every square has been moved (or after 60 s),
                    // then a moment to look (the owner: it went too soon), and
                    // the board set up again.
                    static std::optional<std::chrono::steady_clock::time_point> done_at;
                    if (slide_demo_step >= slide_moves || since() > 60000) {
                        if (!done_at) {
                            done_at = now_m;
                            std::fprintf(stdout, "{\"event\":\"slide_demo_done\",\"group\":%d,\"ms\":%lld}\n", slide_biggest_group(), (long long)since());
                            std::fflush(stdout);
                        }
                        else if (now_m - *done_at > std::chrono::milliseconds(3500)) {
                            done_at.reset();
                            set_up();
                            slide_lesson_phase = 1, slide_lesson_at = now_m;
                            slide_watched = true, slide_choice_armed = false, slide_page = slide_last_page;
                        }
                    }
                } else if (slide_lesson_phase == 4) {
                    // Done when all six touch, held for a moment with B let go.
                    slide_group = slide_biggest_group();
                    static std::optional<std::chrono::steady_clock::time_point> together;
                    if (slide_group >= slide_pieces && !(player_buttons_now() & 0x4000u)) {
                        if (!together) together = now_m;
                        else if (now_m - *together > std::chrono::milliseconds(600)) {
                            together.reset();
                            slide_lesson_phase = 6, slide_lesson_at = now_m;
                        }
                    } else {
                        together.reset();
                    }
                } else if (slide_lesson_phase == 6) {
                    // Slid together: on to the drop stage on the same board.
                    if (since() > 2500) {
                        if (profile_save && profile_data.progress.chapters_completed["lessons"].insert(101).second) {
                            profile_dirty = true;
                            save_profile();
                        }
                        enter_drop();
                    }
                } else if (slide_lesson_phase == 10) {
                    const std::uint16_t b = player_buttons_now();
                    if (!in_play || blockplanet::ui::menus_open()) slide_choice_armed = false;
                    else if (!slide_choice_armed) slide_choice_armed = (b & 0xF000u) == 0;
                    else if (b & 0x1000u) slide_choice_armed = false, pause_bridge.request_pause(now_m);
                    else if (slide_drop_page < 1) {
                        if (b & 0x8000u) ++slide_drop_page, slide_choice_armed = false;
                    } else if (b & 0x8000u) {
                        lesson_wait_release = true;
                        slide_lesson_phase = 12, slide_lesson_at = now_m;
                        slide_drop_bricks = -1;
                    } else if (b & 0x4000u) {
                        const int cx = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), cy = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                        if (!slide_nearest_square(cx, cy, slide_drop_target)) slide_drop_target[0] = cx, slide_drop_target[1] = cy;
                        slide_lesson_phase = 11, slide_lesson_at = now_m;
                        slide_drop_aimed = false, slide_drop_dropped = false, slide_drop_slot = -1, slide_drop_held = 0;
                        slide_drop_dropped_at.reset();
                    }
                } else if (slide_lesson_phase == 11) {
                    // Dropped: all three break; a pause to look (the owner),
                    // then the pair put back for the player.
                    if (slide_drop_dropped && slide_drop_dropped_at && now_m - *slide_drop_dropped_at > std::chrono::milliseconds(4500) &&
                        guest::load32(rd, pc + 0x2AFC) == 0) {
                        restore_drop();
                        slide_lesson_phase = 10, slide_lesson_at = now_m;
                        slide_drop_watched = true, slide_choice_armed = false, slide_drop_page = 1;
                    } else if (since() > 20000) {
                        restore_drop();
                        slide_lesson_phase = 10, slide_lesson_at = now_m, slide_choice_armed = false, slide_drop_page = 1;
                    }
                } else if (slide_lesson_phase == 12) {
                    // A landing scores a moment later: none after 1.5 s is a miss.
                    // (The board's counts taken once its reset has been done.)
                    static std::optional<std::chrono::steady_clock::time_point> landed;
                    if (slide_drop_bricks < 0) {
                        if (!tetrisphere::minefield_sphere::game_thread_job_pending())
                            slide_drop_score = guest::load32(rd, pc + 0x2AF8), slide_drop_bricks = count_bricks();
                    } else if (guest::load32(rd, pc + 0x2AF8) != slide_drop_score) {
                        landed.reset();
                        slide_lesson_phase = 14, slide_lesson_at = now_m;
                    } else if (count_bricks() != slide_drop_bricks) {
                        if (!landed) landed = now_m;
                        else if (now_m - *landed > std::chrono::milliseconds(1500)) {
                            landed.reset(), slide_lesson_phase = 13, slide_lesson_at = now_m;
                            // Research: where the board's bricks are at a miss.
                            std::string cells;
                            const std::uint32_t values = guest::load32(rd, 0x800E44D0u);
                            for (int layer = 1; layer < 9; ++layer)
                                for (int c = 0; c < 1024; ++c) {
                                    const int v = std::int16_t(guest::load16(rd, values + 2u * std::uint32_t(layer * 1024 + c)));
                                    if (v > 0 && v != 423) cells += (cells.empty() ? "" : ",") + std::string("[") + std::to_string(layer) + "," +
                                                                     std::to_string(c % 32) + "," + std::to_string(c / 32) + "," + std::to_string(v) + "]";
                                }
                            std::fprintf(stdout, "{\"event\":\"drop_miss_board\",\"was\":%d,\"now\":%d,\"cells\":[%s]}\n", slide_drop_bricks, count_bricks(),
                                         cells.c_str());
                            std::fflush(stdout);
                        }
                    } else {
                        landed.reset();
                    }
                } else if (slide_lesson_phase == 13) {
                    if (since() > 2500) {
                        restore_drop();
                        slide_lesson_phase = 12, slide_lesson_at = now_m;
                        slide_drop_bricks = -1;
                    }
                } else if (slide_lesson_phase == 14) {
                    if (since() > 3500 && !pending_results) {
                        if (profile_save) {
                            auto& done = profile_data.progress.chapters_completed["lessons"];
                            if (done.insert(100).second | done.insert(101).second) profile_dirty = true, save_profile();
                        }
                        blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                               magic_lessons_done(), new_lessons_done()));
                        pending_results = StageResults{};
                        pending_results->lesson = 102;
                        slide_lesson_phase = 15;
                        if (!single_stage_quit_at && !single_stage_quitting) single_stage_quit_at = now_m;
                    }
                } else if (slide_lesson_phase == 20) {
                    const std::uint16_t b = player_buttons_now();
                    if (!in_play || blockplanet::ui::menus_open()) slide_choice_armed = false;
                    else if (!slide_choice_armed) slide_choice_armed = (b & 0xF000u) == 0;
                    else if (b & 0x1000u) slide_choice_armed = false, pause_bridge.request_pause(now_m);
                    else if (hearts_page < hearts_last_page) {
                        if (b & 0x8000u) ++hearts_page, slide_choice_armed = false;
                    } else if (b & 0x8000u) {
                        lesson_wait_release = true;
                        hearts_good = 0, hearts_note.clear(), hearts_bricks = -1, hearts_round = 0;
                        hearts_new_pair();
                        guest::store32(rd, pc + 0x2408, 0);
                        slide_lesson_phase = 22, slide_lesson_at = now_m;
                    } else if (b & 0x4000u) {
                        hearts_demo_part = 0, hearts_demo_at = now_m, hearts_bricks = count_bricks(), hearts_before = std::int16_t(guest::load16(rd, pc + 0x2D7E));
                        guest::store32(rd, pc + 0x2408, 0);
                        slide_lesson_phase = 21, slide_lesson_at = now_m;
                    }
                } else if (slide_lesson_phase == 21) {
                    const int hearts = std::int16_t(guest::load16(rd, pc + 0x2D7E));
                    const auto part_ms = hearts_demo_at ? std::chrono::duration_cast<std::chrono::milliseconds>(now_m - *hearts_demo_at).count() : 0;
                    {
                        static int last_b = -1, last_h = -9;
                        static float last_t = -1;
                        const int b = count_bricks();
                        float t;
                        const std::uint32_t raw = guest::load32(rd, pc + 0x2408);
                        std::memcpy(&t, &raw, 4);
                        static auto last_at = std::chrono::steady_clock::now();
                        if (b != last_b || hearts != last_h || now_m - last_at > std::chrono::milliseconds(700)) {
                            last_at = now_m;
                            std::fprintf(stdout, "{\"event\":\"hearts_demo\",\"part\":%d,\"bricks\":%d,\"hearts\":%d,\"timer\":%.1f,\"shown\":%d}\n", hearts_demo_part, b, hearts, t,
                                         int(std::int16_t(guest::load16(rd, pc + 0x2B10))));
                            std::fflush(stdout);
                            last_b = b, last_h = hearts, last_t = t;
                        }
                    }
                    if (hearts_demo_part == 0) {
                        // The bar runs out with the shadow on open floor: the
                        // piece drops by itself, matches nothing, a heart goes.
                        if (hearts < hearts_before || part_ms > 15000) hearts_demo_part = 1, hearts_demo_at = now_m;
                    } else if (hearts_demo_part == 1) {
                        if (part_ms > 3000 && !tetrisphere::minefield_sphere::game_thread_job_pending()) {
                            hearts_new_pair();
                            hearts_demo_part = 2, hearts_demo_at = now_m, hearts_slot = -1, hearts_held = 0, hearts_aimed = false;
                            hearts_aim_pending = true;
                        }
                    } else if (hearts_demo_part == 2) {
                        // Aimed once the new pair is in place.
                        if (hearts_aim_pending && !tetrisphere::minefield_sphere::game_thread_job_pending()) {
                            hearts_aim_pending = false;  // the gap, set as the pair was built
                            hearts_demo_at = now_m;
                        }
                        if (part_ms > 15000) hearts_demo_part = 3, hearts_demo_at = now_m;
                    } else if (part_ms > 4000 && guest::load32(rd, pc + 0x2AFC) == 0) {
                        hearts_round = 0;
                        hearts_new_pair();
                        lesson_refill_hearts(rd);
                        slide_lesson_phase = 20, slide_lesson_at = now_m;
                        hearts_watched = true, slide_choice_armed = false, hearts_page = hearts_last_page;
                    }
                } else if (slide_lesson_phase == 22) {
                    // Three good drops before the bar runs out; a drop that
                    // matches nothing (the player's, or the timer's) costs a heart.
                    const int hearts = std::int16_t(guest::load16(rd, pc + 0x2D7E));
                    if (hearts_bricks < 0) {
                        if (!tetrisphere::minefield_sphere::game_thread_job_pending())
                            hearts_score = guest::load32(rd, pc + 0x2AF8), hearts_bricks = count_bricks(), hearts_before = hearts;
                    } else if (guest::load32(rd, pc + 0x2AF8) != hearts_score) {
                        ++hearts_good;
                        hearts_round = std::min(2, hearts_good);  // the next shape
                        hearts_note = hearts_good < hearts_goal ? "Good drop! Now another shape." : "Good drop: all three broke.";
                        slide_lesson_phase = 23, slide_lesson_at = now_m;
                    } else if (hearts < hearts_before) {
                        if (hearts <= 0) {
                            hearts_note = "Out of hearts! In a game, that's game over. Let's start again.";
                            hearts_good = 0, hearts_round = 0;
                        } else {
                            hearts_note = "No match there, so a heart was lost.";
                        }
                        slide_lesson_phase = 23, slide_lesson_at = now_m;
                    }
                } else if (slide_lesson_phase == 23) {
                    if (since() > 2500 && guest::load32(rd, pc + 0x2AFC) == 0) {
                        if (hearts_good >= hearts_goal) {
                            slide_lesson_phase = 24, slide_lesson_at = now_m;
                        } else {
                            if (std::int16_t(guest::load16(rd, pc + 0x2D7E)) <= 0) lesson_refill_hearts(rd);
                            hearts_new_pair();
                            hearts_bricks = -1;
                            slide_lesson_phase = 22, slide_lesson_at = now_m;
                        }
                    }
                } else if (slide_lesson_phase == 24) {
                    if (since() > 3500 && !pending_results) {
                        if (profile_save && profile_data.progress.chapters_completed["lessons"].insert(103).second) profile_dirty = true, save_profile();
                        blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                               magic_lessons_done(), new_lessons_done()));
                        pending_results = StageResults{};
                        pending_results->lesson = 103;
                        slide_lesson_phase = 25;
                        if (!single_stage_quit_at && !single_stage_quitting) single_stage_quit_at = now_m;
                    }
                } else if (slide_lesson_phase == 30 || slide_lesson_phase == 34) {
                    // The pages (A on), the choice (A try it, B show me), and the
                    // ending's two lines.
                    const std::uint16_t b = player_buttons_now();
                    if (!in_play || blockplanet::ui::menus_open()) slide_choice_armed = false;
                    else if (!slide_choice_armed) slide_choice_armed = (b & 0xF000u) == 0;
                    else if (b & 0x1000u) slide_choice_armed = false, pause_bridge.request_pause(now_m);
                    else if (slide_lesson_phase == 34) {
                        if (b & 0x8000u) {
                            slide_choice_armed = false;
                            if (combos_end_page < 1) ++combos_end_page;
                            else if (!pending_results) {
                                if (profile_save && profile_data.progress.chapters_completed["lessons"].insert(104).second) profile_dirty = true, save_profile();
                                blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                                       magic_lessons_done(), new_lessons_done()));
                                pending_results = StageResults{};
                                pending_results->lesson = 104;
                                slide_lesson_phase = 35;
                                if (!single_stage_quit_at && !single_stage_quitting) single_stage_quit_at = now_m;
                            }
                        }
                    } else if (combos_page < combos_last_page) {
                        if (b & 0x8000u) ++combos_page, slide_choice_armed = false;
                    } else if (b & 0x8000u) {
                        lesson_wait_release = true;
                        combos_bricks = -1, combos_note.clear();
                        slide_lesson_phase = 32, slide_lesson_at = now_m;
                    } else if (b & 0x4000u) {
                        combos_dropped = false, combos_aimed = false, combos_slot = -1, combos_held = 0, combos_at = now_m;
                        slide_lesson_phase = 31, slide_lesson_at = now_m;
                    }
                } else if (slide_lesson_phase == 31 || slide_lesson_phase == 32) {
                    // A drop on the group breaks six and tops up the meter; then a
                    // moment, and the next layer down. A drop away from it: the
                    // layer set up again.
                    const int meter = guest::load16(rd, pc + 0x2AEC);
                    static std::optional<std::chrono::steady_clock::time_point> landed;
                    if (combos_bricks < 0) {
                        if (!tetrisphere::minefield_sphere::game_thread_job_pending())
                            combos_bricks = count_bricks(), combos_score = guest::load32(rd, pc + 0x2AF8), combos_meter_before = meter;
                    } else if (guest::load32(rd, pc + 0x2AF8) != combos_score || guest::load32(rd, pc + 0x2AFC) != 0) {
                        // (A combo of six takes a while to score: its start counts.)
                        landed.reset();
                        combos_note.clear();
                        combos_at = now_m;
                        slide_lesson_phase = 33, slide_lesson_at = now_m;
                    } else if (count_bricks() != combos_bricks) {
                        if (!landed) landed = now_m;
                        else if (now_m - *landed > std::chrono::milliseconds(2500)) {
                            landed.reset();
                            combos_note = "No match there, so the piece stacked up. Drop it on the matching stack.";
                            std::fprintf(stdout, "{\"event\":\"combos_miss\",\"step\":%d,\"hand\":%d,\"queue0\":%d,\"cursor\":[%d,%d],\"target\":[%d,%d]}\n",
                                         combos_step, int(std::int16_t(guest::load16(rd, pc + 0x2386))), int(std::int16_t(guest::load16(rd, pc + 0x2374))),
                                         8 + std::int16_t(guest::load16(rd, pc + 0x2B2A)), 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E)), combos_target[0], combos_target[1]);
                            std::fflush(stdout);
                            combos_build_step();
                            combos_bricks = -1;
                        }
                    }
                    combos_demo_running = slide_lesson_phase == 33 ? combos_demo_running : slide_lesson_phase == 31;
                } else if (slide_lesson_phase == 33) {
                    // The combo plays out; then the next layer, or the end.
                    if (since() > 2500 && guest::load32(rd, pc + 0x2AFC) == 0) {
                        if (combos_step < 2) {
                            ++combos_step;
                            combos_build_step();
                            combos_bricks = -1;
                            combos_dropped = false, combos_aimed = false, combos_slot = -1, combos_held = 0, combos_at = now_m;
                            slide_lesson_phase = combos_demo_running ? 31 : 32, slide_lesson_at = now_m;
                        } else if (combos_demo_running) {
                            // The demonstration done: a moment with the meter full and the
                            // power-up shown, then all set up again for the player.
                            if (since() > 5000) {
                                combos_step = 0;
                                combos_build_step();
                                guest::store16(rd, pc + 0x2AEC, 11);
                                guest::store8(rd, pc + 0x702, 0xFF);
                                combos_demo_running = false;
                                slide_lesson_phase = 30, slide_lesson_at = now_m;
                                combos_watched = true, slide_choice_armed = false, combos_page = combos_last_page;
                            }
                        } else {
                            combos_end_page = 0;
                            slide_lesson_phase = 34, slide_lesson_at = now_m, slide_choice_armed = false;
                        }
                    }
                } else if (slide_lesson_phase == 40 || slide_lesson_phase == 44 || slide_lesson_phase == 47) {
                    // Rubble and Mystery Blocks: the pages (A on), the choices (A
                    // try it, B show me), and the ending's two lines.
                    const std::uint16_t b = player_buttons_now();
                    const bool part2 = slide_lesson_phase == 44;
                    int& page = slide_lesson_phase == 47 ? rubble_end_page : rubble_page;
                    const int last = slide_lesson_phase == 47 ? 1 : part2 ? rubble_last_page2 : rubble_last_page;
                    if (!in_play || blockplanet::ui::menus_open()) slide_choice_armed = false;
                    else if (!slide_choice_armed) slide_choice_armed = (b & 0xF000u) == 0;
                    else if (b & 0x1000u) slide_choice_armed = false, pause_bridge.request_pause(now_m);
                    else if (page < last) {
                        if (b & 0x8000u) ++page, slide_choice_armed = false;
                    } else if (slide_lesson_phase == 47) {
                        if ((b & 0x8000u) && !pending_results) {
                            if (profile_save && profile_data.progress.chapters_completed["lessons"].insert(105).second) profile_dirty = true, save_profile();
                            blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                                   magic_lessons_done(), new_lessons_done()));
                            pending_results = StageResults{};
                            pending_results->lesson = 105;
                            slide_lesson_phase = 48;
                            if (!single_stage_quit_at && !single_stage_quitting) single_stage_quit_at = now_m;
                        }
                    } else if (b & 0x8000u) {
                        lesson_wait_release = true;
                        rubble_bricks = -1, rubble_note.clear(), rubble_at = now_m, rubble_saw_combo = false;
                        slide_lesson_phase = part2 ? 46 : 42, slide_lesson_at = now_m;
                    } else if (b & 0x4000u) {
                        lesson_wait_release = true;
                        rubble_bricks = -1, rubble_note.clear(), rubble_at = now_m;
                        rubble_step = 0, rubble_slot = -1, rubble_pushes = 0, rubble_aimed = false, rubble_held = 0;
                        rubble_demo_running = true, rubble_saw_combo = false;
                        slide_lesson_phase = part2 ? 45 : 41, slide_lesson_at = now_m;
                    }
                } else if (slide_lesson_phase == 41 || slide_lesson_phase == 42) {
                    // Part 1: a combo started (pieces breaking: the counter at
                    // +0x2AE0; a conversion scores, so not the points) goes on to
                    // 43. A drop that lands without one: the board set up again.
                    static std::optional<std::chrono::steady_clock::time_point> landed;
                    {
                        static int last_b = -1, last_c = -1;
                        const int b = count_bricks(), c = rubble_cells_left();
                        if (b != last_b || c != last_c) {
                            last_b = b, last_c = c;
                            std::fprintf(stdout, "{\"event\":\"rubble_board\",\"bricks\":%d,\"crystals\":%d,\"start\":%d}\n", b, c, rubble_bricks);
                            std::fflush(stdout);
                        }
                    }
                    if (rubble_bricks < 0) {
                        if (!tetrisphere::minefield_sphere::game_thread_job_pending()) rubble_bricks = count_bricks();
                    } else if (guest::load16(rd, pc + 0x2AE0) != 0) {
                        landed.reset();
                        rubble_note.clear();
                        slide_lesson_phase = 43, slide_lesson_at = now_m;
                    } else if (count_bricks() != rubble_bricks) {
                        // (Held for 2.5 s: the count also jumps for a moment as the
                        // game works, which is not a landing.)
                        if (!landed) landed = now_m;
                        else if (now_m - *landed > std::chrono::milliseconds(2500)) {
                            landed.reset();
                            rubble_note = "No match there, so the piece stacked up. Drop on the pieces once the crystals are converted.";
                            rubble_build(1);
                            rubble_bricks = -1;
                        }
                    } else if (landed) {
                        landed.reset();  // back as it was: no landing
                    } else if (slide_lesson_phase == 41 && since() > 30000) {
                        rubble_build(1);
                        rubble_demo_running = false;
                        slide_lesson_phase = 40, slide_lesson_at = now_m, slide_choice_armed = false;
                    }
                } else if (slide_lesson_phase == 43) {
                    // The combo plays out; its size (+0x2B14) says whether both
                    // lines were converted first (all five and the dropped piece).
                    if (since() > 2500 && guest::load16(rd, pc + 0x2AE0) == 0 && guest::load32(rd, pc + 0x2AFC) == 0) {
                        if (rubble_combo != std::int16_t(guest::load16(rd, pc + 0x2B14)) || since() < 2600) {
                            rubble_combo = std::int16_t(guest::load16(rd, pc + 0x2B14));
                            std::fprintf(stdout, "{\"event\":\"rubble_combo\",\"combo\":%d,\"demo\":%d}\n", rubble_combo, int(rubble_demo_running));
                            std::fflush(stdout);
                        }
                        if (rubble_combo < rubble_groups() + 1) {
                            rubble_note = "Only " + std::to_string(rubble_combo) + " broke. Convert every line of crystals first, then drop.";
                            rubble_build(1);
                            rubble_bricks = -1;
                            slide_lesson_phase = rubble_demo_running ? 40 : 42, slide_lesson_at = now_m;
                            rubble_demo_running = false;
                        } else if (rubble_demo_running) {
                            if (since() > 4000) {
                                rubble_build(1);
                                rubble_demo_running = false;
                                slide_lesson_phase = 40, slide_lesson_at = now_m;
                                rubble_watched = true, slide_choice_armed = false, rubble_page = rubble_last_page;
                            }
                        } else if (since() > 4000 && rubble_round < 2) {
                            // Again with the next shape: blue squares, then red Ts.
                            ++rubble_round;
                            rubble_note = rubble_round == 1 ? "Well done! Now with blue squares." : "Well done! Now with red Ts.";
                            rubble_build(1);
                            rubble_bricks = -1;
                            slide_lesson_phase = 42, slide_lesson_at = now_m;
                        } else if (since() > 4000) {
                            // On to the mystery block, on a new board.
                            rubble_build(2);
                            guest::store8(rd, pc + 0x702, 0xFF);
                            rubble_page = 0, rubble_watched = false, rubble_note.clear();
                            slide_lesson_phase = 44, slide_lesson_at = now_m, slide_choice_armed = false;
                        }
                    }
                } else if (slide_lesson_phase == 45 || slide_lesson_phase == 46) {
                    // Part 2: done once a power-up is held (+0x702), with a moment
                    // to see it; a demonstration then sets the board up again.
                    static std::optional<std::chrono::steady_clock::time_point> found;
                    const bool held = std::int8_t(guest::load8(rd, pc + 0x702)) >= 0;
                    if (held) {
                        if (!found) {
                            found = now_m;
                            std::fprintf(stdout, "{\"event\":\"rubble_found\",\"magic\":%d,\"demo\":%d}\n", int(guest::load8(rd, pc + 0x702)),
                                         int(slide_lesson_phase == 45));
                            std::fflush(stdout);
                        } else if (now_m - *found > std::chrono::milliseconds(slide_lesson_phase == 45 ? 4000 : 3000) &&
                                   !(player_buttons_now() & 0x4000u)) {
                            found.reset();
                            if (slide_lesson_phase == 45) {
                                rubble_build(2);
                                guest::store8(rd, pc + 0x702, 0xFF);
                                rubble_demo_running = false, ++rubble_demos;
                                slide_lesson_phase = 44, slide_lesson_at = now_m;
                                rubble_watched = true, slide_choice_armed = false, rubble_page = rubble_last_page2;
                            } else {
                                rubble_end_page = 0;
                                slide_lesson_phase = 47, slide_lesson_at = now_m, slide_choice_armed = false;
                            }
                        }
                    } else {
                        // Either way works (the owner): convert and drop for a combo
                        // beside the mystery block, or slide through. A combo that
                        // ends with no power-up, or a drop on nothing: set up again.
                        static std::optional<std::chrono::steady_clock::time_point> landed;
                        const bool combo_now = guest::load16(rd, pc + 0x2AE0) != 0 || guest::load32(rd, pc + 0x2AFC) != 0;
                        if (rubble_bricks < 0) {
                            if (!tetrisphere::minefield_sphere::game_thread_job_pending()) rubble_bricks = count_bricks();
                        } else if (guest::load16(rd, pc + 0x2AE0) != 0) {
                            rubble_saw_combo = true, landed.reset();
                        }
                        const bool settle = rubble_bricks >= 0 && !combo_now && !(player_buttons_now() & 0x4000u);
                        const bool missed_drop = settle && !rubble_saw_combo && count_bricks() > rubble_bricks;
                        if (!((settle && rubble_saw_combo) || missed_drop)) landed.reset();
                        if ((settle && rubble_saw_combo) || missed_drop) {
                            if (!landed) landed = now_m;
                            else if (now_m - *landed > std::chrono::milliseconds(2500)) {
                                landed.reset();
                                rubble_note = rubble_saw_combo ? "That combo wasn't next to the mystery block. Try again."
                                                               : "No match there, so the piece stacked up. Try again.";
                                rubble_build(2);
                                rubble_bricks = -1, rubble_saw_combo = false;
                            }
                        }
                        // Stuck (a slide stopped short, crystals converted but no drop):
                        // the board changed and nothing pressed for 15 s.
                        static std::optional<std::chrono::steady_clock::time_point> last_input;
                        static int crystals_at_start = -1;
                        if (rubble_bricks < 0 || !last_input) last_input = now_m, crystals_at_start = rubble_cells_left();
                        if (player_buttons_now() & 0xFF00u) last_input = now_m;
                        const bool changed = rubble_cells_left() != crystals_at_start;
                        if (slide_lesson_phase == 46 && changed && !combo_now && now_m - *last_input > std::chrono::seconds(15)) {
                            landed.reset(), last_input.reset();
                            rubble_note = "Let's set that up again.";
                            rubble_build(2);
                            rubble_bricks = -1, rubble_saw_combo = false;
                        } else if (since() > (slide_lesson_phase == 45 ? 20000 : 600000) && !(player_buttons_now() & 0x4000u)) {
                            landed.reset();
                            rubble_build(2);
                            rubble_bricks = -1, rubble_saw_combo = false;
                            rubble_demo_running = false;
                            if (slide_lesson_phase == 45) {
                                ++rubble_demos;
                                slide_lesson_phase = 44, slide_lesson_at = now_m, slide_choice_armed = false;
                            } else {
                                slide_lesson_at = now_m;
                            }
                        }
                    }
                }
            }
            // The Drop lesson in play (see drop_lesson_phase).
            if (drop_lesson_phase >= 0) {
                {
                    static int last = -9;
                    if (drop_lesson_phase != last) {
                        last = drop_lesson_phase;
                        std::fprintf(stdout, "{\"event\":\"drop_phase\",\"phase\":%d,\"cursor\":[%d,%d],\"score\":%u,\"bricks\":%d}\n", last,
                                     8 + std::int16_t(guest::load16(guest_rdram, 0x80112D4A)), 8 + std::int16_t(guest::load16(guest_rdram, 0x80112D4E)),
                                     guest::load32(guest_rdram, 0x80112D18), count_bricks());
                        std::fflush(stdout);
                    }
                }
                const auto now_m = std::chrono::steady_clock::now();
                auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                const std::uint32_t pc = 0x80110220u;
                const bool in_play = guest::load16(rd, 0x800E121C) == -1 && guest::load16(rd, 0x800E44A8) == 1 &&
                                     std::int16_t(guest::load16(rd, 0x800E1228)) < 0 && std::int16_t(guest::load16(rd, pc + 0x2D7E)) >= 0;
                const auto since = [&] {
                    return drop_lesson_at ? std::chrono::duration_cast<std::chrono::milliseconds>(now_m - *drop_lesson_at).count() : 0;
                };
                // The board for a go: the cursor home, the hole and the pair at
                // the target, the hearts full, nothing breaking.
                // Round the shadow where it is (no jump: the camera stays still;
                // while it swung, part of the pit's floor went undrawn).
                const auto set_up = [&](int dx, int dy) {
                    drop_lesson_home[0] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A));
                    drop_lesson_home[1] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                    drop_lesson_target[0] = drop_lesson_home[0] + dx, drop_lesson_target[1] = drop_lesson_home[1] + dy;
                    static const int pit = [] { const char* v = std::getenv("BLOCKPLANET_DROP_PIT"); return v ? std::atoi(v) : -1; }();
                    build_pair_board(drop_lesson_target[0], drop_lesson_target[1], -1, -1, 1, 1, pit, drop_lesson_home[0], drop_lesson_home[1]);
                    guest::store16(rd, pc + 0x2D7E, 3);
                    drop_lesson_bricks = count_bricks();
                    drop_lesson_score = guest::load32(rd, pc + 0x2AF8);
                };
                start_card_pending.reset();  // the lesson's own card instead
                // Left before the end (Quit): the lesson ends there.
                {
                    static std::optional<std::chrono::steady_clock::time_point> out_since;
                    if (drop_lesson_phase >= 1 && drop_lesson_phase <= 5 && !pending_results && std::int16_t(guest::load16(rd, 0x800E121C)) >= 0) {
                        if (!out_since) out_since = now_m;
                        else if (now_m - *out_since > std::chrono::milliseconds(1500)) drop_lesson_phase = -1;
                    } else {
                        out_since.reset();
                    }
                }
                if (drop_lesson_phase >= 1 && in_play) guest::store32(rd, pc + 0x2408, 0);  // no drop timer in this lesson
                if (drop_lesson_phase == 0) {
                    if (!in_play || blockplanet::ui::menus_open()) drop_lesson_at.reset();
                    else if (!drop_lesson_at) drop_lesson_at = now_m;
                    else if (since() > 200) {  // the board at once (the stage's own sphere barely seen)
                        drop_lesson_home[0] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2A));
                        drop_lesson_home[1] = 8 + std::int16_t(guest::load16(rd, pc + 0x2B2E));
                        static const int square = [] { const char* v = std::getenv("BLOCKPLANET_DROP_SQUARE"); return v ? std::atoi(v) : 16; }();
                        guest::store16(rd, pc + 0x23E4, std::uint16_t(square));  // no win from the open pit
                        set_up(2, -2);  // the pair two left and two down: move the shadow there and drop (the owner; dragging a piece is Slide's)
                        std::fprintf(stdout, "{\"event\":\"drop_lesson\",\"home\":[%d,%d],\"target\":[%d,%d]}\n", drop_lesson_home[0],
                                     drop_lesson_home[1], drop_lesson_target[0], drop_lesson_target[1]);
                        std::fflush(stdout);
                        // No card (the owner, 2026-11-20): the panel says what
                        // the lesson is, then the choice: try it, or watch first.
                        drop_lesson_phase = 1, drop_lesson_at = now_m;
                        drop_lesson_watched = false, drop_choice_armed = false;
                    }
                } else if (drop_lesson_phase == 1) {
                    // The choice: A tries it, B watches; Start pauses as usual.
                    const std::uint16_t b = player_buttons_now();
                    if (!in_play || blockplanet::ui::menus_open()) drop_choice_armed = false;
                    else if (!drop_choice_armed) drop_choice_armed = (b & 0xF000u) == 0;
                    else if (b & 0x1000u) drop_choice_armed = false, pause_bridge.request_pause(now_m);
                    else if (b & 0x8000u) {
                        lesson_wait_release = true;
                        drop_lesson_phase = 4, drop_lesson_at = now_m;
                        drop_lesson_say = "Your turn: move onto the pair and press A";
                    } else if (b & 0x4000u) {
                        drop_lesson_phase = 2, drop_lesson_at = now_m;
                        drop_demo_slot = -1, drop_demo_held = 0, drop_demo_aimed = false, drop_demo_dropped = false;
                        drop_lesson_say = "Watch: the shadow moves to the pair";
                    }
                } else if (drop_lesson_phase == 2) {
                    if (drop_demo_dropped) drop_lesson_phase = 3, drop_lesson_at = now_m, drop_lesson_say = "All three break";
                    else if (since() > 15000) drop_lesson_phase = 3, drop_lesson_at = now_m;  // never stuck
                } else if (drop_lesson_phase == 3) {
                    // The bricks break, then the board is set up again for the player.
                    if (since() > 3500 && guest::load32(rd, pc + 0x2AFC) == 0) {
                        set_up(2, -2);  // the same again, for the player
                        drop_lesson_phase = 1, drop_lesson_at = now_m;
                        drop_lesson_watched = true, drop_choice_armed = false;
                    }
                } else if (drop_lesson_phase == 4) {
                    // A drop lands first and scores a moment later: a landing
                    // with no score after 1.5 s missed the pair.
                    static std::optional<std::chrono::steady_clock::time_point> landed;
                    if (guest::load32(rd, pc + 0x2AF8) != drop_lesson_score) {
                        landed.reset();
                        drop_lesson_phase = 6, drop_lesson_at = now_m;
                        drop_lesson_say = "Well done";
                    } else if (count_bricks() != drop_lesson_bricks) {
                        if (!landed) landed = now_m;
                        else if (now_m - *landed > std::chrono::milliseconds(1500)) {
                            landed.reset();
                            drop_lesson_phase = 5, drop_lesson_at = now_m;
                            drop_lesson_say = "No match there: line up with the pair first";
                        }
                    } else {
                        landed.reset();
                    }
                } else if (drop_lesson_phase == 5) {
                    if (since() > 2000) {
                        set_up(2, -2);
                        drop_lesson_phase = 4, drop_lesson_at = now_m;
                        drop_lesson_say = "Try again: move onto the pair and press A";
                    }
                } else if (drop_lesson_phase == 6) {
                    if (since() > 2500 && !pending_results) {
                        if (profile_save && profile_data.progress.chapters_completed["lessons"].insert(100).second) {
                            profile_dirty = true;
                            save_profile();
                        }
                        blockplanet::ui::set_list("chapters", training_lessons(training_done("rescue"), training_done("training_advanced"),
                                                                               magic_lessons_done(), new_lessons_done()));
                        pending_results = StageResults{};
                        pending_results->lesson = 100;
                        drop_lesson_phase = 7;
                        if (!single_stage_quit_at && !single_stage_quitting) single_stage_quit_at = now_m;
                    }
                }
            }
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
            {
                // BlockPlanet's own pause: in play (not paused, no menu on the game's row,
                // not at a game over), Start asks for it.
                static bool parker_set = false;
                static const bool own_pause = [] {
                    const char* v = std::getenv("BLOCKPLANET_OWN_PAUSE");
                    return v == nullptr || v[0] != '0';
                }();
                if (own_pause && !parker_set) {
                    parker_set = true;
                    pause_bridge.set_parker([](bool park) { blockplanet::set_pause_park(park); },
                                            [] { return blockplanet::pause_parked(); });
                    static const bool direct_quit = [] {
                        const char* v = std::getenv("BLOCKPLANET_DIRECT_QUIT");
                        return v == nullptr || v[0] != '0';
                    }();
                    if (direct_quit) pause_bridge.set_direct_quit([] { blockplanet::request_direct_quit(); });
                    if (direct_quit) end_menu.set_direct_exit([] { blockplanet::request_direct_quit(true); });
                    static const bool own_end = [] {
                        const char* v = std::getenv("BLOCKPLANET_OWN_END");
                        return v == nullptr || v[0] != '0';
                    }();
                    if (own_end)
                        end_menu.set_parker([](bool park) { blockplanet::set_end_park(park); },
                                            [] { return blockplanet::end_parked(); });
                }
                // (Not the attract demo, mode 10, nor the box art's: Start ends it, as in the game.)
                const bool demo_now = guest::load16(guest_rdram, 0x800E44A8) == 10 ||
                                      tetrisphere::box_art::phase().load() == tetrisphere::box_art::Phase::Demo;
                const bool in_play_now = !demo_now && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                                         std::int16_t(guest::load16(guest_rdram, 0x800E1228)) < 0 &&
                                         std::int16_t(guest::load16(guest_rdram, 0x80112F9E)) != -1;
                set_own_pause_keys(own_pause && !demo_now);
                own_pause_ready.store(own_pause && in_play_now && !blockplanet::ui::menus_open() && !launching() &&
                                      !pause_bridge.owned());
                // Kept from the game the whole time in play (a key stays down
                // for several reads: let through once paused, the game took it as
                // its own pause, which started when it was let out).
                own_pause_mask.store(own_pause && !demo_now && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                                     std::int16_t(guest::load16(guest_rdram, 0x80112F9E)) != -1);
                const bool start_now = own_pause_start.exchange(false);
                static const bool log = std::getenv("BLOCKPLANET_IDLE_LOG") != nullptr;  // research
                static int last_reasons = -1;
                const int reasons = (own_pause ? 1 : 0) | (in_play_now ? 2 : 0) | (!blockplanet::ui::menus_open() ? 4 : 0) |
                                    (!launching() ? 8 : 0) | (!pause_bridge.owned() ? 16 : 0);
                if (log && (reasons != last_reasons || start_now))
                    std::fprintf(stdout, "{\"event\":\"own_pause\",\"ready\":%d,\"bits\":%d,\"start\":%d,\"scene\":%d,\"row\":%d,\"hearts\":%d,\"t\":%lld}\n",
                                 int(own_pause_ready.load()), reasons, int(start_now), guest::load16(guest_rdram, 0x800E121C),
                                 guest::load16(guest_rdram, 0x800E1228), guest::load16(guest_rdram, 0x80112F9E),
                                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                last_reasons = reasons;
                if (start_now && own_pause_ready.load()) pause_bridge.request_pause(std::chrono::steady_clock::now());
                bp_paused.store(pause_bridge.owned());
            }
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
                    static const char* basics[] = {"Slide and Drop", "Lives", "Combos", "Earning Magic"};
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
            const int state_mode_now = guest::load16(guest_rdram, 0x800E44A8);
            const auto exit_label = [&]() -> std::string {
                // Training's lessons go back to Training's menu (its screen's file title is another mode's).
                if (guest::load16(guest_rdram, 0x800E44A8) == 3 && !launch_path.empty() && launch_path.back().first == "chapters")
                    return "Exit to Training Menu";
                // BlockPlanet's lessons play on a Rescue stage but go back to Training's list.
                if (slide_lesson_phase >= 0 || magic_lesson_phase >= 0 || drop_lesson_phase >= 0) return "Exit to Training Menu";
                if (!launch_path.empty() && !launch_place.empty()) return "Exit to " + blockplanet::ui::menu_place(launch_place);
                // Lines' Campaign list is the same screen as Rescue's (titled Rescue).
                if (state_mode_now == 9 && (launch_path.empty() || launch_path.back().first == "chapters")) return "Exit to Lines Menu";
                return blockplanet::ui::exit_to_label(launch_path.empty() ? std::string() : launch_path.back().first);
            };
            // A lost Minefield (the owner, 2026-11-15): after its mines are shown,
            // the standard game over's black shell closes over the sphere, then
            // the Game Over panel (BlockPlanet's: the game's own game over goes
            // on to its high-score table instead).
            {
                static std::optional<std::chrono::steady_clock::time_point> shell_since;
                const auto now_m = std::chrono::steady_clock::now();
                // (The owner, 2026-11-16: the GAME OVER letters too.) The game's own
                // game over now follows (Casual's refill off, the hearts to 0): the
                // shell, its letters, then its menu as the Game Over panel.
                if (tetrisphere::minefield_sphere::take_lost_game_over()) {
                    shell_since = now_m;
                    if (game_option_applier) game_option_applier->set_menu_casual(false);
                    guest::store16(const_cast<std::uint8_t*>(guest_rdram), 0x80110220u + 0x2D7E, 0);
                }
                const bool in_play_m = guest::load16(guest_rdram, 0x800E121C) == -1;
                if (shell_since && !in_play_m) shell_since.reset();
                tetrisphere::sphere::set_game_over_shell_forced(shell_since.has_value() || hold_shell_on.load());
                minefield_frozen.store(shell_since.has_value());
                // (No panel of BlockPlanet's own: if the game's menu does not come,
                // nothing can leave play from there either.)
                if (minefield_end_chosen.exchange(false)) shell_since.reset();
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
                if (drop_lesson_phase >= 0) {
                    blockplanet::ui::set_text("card_title", "Drop");
                    blockplanet::ui::set_text("card_where", "Training \u00B7 The Basics");
                    blockplanet::ui::set_text("card_goal", "Your shadow is your next piece. Drop it onto two matching pieces "
                                                           "and all three break. Watch first, then it's your turn.");
                } else if (magic_lesson >= 0 && magic_lesson < 6) {
                    const auto family = active_family.load();
                    blockplanet::ui::set_text("card_title", std::string("Magic: ") + magic_lesson_names[magic_lesson]);
                    blockplanet::ui::set_text("card_where", "Training \u00B7 Magic");
                    // What it does and how to fire it; the stripes speak for
                    // themselves (the owner, 2026-11-20).
                    blockplanet::ui::set_text("card_goal", std::string(magic_lesson_texts[magic_lesson]) + " Press " +
                                                           magic_control_label(family, magic_control()) + " to use it.");
                } else if (start_card_mode == 4) {
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
            // Research: BLOCKPLANET_END_MENU_LOG=1 logs, once a second while
            // the pause row is set in play, the game's menu text, the grid's
            // state (0x800DFF08) and the mode, to see why an end panel opens or not.
            if (static const bool end_log = std::getenv("BLOCKPLANET_END_MENU_LOG") != nullptr;
                end_log && guest::load16(guest_rdram, 0x800E1228) >= 0) {
                static auto last_log = std::chrono::steady_clock::time_point{};
                if (std::chrono::steady_clock::now() - last_log > std::chrono::seconds(1)) {
                    last_log = std::chrono::steady_clock::now();
                    std::string texts;
                    for (const auto& t : blockplanet::recent_game_text())
                        texts += (texts.empty() ? "" : "|") + t.text + "@" + std::to_string(t.font) + "," + std::to_string(t.y);
                    std::fprintf(stdout, "{\"event\":\"end_menu_state\",\"menu\":%d,\"row\":%d,\"grid\":%d,\"mode\":%d,\"expected\":%d,\"texts\":\"%s\"}\n",
                                 guest::load16(guest_rdram, 0x800E121C), guest::load16(guest_rdram, 0x800E1228),
                                 guest::load16(guest_rdram, 0x800DFF08u), guest::load16(guest_rdram, 0x800E44A8), episode_end_expected, texts.c_str());
                    std::fflush(stdout);
                }
            }
            if (episode_end_expected != 0) end_menu.expect_episode_end(episode_end_expected > 0), episode_end_expected = 0;
            const auto end = end_menu.update(const_cast<std::uint8_t*>(guest_rdram), end_labels, blockplanet::ui::menus_open(),
                                             std::chrono::steady_clock::now(), pause_bridge.hide_original());
            end_menu_active = end.active;
            end_menu_cleared = end.cleared;
            // The episode end's steps, in the diagnostics file too (the
            // owner's Carry On loop after Episode 2, 2026-11-19, not seen in
            // the harness): each change of the game's row, its grid, the
            // panel and the presses while an end menu or card is up.
            {
                static std::string last_flow;
                const bool flow = end.active || episode_card_skip.has_value() || end_menu.hide_original();
                char line[256];
                std::snprintf(line, sizeof line,
                              "{\"event\":\"end_flow\",\"row\":%d,\"grid\":%d,\"showing\":%d,\"hide\":%d,\"inject\":%d,\"a\":%d,"
                              "\"card_skip\":%d,\"cleared\":%d,\"menus\":%d,\"screen\":\"%s\"}",
                              guest::load16(guest_rdram, 0x800E1228), guest::load16(guest_rdram, 0x800DFF08u), end_menu.showing(),
                              end_menu.hide_original(), end.inject, end.buttons != 0, episode_card_skip.has_value(), end.cleared,
                              blockplanet::ui::menus_open(), blockplanet::ui::current_screen().c_str());
                // (The A press itself flickers: left out of the comparison.)
                std::string key = line;
                if (const auto at = key.find("\"a\":"); at != std::string::npos) key.erase(at, 6);
                if (flow && key != last_flow) {
                    last_flow = key;
                    diagnostic_log().perf(line);
                    if (std::getenv("BLOCKPLANET_END_MENU_LOG") != nullptr) std::fprintf(stdout, "%s\n", line), std::fflush(stdout);
                } else if (!flow) {
                    last_flow.clear();
                }
            }
            // After Rescue's final episode the game shows no end menu but goes
            // on to its own credits, with nothing seen. Once it gets there (the
            // pause row set a moment after the last stage is cleared), the game
            // is parked (its credits stop) and BlockPlanet's credits open;
            // closing them leaves for the Rescue menu.
            {
                static std::optional<std::chrono::steady_clock::time_point> final_since;
                const bool at_end = final_stage_cleared && guest::load16(guest_rdram, 0x800E121C) == -1 &&
                                    guest::load16(guest_rdram, 0x800E1228) >= 0 && !blockplanet::ui::menus_open();
                // (The end bridge counts this as an episode's end waiting for
                // the game's menu, which never comes; its panel never opens.)
                static const bool final_raw_c = std::getenv("BLOCKPLANET_FINAL_RAW") != nullptr;
                if (final_stage_cleared) final_stage_cleared_seen = true;
                if (!at_end || final_raw_c) final_since.reset();
                else if (!final_since) final_since = std::chrono::steady_clock::now();
                static std::optional<std::chrono::steady_clock::time_point> finale_at;
                if (!final_finale && !final_credits_open && final_since &&
                    std::chrono::steady_clock::now() - *final_since > std::chrono::milliseconds(1500)) {
                    // The curtain call: the game's own, seen (its grid and the HUD off).
                    final_finale = true;
                    final_skip = false;
                    finale_at = std::chrono::steady_clock::now();
                    final_since.reset();
                    show_hud(std::nullopt);
                    std::fprintf(stdout, "{\"event\":\"final_finale\",\"on\":true}\n");
                    std::fflush(stdout);
                }
                // Its end: the game's row moves to 1 as its own credits begin
                // (about 114 s in); or a button skipped it.
                if (final_finale && (final_skip || guest::load16(guest_rdram, 0x800E1228) == 1 ||
                                     std::chrono::steady_clock::now() - *finale_at > std::chrono::seconds(180))) {
                    final_finale = false;
                    std::fprintf(stdout, "{\"event\":\"final_finale\",\"on\":false,\"skipped\":%d}\n", final_skip);
                    std::fflush(stdout);
                    final_skip = false;
                    final_credits_open = true;
                    final_stage_cleared = false;
                    blockplanet::play_track(20);  // Sappy (the owner's choice), for the credits
                    if (profile_save) {
                        auto& progress = profile_data.progress;
                        if (progress.chapters_completed["rescue"].insert(10).second) {
                            progress.levels_completed["rescue"][10].insert(10);
                            profile_dirty = true;
                            save_profile();
                        }
                    }
                    blockplanet::set_end_park(true);
                    show_hud(std::nullopt);
                    blockplanet::ui::send_command("credits");
                    std::fprintf(stdout, "{\"event\":\"final_credits\",\"open\":true}\n");
                    std::fflush(stdout);
                }
                // An attack mode's whole episode: the same state after its last
                // stage (the game waits with no menu); play is left at once for
                // the menus, with the episode's results.
                static std::optional<std::chrono::steady_clock::time_point> attack_since;
                static bool attack_leaving = false;
                const bool attack_end = attack_episode_results && variant_competes(play_variant) && !single_stage &&
                                        guest::load16(guest_rdram, 0x800E121C) == -1 &&
                                        guest::load16(guest_rdram, 0x800E1228) >= 0 && !blockplanet::ui::menus_open();
                if (!attack_end) attack_since.reset();
                else if (!attack_since) attack_since = std::chrono::steady_clock::now();
                else if (std::chrono::steady_clock::now() - *attack_since > std::chrono::milliseconds(1500)) {
                    attack_since.reset();
                    pending_results = attack_episode_results;
                    attack_episode_results.reset();
                    std::fprintf(stdout, "{\"event\":\"attack_episode_end\",\"chapter\":%d}\n", pending_results->chapter);
                    std::fflush(stdout);
                    if (launch_path.empty()) launch_path = {{"main", 0}, {"solo", 0}, {"story", 0}}, campaign_source.clear();
                    blockplanet::set_end_park(true);  // the quit leaves from the parked game
                    attack_leaving = true;
                    end_track_override(), grid_quit_at = std::chrono::steady_clock::now();
                }
                if (attack_leaving && blockplanet::end_parked()) {
                    attack_leaving = false;
                    return_to_menu_chapter = pending_results ? pending_results->chapter : 0;
                    blockplanet::request_direct_quit(false);
                    blockplanet::set_end_park(false);
                }
                static std::optional<std::chrono::steady_clock::time_point> credits_seen;
                if (final_credits_open) {
                    if (blockplanet::ui::current_screen() == "credits") credits_seen = std::chrono::steady_clock::now();
                    else if (credits_seen && std::chrono::steady_clock::now() - *credits_seen > std::chrono::milliseconds(300)) {
                        // Closed: back to the Rescue menu.
                        final_credits_open = false;
                        credits_seen.reset();
                        // (A game started without the menus has no launch
                        // path: Rescue's episode list then.)
                        if (launch_path.empty()) {
                            launch_path = {{"main", 0}, {"solo", 0}, {"story", 0}};
                            campaign_source.clear();
                        }
                        end_track_override(), return_to_menu_chapter = 0, grid_quit_at = std::chrono::steady_clock::now();
                        blockplanet::request_direct_quit(false);
                        blockplanet::set_end_park(false);
                        std::fprintf(stdout, "{\"event\":\"final_credits\",\"open\":false}\n");
                        std::fflush(stdout);
                    }
                }
            }
            if (end.open_menu) {
                blockplanet::ui::set_text("end_title", end.title);
                const bool lines_end = guest::load16(guest_rdram, 0x800E44A8) == 9;
                if (lines_end) episode_end_line.clear();  // (no episode time kept for Lines yet)
                blockplanet::ui::set_text("end_where", end.cleared ? std::string(lines_end ? "Lines" : variant_title(play_variant)) + " \u00B7 Episode " +
                                                                         std::to_string(read_game_state(guest_rdram).chapter + 1)
                                                                   : where_now());
                {
                    const int passed = read_game_state(guest_rdram).chapter + 1;
                    // The episode recorded as cleared now, in the profile (the
                    // owner, 2026-11-18: after Exit, Rescue still said Continue
                    // Episode 2). The game's own record of it is written as it
                    // leaves, but BlockPlanet counted an episode cleared only
                    // when play moved on to the next one, which Exit never does.
                    if (end.cleared && profile_save) {
                        const char* game = lines_end ? "lines" : "rescue";
                        auto& progress = profile_data.progress;
                        if (progress.chapters_completed[game].insert(passed).second) {
                            progress.levels_completed[game][passed].insert(read_game_state(guest_rdram).level + 1);
                            profile_dirty = true;
                            save_profile();
                            std::fprintf(stdout, "{\"event\":\"episode_recorded\",\"game\":\"%s\",\"episode\":%d}\n", game, passed);
                            std::fflush(stdout);
                        }
                    }
                    if (end.cleared) blockplanet::ui::set_text("end_title", "Episode " + std::to_string(passed) + " Cleared");
                    const std::string cheer = "Congratulations, you passed Episode " + std::to_string(passed) + ".";
                    blockplanet::ui::set_text("end_line", end.cleared ? (episode_end_line.empty() ? cheer : cheer + "  " + episode_end_line)
                                                                      : game_over_line());
                }
                auto end_items = end.items;
                // Minefield: New Field (a new field in the same arena) and Exit; no Training.
                if (!tetrisphere::minefield_sphere::arena_name().empty() && !end.cleared) {
                    end_items.erase(std::remove_if(end_items.begin(), end_items.end(), [](const auto& i) { return i.id == "training"; }),
                                    end_items.end());
                    for (auto& item : end_items)
                        if (item.id == "try_again") item.label = "New Field", item.action = "command:minefield_again_end";
                    blockplanet::ui::set_text("end_where", "Minefield \u00B7 " + tetrisphere::minefield_sphere::arena_name());
                    blockplanet::ui::set_text("end_line", game_over_line(true));
                }
                for (auto& item : end_items) {
                    if (item.id == "exit") item.label = exit_label();
                    if (item.id == "continue" && end.cleared) {
                        const int next = read_game_state(guest_rdram).chapter + 2;  // (0-based there)
                        item.label = "Carry on to Episode " + std::to_string(next);
                        item.note.clear();
                    }
                }
                blockplanet::ui::set_list("end", end_items);
                blockplanet::ui::request_open(end.cleared ? "episode_end" : "gameover");
            }
            // Only in play: back at the game's menu (after Quit), the menus shown are the launch path's.
            if ((pause.close_menu || end.close_menu) && guest::load16(guest_rdram, 0x800E121C) == -1) blockplanet::ui::request_close();
            std::uint16_t card_buttons = 0;
            if (episode_card_skip) {
                const auto since = std::chrono::steady_clock::now() - *episode_card_skip;
                static bool card_seen = false;
                const int card_mode = guest::load16(guest_rdram, 0x800E44A8);
                const bool card = guest::load16(guest_rdram, 0x800E121C) == -1 && guest::load16(guest_rdram, 0x800E1228) == 100 &&
                                  (card_mode == 1 || card_mode == 9);  // Rescue, Lines (Hide & Seek's card has its own panel)
                if (card) {
                    if (!card_seen) std::fprintf(stdout, "{\"event\":\"episode_card_skip\"}\n"), std::fflush(stdout);
                    card_seen = true;
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
                    card_buttons = ms % 350 < 100 ? 0x8000 : 0;  // A, pressed and let go
                }
                episode_card_up = card;
                if ((card_seen && !card) || since > std::chrono::seconds(30)) episode_card_skip.reset(), card_seen = false;
            } else {
                episode_card_up = false;
            }
            launcher_buttons.store(pause.inject ? (0x10000u | pause.buttons)
                                   : end.inject ? (0x10000u | end.buttons)
                                   : hs_buttons != 0 ? (0x10000u | hs_buttons)
                                   : card_buttons != 0 ? (0x10000u | card_buttons)
                                   // The Drop lesson's demonstration plays itself.
                                   : drop_lesson_phase == 1 || slide_lesson_phase == 1 || slide_lesson_phase == 10 || lesson_wait_release ||
                                             // done or missed: no second press until the board is reset or the lesson ends (the owner)
                                             slide_lesson_phase == 6 || slide_lesson_phase == 13 || (slide_lesson_phase >= 14 && slide_lesson_phase <= 20) ||
                                             (slide_lesson_phase >= 23 && slide_lesson_phase <= 30) || (slide_lesson_phase >= 33 && slide_lesson_phase <= 40) ||
                                             slide_lesson_phase == 43 || slide_lesson_phase == 44 || slide_lesson_phase >= 47 ||
                                             (slide_lesson_phase == 21 && hearts_demo_part != 2) ||
                                             drop_lesson_phase >= 5
                                         ? 0x10000u  // choosing: the game waits
                                   : slide_lesson_phase == 2 ? (0x30000u | slide_lesson_buttons())
                                   : slide_lesson_phase == 11 ? (0x30000u | slide_drop_buttons())
                                   : slide_lesson_phase == 21 ? (0x30000u | hearts_demo_buttons())
                                   : slide_lesson_phase == 31 ? (0x30000u | combos_demo_buttons())
                                   : slide_lesson_phase == 41 || slide_lesson_phase == 45 ? (0x30000u | rubble_demo_buttons())
                                   : drop_lesson_phase == 2 || drop_lesson_phase == 3 ? (0x30000u | drop_lesson_buttons())
                                   : research_fuse_buttons() != 0 ? (0x10000u | research_fuse_buttons())
                                   : research_magic_press && std::chrono::steady_clock::now() >= *research_magic_press &&
                                             std::chrono::steady_clock::now() < *research_magic_press + std::chrono::milliseconds(400)
                                       ? 0x10004u
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
            // Left by a press on the box art: seen, however soon it was pressed.
            static int title_leaves_seen = 0;
            if (const int leaves = tetrisphere::direct_start::title_leaves(); leaves != title_leaves_seen) {
                title_leaves_seen = leaves;
                title_seen = true;
                title_restart_pending = false;
            }
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
            // Step 4 (docs/ROADMAP.md): the game parked in its reset screen while
            // BlockPlanet's menus are open over its menu, so its menu code does not
            // run; left for anything to be done on its thread (a start, a save
            // switch, a title restart: direct_start::game_thread_wanted).
            {
                const bool parts[] = {!original_menus, at_game_menu, blockplanet::ui::menus_open(), !launching(), !box_art_demo,
                                      !continue_mode, !return_to_menu_chapter, !credits_pending, !title_restart_pending,
                                      !tetrisphere::direct_start::game_thread_wanted()};
                bool idle = true;
                int why = 0;
                for (int i = 0; i < 10; ++i) if (!parts[i] && idle) idle = false, why = i;
                static bool last = false;
                static const bool log = std::getenv("BLOCKPLANET_IDLE_LOG") != nullptr;  // research
                if (log && idle != last) std::fprintf(stdout, "{\"event\":\"game_idle\",\"on\":%d,\"because\":%d}\n", idle, why);
                last = idle;
                blockplanet::set_game_idle(idle);
            }
            if (exit_hide_since) {
                if (!blockplanet::vortex_backdrop_on()) exit_hide_let_out = true;
                if ((exit_hide_let_out && blockplanet::vortex_backdrop_on()) ||
                    std::chrono::steady_clock::now() - *exit_hide_since > std::chrono::seconds(4))
                    exit_hide_since.reset();
            }
            static const bool exit_hide_off = std::getenv("BLOCKPLANET_EXIT_HIDE") != nullptr && std::getenv("BLOCKPLANET_EXIT_HIDE")[0] == '0';  // research
            blockplanet::set_game_hidden_all(exit_hide_since.has_value() && !exit_hide_off);
            // (Not while the jukebox's Vortex is shown: it is the game's own scene.)
            blockplanet::set_game_menu_hidden(!original_menus && at_game_menu && !blockplanet::vortex_backdrop_on() &&
                                              (blockplanet::ui::menus_open() || launching()));
        }
        // The game's attract demo starts after its menu sits idle; behind
        // BlockPlanet's menus it counts as play and launches were refused
        // (the owner's report). While BlockPlanet's menus are open over the
        // game's menu, its idle counters (frames on the page) are held at 10
        // seconds, past every page's own entrance timing.
        if (guest_rdram != nullptr && blockplanet::ui::menus_open() && !launching() && !blockplanet::vortex_backdrop_on() &&
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
                // (Held over the game's next-episode card while it is pressed away.)
                if (launching() || continue_mode || !in_play_now || episode_card_up) playing_since.reset();
                else if (!playing_since) playing_since = now_l;
                if (!playing_since || !faded_in) clear_since.reset();
                else if (!clear_since) clear_since = now_l;
                // Done settle_ms after the fade is clear in play; 4 s into
                // play in any case (a mode whose fade reads otherwise); 20 s
                // at most.
                if ((clear_since && now_l - *clear_since > std::chrono::milliseconds(settle_ms)) ||
                    (playing_since && now_l - *playing_since > std::chrono::seconds(4)) ||
                    (now_l - loading_since > std::chrono::seconds(20) && !episode_card_up))
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
                if (bp_paused.load()) paused = true;  // BlockPlanet's own pause (the game parked)
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
            blockplanet::jukebox::tick(guest_rdram);
            // Research: BLOCKPLANET_TRANSITION_LOG=1 logs, with the time, each
            // change of the states a stage-to-stage transition goes through:
            // the scene/menu (0x800E121C), the pause row (0x800E1228), the
            // stage (+0x23BA) and episode (0x800E44A0), the hearts, BlockPlanet's
            // loading cover, and "playable" (a piece in hand: the drop timer
            // counting, in play with no pause).
            if (static const bool transition_log = std::getenv("BLOCKPLANET_TRANSITION_LOG") != nullptr;
                transition_log && guest_rdram != nullptr) {
                constexpr std::uint32_t p1 = 0x80110220u;
                const int menu = guest::load16(guest_rdram, 0x800E121C), row = guest::load16(guest_rdram, 0x800E1228);
                const int stage = guest::load16(guest_rdram, p1 + 0x23BA), episode = guest::load16(guest_rdram, 0x800E44A0);
                const int hearts = guest::load16(guest_rdram, p1 + 0x2D7E);
                const std::uint32_t t = guest::load32(guest_rdram, p1 + 0x2408);
                float timer = 0;
                std::memcpy(&timer, &t, 4);
                const int playable = menu == -1 && row < 0 && timer > 0.0f && timer < 7000.0f;
                const int state[7] = {menu, row, stage, episode, hearts, int(loading_active), playable};
                static int last[7] = {-99, -99, -99, -99, -99, -99, -99};
                if (std::memcmp(state, last, sizeof state) != 0) {
                    std::memcpy(last, state, sizeof state);
                    std::fprintf(stdout, "{\"event\":\"transition\",\"ms\":%lld,\"menu\":%d,\"pause_row\":%d,\"stage\":%d,\"episode\":%d,\"hearts\":%d,\"cover\":%d,\"playable\":%d}\n",
                                 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),
                                 menu, row, stage, episode + 1, hearts, int(loading_active), playable);
                    std::fflush(stdout);
                }
            }
            // Research: BLOCKPLANET_DROP_LOG=1 logs player 1's drop timer
            // (+0x2408, its 0-600 form +0x2B10), the camera zoom (+0x2404 speed,
            // +0x2414 target, +0x2416 eased) and hearts whenever they change,
            // with the time, to find when the game drops the piece itself.
            if (static const bool drop_log = std::getenv("BLOCKPLANET_DROP_LOG") != nullptr; drop_log && guest_rdram != nullptr) {
                constexpr std::uint32_t c = 0x80110220u;
                const std::uint32_t t = guest::load32(guest_rdram, c + 0x2408);
                float timer;
                std::memcpy(&timer, &t, 4);
                const int shown = guest::load16(guest_rdram, c + 0x2B10), target = guest::load16(guest_rdram, c + 0x2414),
                          eased = guest::load16(guest_rdram, c + 0x2416), hearts = guest::load16(guest_rdram, c + 0x2D7E);
                const std::uint32_t speed = guest::load32(guest_rdram, c + 0x2404);
                // Research: BLOCKPLANET_DROP_TEST_KEEP=n empties every layer
                // above layer n in a 5 x 5 patch round the cursor each time a
                // piece comes into hand (the timer back to 0), to see whether a
                // deeper landing spot changes the fall and the time to drop.
                if (static const char* keep = std::getenv("BLOCKPLANET_DROP_TEST_KEEP"); keep != nullptr) {
                    static int last_shown = -1;
                    if (shown == 0 && last_shown != 0) {
                        auto* rd = const_cast<std::uint8_t*>(guest_rdram);
                        const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u), masks = guest::load32(guest_rdram, 0x800E44D4u),
                                            entries = guest::load32(guest_rdram, 0x800E44D8u);
                        const int k = std::atoi(keep);
                        const int x0 = 8 + guest::load16(guest_rdram, c + 0x2B2A), y0 = 8 + guest::load16(guest_rdram, c + 0x2B2E);
                        if (values != 0 && masks != 0 && entries != 0)
                            for (int dy = -2; dy <= 2; ++dy)
                                for (int dx = -2; dx <= 2; ++dx) {
                                    const std::uint32_t cell = std::uint32_t((((y0 + dy) % 32 + 32) % 32) * 32 + (((x0 + dx) % 32 + 32) % 32));
                                    for (int layer = k + 1; layer <= 8; ++layer) {
                                        const std::uint32_t at = std::uint32_t(layer) * 1024u + cell;
                                        guest::store16(rd, values + 2u * at, 0);
                                        guest::store8(rd, masks + at, 0);
                                        guest::store32(rd, entries + 4u * at, 0);
                                    }
                                    const std::uint32_t top = std::uint32_t(k) * 1024u + cell;
                                    guest::store8(rd, masks + top, static_cast<std::uint8_t>(guest::load8(guest_rdram, masks + top) | 16));
                                }
                        std::fprintf(stdout, "{\"event\":\"drop_test_dig\",\"at\":[%d,%d],\"values\":\"0x%08X\",\"layer2\":%d}\n", x0, y0, values,
                                     values ? guest::load16(guest_rdram, values + 2u * std::uint32_t(2 * 1024 + ((y0 & 31) * 32 + (x0 & 31)))) : -1);
                        std::fflush(stdout);
                    }
                    last_shown = shown;
                }
                static int last[5] = {-9, -9, -9, -9, -9};
                static std::uint32_t last_speed = 1;
                if (shown != last[0] || target != last[1] || eased != last[2] || hearts != last[3] || speed != last_speed) {
                    last[0] = shown, last[1] = target, last[2] = eased, last[3] = hearts, last_speed = speed;
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    // The column under the cursor: each layer's block value
                    // (0 empty) and the column map byte (height, kind).
                    const int cx = ((8 + guest::load16(guest_rdram, c + 0x2B2A)) % 32 + 32) % 32,
                              cy = ((8 + guest::load16(guest_rdram, c + 0x2B2E)) % 32 + 32) % 32;
                    const std::uint32_t values = guest::load32(guest_rdram, 0x800E44D0u), columns = guest::load32(guest_rdram, 0x800E44DCu);
                    std::string column;
                    if (values != 0)
                        for (int layer = 0; layer < 8; ++layer)
                            column += (layer ? "," : "") + std::to_string(guest::load16(guest_rdram, values + 2u * std::uint32_t(layer * 1024 + cy * 32 + cx)));
                    const int map = columns != 0 ? guest::load8(guest_rdram, columns + std::uint32_t(cy * 32 + cx)) : -1;
                    std::fprintf(stdout, "{\"event\":\"drop_log\",\"speed_per_retrace\":%.3f,\"retraces\":%d,\"ms\":%lld,\"timer\":%.0f,\"shown\":%d,\"speed\":%u,\"target\":%d,\"eased\":%d,\"hearts\":%d,\"cell\":[%d,%d],\"column\":[%s],\"map\":%d}\n",
                                 [&] { const std::uint32_t r = guest::load32(guest_rdram, c + 0x3254); float f; std::memcpy(&f, &r, 4); return double(f); }(),
                                 int(guest::load16(guest_rdram, 0x80160C64u)),
                                 static_cast<long long>(ms), timer, shown, speed >> 16, target, eased, hearts, cx, cy, column.c_str(), map);
                    std::fflush(stdout);
                }
            }
            // Research: BLOCKPLANET_PLAYER_TRACE=path appends the music players
            // (0x80107290 and the jingle player 0x80109DC0, 0x1600 bytes each)
            // ten times a second, to find where a song loops.
            if (static const char* trace = std::getenv("BLOCKPLANET_PLAYER_TRACE"); trace != nullptr && guest_rdram != nullptr) {
                static auto next_trace = std::chrono::steady_clock::now();
                static std::ofstream out(trace, std::ios::binary);
                if (std::chrono::steady_clock::now() >= next_trace) {
                    next_trace += std::chrono::milliseconds(100);
                    for (const std::uint32_t base : {0x80107290u, 0x80109DC0u})
                        for (std::uint32_t a = base; a < base + 0x1600u; a += 2) {
                            const std::uint16_t v = static_cast<std::uint16_t>(guest::load16(guest_rdram, a));
                            out.write(reinterpret_cast<const char*>(&v), 2);
                        }
                    out.flush();
                }
            }
            // Developer tools: the free camera on player 1's sticks and triggers.
            if (tetrisphere::frame_step::enabled()) {
                static auto last_move = std::chrono::steady_clock::now();
                const auto now_move = std::chrono::steady_clock::now();
                const float dt = std::clamp(std::chrono::duration<float>(now_move - last_move).count(), 0.0f, 0.1f);
                last_move = now_move;
                ControllerSample pad{};
                {
                    std::lock_guard lock(input_snapshot_mutex);
                    if (input_snapshot[0].connected) pad = input_snapshot[0].sample;
                }
                const auto axis = [](int v) {  // -1 to 1, with a dead zone
                    const float f = std::clamp(float(v) / 32767.0f, -1.0f, 1.0f);
                    return std::abs(f) < 0.2f ? 0.0f : (f - std::copysign(0.2f, f)) / 0.8f;
                };
                tetrisphere::frame_step::CameraMove move;
                static bool left_click = false, right_click = false;
                if (pad.right_stick && !right_click) move.scene = 1;
                if (pad.left_stick && !left_click) move.scene = -1;
                left_click = pad.left_stick, right_click = pad.right_stick;
                move.right = axis(pad.left_x) * dt;
                move.forward = -axis(pad.left_y) * dt;
                move.up = (axis(pad.trigger_right) - axis(pad.trigger_left)) * dt;
                move.yaw = axis(pad.right_x) * 1.5f * dt;
                move.pitch = axis(pad.right_y) * 1.5f * dt;
                // Research (no sticks on the test harness's virtual pad):
                // BLOCKPLANET_QA_CAMERA="right up forward yaw pitch", held.
                static const std::array<float, 5> qa_stick = [] {
                    std::array<float, 5> v{};
                    if (const char* e = std::getenv("BLOCKPLANET_QA_CAMERA"))
                        std::sscanf(e, "%f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]);
                    return v;
                }();
                move.right += qa_stick[0] * dt, move.up += qa_stick[1] * dt, move.forward += qa_stick[2] * dt;
                move.yaw += qa_stick[3] * 1.5f * dt, move.pitch += qa_stick[4] * 1.5f * dt;
                tetrisphere::frame_step::drive_camera(move);
            }
            // --developer-frozen: the inspector opened on the first held frame,
            // as F1 would (RT64 watches the key through its event filter).
            if (developer_frozen_start && tetrisphere::frame_step::waiting()) {
                static bool opened = false;
                if (!opened) {
                    opened = true;
                    SDL_Event f1{};
                    f1.type = SDL_KEYDOWN;
                    f1.key.type = SDL_KEYDOWN;
                    f1.key.state = SDL_PRESSED;
                    f1.key.keysym.scancode = SDL_SCANCODE_F1;
                    f1.key.keysym.sym = SDLK_F1;
                    f1.key.windowID = window != nullptr ? SDL_GetWindowID(window) : 0;
                    SDL_PushEvent(&f1);
                }
            }
            if (window != nullptr) {
                int w = 0, h = 0;
                SDL_GetWindowSize(window, &w, &h);
                if (h > 0) blockplanet::set_vortex_aspect(float(w) / float(h));
            }
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
            {
                static const bool log = std::getenv("BLOCKPLANET_GRID_LOG") != nullptr;  // research: why the grid is up
                static int last = -1;
                const int key = (start_at ? 1 : 0) | (paused ? 2 : 0) | (unpaused_at ? 4 : 0) | (grid_quit_at ? 8 : 0) | (grid_quit_red ? 16 : 0) |
                                (over_end ? 32 : 0) | (box_art_grid ? 64 : 0) | (launching() ? 128 : 0);
                if (log && key != last) {
                    last = key;
                    std::fprintf(stdout, "{\"event\":\"grid_reason\",\"start_at\":%d,\"paused\":%d,\"unpaused_at\":%d,\"quit\":%d,\"red\":%d,\"end\":%d,\"box\":%d,\"launching\":%d}\n",
                                 key & 1, (key >> 1) & 1, (key >> 2) & 1, (key >> 3) & 1, (key >> 4) & 1, (key >> 5) & 1, (key >> 6) & 1, (key >> 7) & 1);
                    std::fflush(stdout);
                }
            }
            // Research: BLOCKPLANET_FINAL_RAW=1 shows what the game draws after
            // Rescue's final episode (no grid there, no credits).
            static const bool final_raw = std::getenv("BLOCKPLANET_FINAL_RAW") != nullptr;
            if ((final_raw && final_stage_cleared_seen) || final_finale) blockplanet::ui::set_grid_override("");
            else
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
            // The owner (2026-11-16): the GAME OVER letters stay a moment longer
            // over the shell before the fade (the fade held back for 1.5 s).
            static std::optional<std::chrono::steady_clock::time_point> fade_from;
            const bool fading = guest_rdram != nullptr && guest::load16(guest_rdram, 0x800E1228) >= 0 &&
                                guest::load16(guest_rdram, 0x800DFF08) == 4 && std::int16_t(guest::load16(guest_rdram, 0x80112F9E)) < 0;
            if (!fading) fade_from.reset();
            else if (!fade_from) fade_from = std::chrono::steady_clock::now();
            const bool delay = fading && tetrisphere::sphere::game_over_shell_wanted(guest_rdram) &&
                               std::chrono::steady_clock::now() - *fade_from < std::chrono::milliseconds(1500);
            if ((hold && fading) || delay)
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
