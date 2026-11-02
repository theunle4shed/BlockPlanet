#pragma once

// Turns changes in a few known game variables into trophy GameEvents.
//
// The watcher reads guest memory from the host side, so it needs no changes to
// the generated game code. Addresses were found by tracing the HUD code and
// recording memory during play; see docs/research/blockplanet-game-state.md.
// Event detection is polled, so it is limited to state that lasts longer than
// one poll. Events not listed here need further research.

#include "tetrisphere/trophies.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tetrisphere {

// Guest addresses, NTPE rev0. The player context is 0x80110220 for P1.
namespace game_state {
constexpr std::uint32_t scene = 0x800E121C;         // s16, -1 during play
constexpr std::uint32_t mode = 0x800E44A8;          // s16 game mode number
constexpr std::uint32_t p1_context = 0x80110220;
constexpr std::uint32_t p2_context = 0x80113488;
constexpr std::int16_t mode_vs_cpu = 6;
constexpr std::uint32_t score_offset = 0x2AF8;      // s32; the HUD shows score x 10
constexpr std::uint32_t hearts_offset = 0x2D7E;     // s16 hearts left; 3..1, -1 = out
constexpr std::uint32_t piece = 0x801127FB;         // u8 current piece; 8 while dropping
constexpr std::uint8_t piece_dropping = 8;
constexpr std::uint32_t chapter = 0x800E44A0;       // s16 Rescue / Hide & Seek chapter, 0-based
constexpr std::uint32_t level_offset = 0x23BA;      // s16 level within chapter, or puzzle number; 0-based
}

struct GameStateSnapshot {
    std::int16_t scene = 0;
    std::int16_t mode = 0;
    std::int32_t score = 0;
    std::int16_t hearts = 0;
    std::int16_t p2_hearts = 0;
    std::uint8_t piece = 0;
    std::int16_t chapter = 0;
    std::int16_t level = 0;
};

// Read a snapshot from RDRAM as laid out by N64Recomp (32-bit words in host
// order, so byte and halfword addresses are XOR-swizzled).
GameStateSnapshot read_game_state(const std::uint8_t* rdram);

// Trophy mode name for the game's mode number, or "" when not yet identified.
std::string game_mode_name(std::int16_t mode);

class GameEventWatcher {
public:
    // Compare with the previous snapshot and return the events that happened.
    std::vector<GameEvent> update(const GameStateSnapshot& now);

private:
    std::optional<GameStateSnapshot> previous_;
    // Last snapshot taken during play; the game leaves play between levels.
    std::optional<GameStateSnapshot> last_play_;
    std::int32_t best_score_this_game_ = 0;
    std::int64_t hearts_lost_this_level_ = 0;
};

} // namespace tetrisphere
