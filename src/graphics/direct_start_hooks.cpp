// Starts modes directly, without the original menus (include/tetrisphere/direct_start.h;
// docs/research/blockplanet-game-state.md, "Starting modes directly").
//
// blockplanet_direct_start runs from a hook in the game's top-level loop
// (func_80070118 at 0x8007025C), on the game's thread, once a frame while the
// attract demo and endings are not running: where the loop checks whether
// the title is over (0x800E1646) before the game's own direct start (the byte
// at 0x800DFE8C, which starts Training Basics without menus) and the menus.
// BlockPlanet's menus are often used while the game itself still shows its
// title behind them; then the title's exit is done first, as the title does
// when it hands over to the main menu (func_800685FC at 0x80069500): its
// jingle player stopped, 0x800E16B4 = 1, player 1's controller chosen
// (func_8003E460; without it player 1 has none), 0x800F22C0 set, then
// 0x800E1646 = 1, its count at 0x800E16B8 and the menu timers cleared; the
// loop then goes on into play. It does what the original menus do when a
// mode is confirmed, in the same order, without their pages:
//
// 0. The player page's opening (func_8004127C at 0x80041884): the save's
//    name read into 0x800E0784 (func_800656C8), "PLAYER 1" when there is
//    none, the save call 0x80066854, then the name moved to 0x800E13C8.
//    (Its music, grid fly-in and page variables are left out.)
// 1. The mode page's confirm (func_8004127C at 0x800462D0): flags, the level
//    counters cleared, the mode, player 1's port, character, name and one
//    more byte, then the save calls 0x80065D74, 0x80066854, 0x80065660 and,
//    when the second returned 0, 0x80066CE8. (The confirm sound is left out.)
// 2. The player page's hand-over after its exit animation (0x80041F48): the
//    name copied to 0x800E0784, the save call 0x80065660, 0x801125C6 = 1, the
//    new-game routine func_800CC6E0, 0x80112D18 = 0 and the four menu rows
//    -1, which is play.
//
// The chapter and level BlockPlanet chose are written just before the
// new-game routine, so the first level is built there.

#include "tetrisphere/direct_start.h"

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

extern "C" {
void func_80065D74(std::uint8_t* rdram, recomp_context* ctx);
void func_800656C8(std::uint8_t* rdram, recomp_context* ctx);
void func_80066854(std::uint8_t* rdram, recomp_context* ctx);
void func_80065660(std::uint8_t* rdram, recomp_context* ctx);
void func_80066CE8(std::uint8_t* rdram, recomp_context* ctx);
void func_800CC6E0(std::uint8_t* rdram, recomp_context* ctx);
void func_8003E460(std::uint8_t* rdram, recomp_context* ctx);
void func_80066E6C(std::uint8_t* rdram, recomp_context* ctx);
void func_8007ADF8(std::uint8_t* rdram, recomp_context* ctx);
void func_8007AEE0(std::uint8_t* rdram, recomp_context* ctx);
}

namespace tetrisphere {
namespace {

using namespace tetrisphere::guest;

// Original menu state.
constexpr std::uint32_t main_row = 0x800E121Cu;
constexpr std::uint32_t submenu_row = 0x800E1220u;
constexpr std::uint32_t player_page_row = 0x800E1222u;
constexpr std::uint32_t mode_row = 0x800E1224u;
constexpr std::uint32_t page_exit = 0x800E11FCu;     // a page's closing phase, 0 when none
constexpr std::uint32_t menu_timer_a = 0x800E1208u;  // u32 menu frame counters
constexpr std::uint32_t menu_timer_b = 0x800E120Cu;
constexpr std::uint32_t training_jump = 0x800DFE8Cu;  // u8: the game's own Training start
constexpr std::uint32_t title_done = 0x800E1646u;      // u16: 0 while the intro and title run
constexpr std::uint32_t title_exits = 0x800E16B8u;     // s16: counted by the title at each exit
constexpr std::uint32_t title_left = 0x800E16B4u;      // s16: set when the title hands over
constexpr std::uint32_t jingle_player = 0x80109DC0u;
constexpr std::uint32_t effects_player = 0x8010B358u;
constexpr std::uint32_t menu_scene_copy = 0x800F22C0u;  // set from 0x801028D0 at the title's exit
constexpr std::uint32_t menu_scene_source = 0x801028D0u;
// The menus' grid transition (static_0_8003B1A4): 0 none, 4 flying in,
// 1 shown, 5 flying out (then 0). The player page hands over only once its
// fly-out has ended (0); a direct start removes a shown grid at once.
constexpr std::uint32_t grid_state = 0x800DFF08u;
constexpr std::uint32_t grid_state_seen = 0x800DFF10u;  // the animator's last state
constexpr std::uint16_t grid_none = 0, grid_shown = 1;
// The menus' screen fade (255 dark, 0 clear) and the values the grid's
// fly-in and fly-out leave beside it: all 0 once the menus hand over to play.
constexpr std::uint32_t screen_fade = 0x800E1202u;
constexpr std::uint32_t fade_saved = 0x800E1206u;
constexpr std::uint32_t fade_flag = 0x800E11FAu;
constexpr std::uint32_t saved_page_flag = 0x800E12DCu;  // s16: with progress, Rescue opens another page

// What the mode page sets (meanings not yet known for the flags).
constexpr std::uint32_t flag_a = 0x800DFD4Cu;
constexpr std::uint32_t flag_b = 0x8015D980u;
constexpr std::uint32_t flag_c = 0x800DFD50u;
constexpr std::uint32_t flag_d = 0x800DFD54u;
constexpr std::uint32_t level_count = 0x800E4498u;
constexpr std::uint32_t next_level = 0x800E449Cu;
constexpr std::uint32_t stage_count = 0x80102666u;
constexpr std::uint32_t chapter = 0x800E44A0u;
constexpr std::uint32_t mode = 0x800E44A8u;
constexpr std::uint32_t port_source = 0x800E07CCu;       // s8
constexpr std::uint32_t character_source = 0x800E07D4u;  // u8
constexpr std::uint32_t extra_source = 0x800E07E4u;      // s8
constexpr std::uint32_t name = 0x800E13C8u;              // the player's name (9 bytes)
constexpr std::uint32_t name_copy = 0x800E0784u;  // also where the save's name is read
constexpr std::uint32_t name_cursor = 0x800E13C0u;
// The mode list's row, kept by the mode page in its row memory (0x800E0754 +
// 2) and saved by func_80065660 into the save (0x80102B07): the mode last chosen.
constexpr std::uint32_t mode_row_memory = 0x800E0756u;
constexpr std::uint32_t p1_port = 0x80113320u;
constexpr std::uint32_t p1_character = 0x80113322u;
constexpr std::uint32_t p1_extra = 0x80113323u;
constexpr std::uint32_t p1_name = 0x80113328u;
constexpr std::uint32_t p1_level = 0x80110220u + 0x23BAu;
// The hand-over.
constexpr std::uint32_t new_game_flag = 0x801125C6u;
constexpr std::uint32_t after_new_game = 0x80112D18u;

constexpr std::int16_t mode_rescue = 1;
constexpr std::int16_t mode_time_trial = 2;
constexpr std::int16_t mode_puzzle = 4;
constexpr std::int16_t mode_lines = 9;
constexpr std::int16_t mode_training = 3;
// Training.
constexpr std::uint32_t training_type = 0x800E44ACu;  // 1 Basics, 2 Advanced, 3 Puzzle
constexpr std::uint32_t port_index = 0x800E0B10u;     // which entry of 0x800E07CC is player 1's port
constexpr std::uint32_t training_name = 0x800ECC4Cu;  // the name the Training hand-over gives player 1
constexpr std::uint8_t default_character = 4;
constexpr std::uint32_t save_flags = 0x80102918u;  // the save's first byte (RAM copy); bit 0 set by every Training start
constexpr std::uint32_t training_cleared_a = 0x800E076Cu;
constexpr std::uint32_t training_cleared_b = 0x800E4480u;

std::mutex mutex;
std::optional<DirectStartRequest> waiting;
std::optional<direct_start::Result> outcome;

gpr address_arg(std::uint32_t value) { return static_cast<gpr>(static_cast<std::int64_t>(static_cast<std::int32_t>(value))); }

// Calls a game function, leaving the caller's registers as they were, and
// returns its v0. a0 is passed when given.
gpr call(void (*function)(std::uint8_t*, recomp_context*), std::uint8_t* rdram, recomp_context* ctx,
         std::optional<std::uint32_t> a0 = std::nullopt) {
    const recomp_context saved = *ctx;
    if (a0) ctx->r4 = address_arg(*a0);
    function(rdram, ctx);
    const gpr result = ctx->r2;
    *ctx = saved;
    return result;
}

void finish(direct_start::Result result) {
    waiting.reset();
    outcome = result;
}

const char* mode_name(const DirectStartRequest& request) {
    switch (request.mode) {
    case DirectMode::Rescue: return "rescue";
    case DirectMode::Training: return "training";
    case DirectMode::Puzzle: return "puzzle";
    case DirectMode::Lines: return "lines";
    }
    return "";
}

void log(const char* status, const DirectStartRequest& request, const char* reason = nullptr) {
    std::fprintf(stdout, "{\"event\":\"direct_start\",\"mode\":\"%s\",\"chapter\":%d,\"level\":%d,\"training\":%d,\"status\":\"%s\"",
                 mode_name(request), request.chapter + 1, request.level + 1, request.training_type, status);
    if (reason != nullptr) std::fprintf(stdout, ",\"reason\":\"%s\"", reason);
    std::fprintf(stdout, "}\n");
    std::fflush(stdout);
}

// The menus' grid gone and their fade clear, as after the grid's fly-out.
void clear_menu_presentation(std::uint8_t* rdram) {
    store16(rdram, grid_state, grid_none);
    store16(rdram, grid_state_seen, grid_none);
    store16(rdram, screen_fade, 0);
    store16(rdram, fade_saved, 0);
    store16(rdram, fade_flag, 0);
}

// The menus' last step: no menu shown, which is play.
void close_menus(std::uint8_t* rdram) {
    store16(rdram, main_row, -1);
    store16(rdram, submenu_row, -1);
    store16(rdram, player_page_row, -1);
    store16(rdram, mode_row, -1);
    store32(rdram, menu_timer_a, 0);
    store32(rdram, menu_timer_b, 0);
}

// Training (Basics, Advanced, Puzzle), as the Training page's confirm
// (func_8004C7E8 at 0x8004F9B8, its sound left out) and the main menu's
// hand-over do it: for Training (not the attract demo) it sets bit 0 of the
// save's first byte and writes the save (0x8004D034), then hands over
// (0x8004D1D8).
bool start_training(std::uint8_t* rdram, recomp_context* ctx, const DirectStartRequest& request) {
    store16(rdram, flag_b, 1);
    store32(rdram, port_index, 0);
    call(func_8003E460, rdram, ctx);  // player 1's port: the first controller connected
    store16(rdram, mode, mode_training);
    store8(rdram, p1_extra, 0);
    store16(rdram, training_type, static_cast<std::int16_t>(request.training_type));
    store16(rdram, level_count, 0);
    store16(rdram, next_level, 0);
    store16(rdram, chapter, 0);

    clear_menu_presentation(rdram);
    store16(rdram, training_cleared_a, 0);
    store8(rdram, save_flags, load8(rdram, save_flags) | 1u);
    call(func_80066E6C, rdram, ctx);  // writes the save
    store32(rdram, training_cleared_b, 0);
    store8(rdram, p1_port, load8(rdram, port_source + load32(rdram, port_index)));
    store32(rdram, p1_name, training_name);
    store8(rdram, p1_character, default_character);
    store16(rdram, new_game_flag, 1);
    call(func_800CC6E0, rdram, ctx);
    store32(rdram, after_new_game, 0);
    close_menus(rdram);
    log("started", request);
    return true;
}

// A mode from the Single page's mode list (Rescue, Puzzle; Time Trial and
// Lines to follow), as the player page and its mode list do it. Returns false
// when the menus would not have started it the usual way.
bool start_single(std::uint8_t* rdram, recomp_context* ctx, const DirectStartRequest& request, std::int16_t mode_number) {
    // 0. The player page's opening: the player's name.
    call(func_800656C8, rdram, ctx);
    if (load8(rdram, name_copy) == 0) {
        const char default_name[] = "PLAYER 1";
        for (std::uint32_t i = 0; i < sizeof default_name; ++i)
            store8(rdram, name_copy + i, static_cast<std::uint8_t>(default_name[i]));
    }
    store32(rdram, p1_name, name_copy);
    call(func_80066854, rdram, ctx);
    for (std::uint32_t i = 0; i < 9; ++i) {
        store8(rdram, name + i, load8(rdram, name_copy + i));
        store8(rdram, name_copy + i, 0);
    }
    store32(rdram, name_cursor, 0);

    // 1. The mode page's confirm, on the mode's row.
    const std::int16_t row = mode_number == mode_rescue ? 0 : mode_number == mode_puzzle ? 2
                           : mode_number == mode_time_trial ? 3 : 5;
    store16(rdram, mode_row_memory, row);
    store16(rdram, flag_a, 1);
    store16(rdram, flag_b, 1);
    store16(rdram, flag_c, 1);
    store16(rdram, flag_d, 1);
    store16(rdram, level_count, 0);
    store16(rdram, next_level, 0);
    store16(rdram, stage_count, 0);
    store16(rdram, chapter, 0);
    store16(rdram, mode, mode_number);
    if (mode_number == mode_lines) store16(rdram, flag_a, 2);
    store16(rdram, level_count, 0);
    store16(rdram, next_level, 0);
    store16(rdram, chapter, 0);
    store8(rdram, p1_port, load8(rdram, port_source));
    store8(rdram, p1_character, load8(rdram, character_source));
    store32(rdram, p1_name, name);
    store8(rdram, p1_extra, load8(rdram, extra_source));
    call(func_80065D74, rdram, ctx);
    const gpr loaded = call(func_80066854, rdram, ctx);
    call(func_80065660, rdram, ctx);
    if (loaded == 0 && call(func_80066CE8, rdram, ctx) == 0) {
        // The menu stays on its page here (0x800E1240 = 0).
        log("refused", request, "save_check");
        return false;
    }
    // With saved progress the mode list opens another original page first
    // (0x800E1232; not for Time Trial); BlockPlanet chooses the chapter and
    // level itself, so it is noted only.
    if (mode_number != mode_time_trial && (load16(rdram, level_count) > 0 || load16(rdram, stage_count) > 0 || load16(rdram, saved_page_flag) != 0))
        log("note", request, "saved_progress_page_skipped");

    // 2. The player page's hand-over, with the grid gone as after its fly-out.
    clear_menu_presentation(rdram);
    for (std::uint32_t i = 0; i < 9; ++i) store8(rdram, name_copy + i, load8(rdram, name + i));
    call(func_80065660, rdram, ctx);
    store16(rdram, chapter, static_cast<std::int16_t>(request.chapter));
    store16(rdram, next_level, static_cast<std::int16_t>(request.level));
    store16(rdram, new_game_flag, 1);
    call(func_800CC6E0, rdram, ctx);
    store32(rdram, after_new_game, 0);
    store16(rdram, p1_level, static_cast<std::int16_t>(request.level));
    close_menus(rdram);
    log("started", request);
    return true;
}

} // namespace

namespace direct_start {

bool request(const DirectStartRequest& request) {
    std::lock_guard lock(mutex);
    if (waiting) return false;
    waiting = request;
    outcome.reset();
    return true;
}

bool pending() {
    std::lock_guard lock(mutex);
    return waiting.has_value();
}

std::optional<Result> take_result() {
    std::lock_guard lock(mutex);
    auto result = outcome;
    outcome.reset();
    return result;
}

void cancel() {
    std::lock_guard lock(mutex);
    if (waiting) {
        log("refused", *waiting, "timed_out");
        finish(Result::Refused);
    }
}

bool enabled() {
    static const bool on = [] {
        const char* value = std::getenv("BLOCKPLANET_LAUNCH");
        return value == nullptr || std::strcmp(value, "menus") != 0;
    }();
    return on;
}

} // namespace direct_start
} // namespace tetrisphere

extern "C" void blockplanet_direct_start(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace tetrisphere;
    using namespace tetrisphere::guest;
    std::lock_guard lock(mutex);
    if (!waiting) return;
    const auto request = *waiting;
    // Only from the original main menu at rest: not in play, not inside a
    // page or while one closes, not while the grid flies, and not when the
    // game starts Training itself. Otherwise wait (BlockPlanet gives up after
    // two seconds and uses the menus).
    const auto grid = static_cast<std::uint16_t>(load16(rdram, grid_state));
    const char* busy = load16(rdram, main_row) < 0           ? "in_play"
                       : load16(rdram, submenu_row) >= 0     ? "submenu_open"
                       : load16(rdram, player_page_row) >= 0 ? "player_page_open"
                       : load16(rdram, mode_row) >= 0        ? "mode_page_open"
                       : load16(rdram, page_exit) != 0       ? "page_closing"
                       : load8(rdram, training_jump) != 0    ? "training_jump"
                       : (grid != grid_none && grid != grid_shown) ? "grid_moving"
                                                                   : nullptr;
    static const char* last_busy = nullptr;
    if (busy != nullptr) {
        if (busy != last_busy) {  // once per reason, for diagnosis
            std::fprintf(stdout,
                         "{\"event\":\"direct_start\",\"status\":\"waiting\",\"reason\":\"%s\",\"main_row\":%d,"
                         "\"submenu_row\":%d,\"page_exit\":%d,\"grid\":%d}\n",
                         busy, load16(rdram, main_row), load16(rdram, submenu_row), load16(rdram, page_exit), grid);
            std::fflush(stdout);
        }
        last_busy = busy;
        return;
    }
    last_busy = nullptr;
    if (load16(rdram, title_done) == 0) {
        // Still on the game's own title: leave it as the title does.
        call(func_8007ADF8, rdram, ctx, jingle_player);
        call(func_8007AEE0, rdram, ctx, effects_player);
        store16(rdram, title_left, 1);
        call(func_8003E460, rdram, ctx);  // player 1's controller
        store32(rdram, menu_scene_copy, load32(rdram, menu_scene_source));
        store16(rdram, title_done, 1);
        store16(rdram, title_exits, static_cast<std::int16_t>(load16(rdram, title_exits) + 1));
        store32(rdram, menu_timer_a, 0);
        store32(rdram, menu_timer_b, 0);
        log("note", request, "from_title");
    }
    bool started = false;
    switch (request.mode) {
    case DirectMode::Rescue: started = start_single(rdram, ctx, request, mode_rescue); break;
    case DirectMode::Puzzle: started = start_single(rdram, ctx, request, mode_puzzle); break;
    case DirectMode::Lines: started = start_single(rdram, ctx, request, mode_lines); break;
    case DirectMode::Training: started = start_training(rdram, ctx, request); break;
    }
    finish(started ? direct_start::Result::Started : direct_start::Result::Refused);
}
