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
#include "tetrisphere/box_art.h"

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"
#include "tetrisphere/runtime.h"

#include <atomic>
#include <chrono>
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
void func_80066E40(std::uint8_t* rdram, recomp_context* ctx);
void func_80066DB8(std::uint8_t* rdram, recomp_context* ctx);
void func_80066F18(std::uint8_t* rdram, recomp_context* ctx);
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
constexpr std::uint32_t intro_done = 0x800E1648u;      // u16: 0 while the intro (func_80067798) plays
constexpr std::uint32_t title_phase = 0x800E1650u;     // u32: the title's phase
constexpr std::uint32_t arena_top = 0x800F22C0u;       // the scene memory's next free address
constexpr std::uint32_t intro_arena_mark = 0x801028D0u;  // where the intro started it (and the title rewinds to)
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
constexpr std::int16_t mode_hide_and_seek = 5;
constexpr std::int16_t mode_vs_cpu = 6;
// Versus against the computer: after its confirm the mode list opens the
// players page (0x800E122A = 0, func_80050BFC), which sets up both players
// and starts the game itself.
constexpr std::uint32_t players_page = 0x800E122Au;
constexpr std::uint32_t pause_row = 0x800E1228u;
constexpr std::uint32_t extra_row = 0x800E1226u;
constexpr std::uint32_t kept_next_level = 0x800E123Eu;  // the next level, kept across the new game
constexpr std::uint32_t two_humans = 0x800E088Cu;
constexpr std::uint32_t player_index_1 = 0x800E0B10u;
constexpr std::uint32_t player_index_2 = 0x800E0B14u;
constexpr std::uint32_t player_index_next = 0x800E0B18u;
constexpr std::uint32_t players_count = 0x800E0B0Cu;
constexpr std::uint32_t p2_port = 0x80116588u;       // player 2's context + 0x3100
constexpr std::uint32_t p2_character = 0x8011658Au;  // + 0x3102: the computer's robot, from the level
constexpr std::uint32_t p2_extra = 0x8011658Bu;      // + 0x3103: 1 for the computer
constexpr std::uint32_t p2_name = 0x80116590u;       // + 0x3108
constexpr std::uint32_t opponent_names = 0x800E08B4u;  // u32 per level: the computer's name
constexpr std::uint32_t music_player_jingle_a = 0x80108828u;
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
std::atomic<bool> intro_restart{false};
std::atomic<bool> title_restart{false};
// A profile change's cartridge save (under `mutex`).
std::optional<std::filesystem::path> save_switch;
std::optional<bool> save_switch_result;
constexpr std::uint32_t save_changed_flags = 0x800F2250u;  // u8 per block: waiting to be written
constexpr std::uint32_t save_reading = 0x800DF740u;       // u8: set while the save thread reads the save in
constexpr std::uint32_t save_copy = 0x80102918u;          // the game's copy of the save (512 bytes)
constexpr std::uint32_t save_checksum = 0x80102B14u;      // u32, as func_80066DB8 computes it
bool save_switch_reading = false;
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
    case DirectMode::HideAndSeek: return "hide_and_seek";
    case DirectMode::Versus: return "versus";
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

// Hide & Seek opens one more page after the hand-over (0x800E1234 = 0,
// func_8006224C): an animated intro (3D scenes, a random pick of seven, a
// sound and a fade) that sets nothing play uses, then clears every menu row
// (0x800E121C to 0x800E1236) for play. A direct start clears them at once.
void close_all_rows(std::uint8_t* rdram) {
    for (std::uint32_t row = main_row; row <= 0x800E1236u; row += 2) store16(rdram, row, -1);
}

// Versus: the mode list opens the players page (0x80046604: 0x800E122A = 0,
// the name copied to 0x800E0784, the timers cleared); the page's opening for
// Versus with one human (func_80050BFC from 0x80050D1C; its scenes and grid
// left out), its A press (0x80052D38, the sound left out) and its hand-over
// (0x80051B60).
void versus_players_page(std::uint8_t* rdram, recomp_context* ctx) {
    for (std::uint32_t i = 0; i < 9; ++i) store8(rdram, name_copy + i, load8(rdram, name + i));
    store32(rdram, menu_timer_a, 0);
    store32(rdram, menu_timer_b, 0);
    // Opening.
    store8(rdram, 0x80102688u, 0);
    store8(rdram, 0x80102690u, 0);
    store32(rdram, 0x800E1018u, 0);
    store32(rdram, player_index_1, 0);
    store32(rdram, player_index_2, 1);
    store32(rdram, players_count, 2);
    store16(rdram, level_count, 0);
    store16(rdram, next_level, 0);
    store32(rdram, player_index_next, 1);
    store32(rdram, 0x801026A8u, 0xFFFFFFFFu);
    store32(rdram, 0x801026ACu, 1);
    const std::uint32_t index = load32(rdram, player_index_1);
    store8(rdram, p1_port, load8(rdram, port_source + index));
    store8(rdram, p2_port, 0xFF);  // the computer
    store8(rdram, p1_character, load8(rdram, character_source + index));
    store8(rdram, p2_character, static_cast<std::uint8_t>(load16(rdram, next_level)));
    store8(rdram, p1_extra, load8(rdram, extra_source + index));
    store8(rdram, p2_extra, 1);
    store32(rdram, p1_name, name_copy + 9 * index);
    const std::int16_t opponent = load16(rdram, chapter) < 3 ? load16(rdram, next_level) : 7;
    store32(rdram, p2_name, load32(rdram, opponent_names + 4u * static_cast<std::uint32_t>(opponent)));
    store32(rdram, 0x800E1210u, 0);
    store16(rdram, page_exit, 0);
    for (const std::uint32_t player : {load32(rdram, player_index_1), load32(rdram, player_index_2)}) {
        const std::uint32_t entry = name_copy + 9 * player;
        if (load8(rdram, entry) != 0) continue;
        const char default_name[] = "PLAYER ";
        for (std::uint32_t i = 0; i < 7; ++i) store8(rdram, entry + i, static_cast<std::uint8_t>(default_name[i]));
        store8(rdram, entry + 7, static_cast<std::uint8_t>('1' + player));
        store8(rdram, entry + 8, 0);
    }
    // A pressed.
    call(func_8007ADF8, rdram, ctx, music_player_jingle_a);
    call(func_8007ADF8, rdram, ctx, jingle_player);
    store8(rdram, 0x8010B34Au, 0);
    store8(rdram, 0x80109DB2u, 0);
    call(func_8007AEE0, rdram, ctx, effects_player);
    // The hand-over, once the grid has gone.
    clear_menu_presentation(rdram);
    store16(rdram, pause_row, -1);
    store16(rdram, players_page, -1);
    close_menus(rdram);
    store16(rdram, extra_row, -1);
    store16(rdram, kept_next_level, load16(rdram, next_level));
    store16(rdram, new_game_flag, 1);
    call(func_800CC6E0, rdram, ctx);
    store16(rdram, next_level, load16(rdram, kept_next_level));
    store16(rdram, level_count, load16(rdram, kept_next_level));
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
    const std::int16_t row = mode_number == mode_rescue ? 0 : mode_number == mode_hide_and_seek ? 1
                           : mode_number == mode_puzzle ? 2 : mode_number == mode_time_trial ? 3
                           : mode_number == mode_vs_cpu ? 4 : 5;

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
    if (mode_number == mode_vs_cpu) {  // the confirm's two-player settings (0x800463C4)
        store16(rdram, 0x800E44A4u, 1);
        store16(rdram, flag_b, 2);
        store16(rdram, 0x801125F6u, 1);  // player 1's context + 0x23D6
        store16(rdram, 0x8011585Eu, 0);  // player 2's
        store16(rdram, 0x800E08A8u, 1);
        store16(rdram, 0x800E08ACu, 0);
        store16(rdram, 0x800E08B0u, 0);
        store32(rdram, two_humans, 0);
    }
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

    if (mode_number == mode_vs_cpu) {
        versus_players_page(rdram, ctx);
        log("started", request);
        return true;
    }

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
    if (mode_number == mode_hide_and_seek) close_all_rows(rdram);  // its intro page's exit
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

void request_intro_restart() { intro_restart.store(true); }
void request_title_restart() { title_restart.store(true); }

void request_save_switch(const std::filesystem::path& path) {
    std::lock_guard lock(mutex);
    save_switch = path;
    save_switch_result.reset();
}

std::optional<bool> take_save_switch_result() {
    std::lock_guard lock(mutex);
    auto result = save_switch_result;
    save_switch_result.reset();
    return result;
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


// The box art's flow (the owner's design, 2026-11-10; include/tetrisphere/box_art.h,
// docs/UI.md "The box art on the grid"), once a frame before the intro or
// title runs:
// - the logo or the intro, A or Start: the intro ends at once (as the box
//   art hand-over in box_art_hooks.cpp), straight to the box art;
// - the box art holds (the title's own 15 s timeout, 0x800E1208 at 901, is
//   held off) until a press or BlockPlanet's attract timer;
// - A or Start: the title leaves for the menus (allowed at once, though the
//   title itself waits 4 s), never into the game's Training for a new save;
// - B: the launch animation again (the intro restart);
// - the attract timer: the title leaves, the game's main menu (behind
//   BlockPlanet's grid) has its idle timer run out so it starts its demo, and
//   the game comes back to its title, and so the box art, when the demo ends.
namespace {

using namespace tetrisphere;

constexpr std::uint32_t buttons = 0x800DFD98u;          // the title's buttons word
constexpr std::uint32_t menu_idle_since = 0x800E1214u;  // u32: the menu timer at the last input
constexpr std::uint32_t demo_running = 0x800E11F8u;     // u16: another attract path (not the demos)
constexpr std::uint32_t save_read = 0x800DF770u;        // u8: the save has been read (the title waits for it)
constexpr std::uint16_t button_a = 0x8000u, button_b = 0x4000u, button_start = 0x1000u;

bool box_art_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_BOX_ART");
        return v == nullptr || v[0] != '0';
    }();
    return on;
}

// Seconds on the box art before the demo (BLOCKPLANET_ATTRACT_SECONDS; the title's own 15).
double attract_seconds() {
    static const double seconds = [] {
        const char* v = std::getenv("BLOCKPLANET_ATTRACT_SECONDS");
        return v ? std::atof(v) : 15.0;
    }();
    return seconds;
}

void end_intro_now(std::uint8_t* rdram) {
    using namespace tetrisphere::guest;
    store32(rdram, menu_timer_a, 0);
    store32(rdram, menu_timer_b, 0);
    store16(rdram, intro_done, 1);
    store32(rdram, arena_top, load32(rdram, intro_arena_mark));
}

void box_art_frame(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace tetrisphere;
    using namespace tetrisphere::guest;
    static std::uint16_t held = 0;
    static std::chrono::steady_clock::time_point since{};
    static bool on_box_art = false;
    const std::uint16_t now_held = static_cast<std::uint16_t>(load32(rdram, buttons) >> 16) | static_cast<std::uint16_t>(load32(rdram, buttons));
    const std::uint16_t pressed = static_cast<std::uint16_t>(now_held & ~held);
    held = now_held;
    if (!box_art_enabled()) return;
    const bool intro = load16(rdram, intro_done) == 0 && load16(rdram, title_done) == 0;
    const bool title = load16(rdram, intro_done) == 1 && load16(rdram, title_done) == 0 && load16(rdram, demo_running) == 0;
    auto phase = box_art::phase().load();
    // The demo has started playing: the tiles may fly out.
    if (phase == box_art::Phase::ToDemo && load16(rdram, main_row) == -1 &&
        (load16(rdram, mode) == 10 || load16(rdram, mode) == 3)) {
        box_art::phase().store(box_art::Phase::Demo);
        std::fprintf(stdout, "{\"event\":\"box_art_demo\"}\n");
        std::fflush(stdout);
    }
    if (intro) {
        on_box_art = false;
        if ((pressed & (button_a | button_start)) != 0 && load8(rdram, save_read) != 0) {
            end_intro_now(rdram);
            std::fprintf(stdout, "{\"event\":\"box_art_skip\"}\n");
            std::fflush(stdout);
        }
        return;
    }
    if (!title) {
        on_box_art = false;
        // On the way to the demo: in the game's main menu, its idle timer is
        // run out (the time of the last input, 0x800E1214, set far back), so
        // the menu starts its demo itself (func at 0x8004FB3C: mode 10, or 3
        // every sixth time, then its grid flies out), as after sitting idle.
        if (phase == box_art::Phase::ToDemo && load16(rdram, title_done) == 1 && load16(rdram, main_row) >= 0 &&
            load16(rdram, submenu_row) < 0)
            store32(rdram, menu_idle_since, load32(rdram, menu_timer_a) - 32000u);
        return;
    }
    // On the box art.
    if (!on_box_art) {
        on_box_art = true;
        since = std::chrono::steady_clock::now();
        // Back from the demo: the title from its start (it builds its scenes
        // only from phase 0, and the held letters never let it get there).
        if (phase != box_art::Phase::None) title_restart.store(true);
        box_art::phase().store(box_art::Phase::None);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
    if ((pressed & button_b) != 0) {
        intro_restart.store(true);
        std::fprintf(stdout, "{\"event\":\"box_art_back\"}\n");
        std::fflush(stdout);
        return;
    }
    if ((pressed & (button_a | button_start)) != 0) {
        // Leave the title as it does on a press (0x80069500), for its menus,
        // even with a brand-new save (no Training jump). Done here: the
        // title reads its buttons after they are polled again this frame.
        store8(rdram, training_jump, 0);
        call(func_8007ADF8, rdram, ctx, jingle_player);
        call(func_8007AEE0, rdram, ctx, effects_player);
        store16(rdram, title_left, 1);
        call(func_8003E460, rdram, ctx);  // player 1's controller
        store32(rdram, menu_scene_copy, load32(rdram, menu_scene_source));
        store16(rdram, title_done, 1);
        store16(rdram, title_exits, static_cast<std::int16_t>(load16(rdram, title_exits) + 1));
        store32(rdram, menu_timer_a, 0);
        store32(rdram, menu_timer_b, 0);
        std::fprintf(stdout, "{\"event\":\"box_art_press\"}\n");
        std::fflush(stdout);
        return;
    }
    if (seconds >= attract_seconds()) {
        store8(rdram, training_jump, 0);
        store32(rdram, menu_timer_a, 901);  // the title's own timeout: it leaves for its menus
        box_art::phase().store(box_art::Phase::ToDemo);
        std::fprintf(stdout, "{\"event\":\"box_art_attract\"}\n");
        std::fflush(stdout);
        return;
    }
    if (static_cast<std::int32_t>(load32(rdram, menu_timer_a)) > 880) store32(rdram, menu_timer_a, 880);
}

} // namespace

extern "C" void blockplanet_direct_start(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace tetrisphere;
    using namespace tetrisphere::guest;
    box_art_frame(rdram, ctx);
    // Back to the box art (Back from "Who is playing?"): the title from its
    // start, in its scene memory's place, held as the box art again.
    if (title_restart.load() && load16(rdram, main_row) >= 0 && load16(rdram, intro_done) == 1) {
        title_restart.store(false);
        store32(rdram, arena_top, load32(rdram, intro_arena_mark));
        store16(rdram, title_done, 0);
        store32(rdram, title_phase, 0);
        store16(rdram, title_left, 0);
        store32(rdram, menu_timer_a, 0);
        store32(rdram, menu_timer_b, 0);
        store32(rdram, 0x800E1210u, 0);
        for (std::uint32_t row = main_row; row <= 0x800E1236u; row += 2) store16(rdram, row, -1);
        store16(rdram, main_row, 0);
        store16(rdram, page_exit, 0);
        clear_menu_presentation(rdram);
        std::fprintf(stdout, "{\"event\":\"title_restart\"}\n");
        std::fflush(stdout);
    }
    // Back to the intro (Back from "Who is playing?"): as at boot, the intro's
    // scene memory rewound to where it started it, the intro, title and their
    // counters from the start, no menu open and no grid; then the loop goes
    // on into the intro this same frame.
    if (intro_restart.load() && load16(rdram, main_row) >= 0) {
        intro_restart.store(false);
        store32(rdram, arena_top, load32(rdram, intro_arena_mark));
        store16(rdram, intro_done, 0);
        store16(rdram, title_done, 0);
        store32(rdram, title_phase, 0);
        store16(rdram, title_exits, 0);
        store16(rdram, title_left, 0);
        store32(rdram, menu_timer_a, 0);
        store32(rdram, menu_timer_b, 0);
        store32(rdram, 0x800E1210u, 0);
        for (std::uint32_t row = main_row; row <= 0x800E1236u; row += 2) store16(rdram, row, -1);
        store16(rdram, main_row, 0);
        store16(rdram, page_exit, 0);
        clear_menu_presentation(rdram);
        std::fprintf(stdout, "{\"event\":\"intro_restart\"}\n");
        std::fflush(stdout);
    }
    blockplanet_box_art_update(rdram);  // the under layer follows the title (box_art_hooks.cpp)
    std::lock_guard lock(mutex);
    // A profile change: when no block of the save is waiting to be written
    // (the save thread only writes while the game's thread waits, so none
    // can start now), the save behind the game changes and the game reads it
    // in again (func_80066E40, as the intro does at start-up), which also
    // refreshes the save thread's copy of what it last wrote.
    if (save_switch && !save_switch_reading && load16(rdram, main_row) >= 0) {
        bool waiting_write = false;
        for (std::uint32_t block = 0; block < 64; ++block)
            if (load8(rdram, save_changed_flags + block) != 0) waiting_write = true;
        if (!waiting_write) {
            std::string error;
            const bool ok = initialize_eeprom(*save_switch, error);
            if (ok) {
                call(func_80066E40, rdram, ctx);
                save_switch_reading = true;
            } else {
                std::fprintf(stdout, "{\"event\":\"save_switch\",\"status\":\"failed\"}\n");
                std::fprintf(stderr, "Could not load the profile's save: %s\n", error.c_str());
                std::fflush(stdout);
                save_switch.reset();
                save_switch_result = false;
            }
        }
    }
    // Once read in, the save is checked as at start-up (func_80067434: its
    // checksum and the "H2O" mark); a new or damaged one is given the game's
    // defaults (func_80066F18, which also writes it). The start-up check's
    // steer towards Training for a new player (its menu row and the game's
    // Training start) is left to BlockPlanet's own Training offers.
    if (save_switch_reading && load8(rdram, save_reading) == 0) {
        save_switch_reading = false;
        const auto checksum = static_cast<std::uint32_t>(call(func_80066DB8, rdram, ctx));
        const bool valid = checksum == load32(rdram, save_checksum) && load8(rdram, save_copy + 1) == 'H' &&
                           load8(rdram, save_copy + 2) == '2' && load8(rdram, save_copy + 3) == 'O';
        if (!valid) call(func_80066F18, rdram, ctx);
        std::fprintf(stdout, "{\"event\":\"save_switch\",\"guest\":%s,\"status\":\"done\",\"new_save\":%s}\n",
                     save_switch->empty() ? "true" : "false", valid ? "false" : "true");
        std::fflush(stdout);
        save_switch.reset();
        save_switch_result = true;
    }
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
    // A new save's start-up check asks the game to start Training (the byte
    // at 0x800DFE8C); the player has chosen a mode in BlockPlanet's menus.
    store8(rdram, training_jump, 0);
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
    case DirectMode::HideAndSeek: started = start_single(rdram, ctx, request, mode_hide_and_seek); break;
    case DirectMode::Versus: started = start_single(rdram, ctx, request, mode_vs_cpu); break;
    case DirectMode::Training: started = start_training(rdram, ctx, request); break;
    }
    finish(started ? direct_start::Result::Started : direct_start::Result::Refused);
}
