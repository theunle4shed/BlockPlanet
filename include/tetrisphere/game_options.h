#pragma once

// Gameplay options applied by writing game memory from the host:
//
// - Casual play: hearts never run out and the Time Trial clock never reaches
//   zero, so there is no game over.
// - Start chapter: Rescue begins at a chosen chapter, for testing later
//   stages and as the base for level select.
//
// Both pause trophies, the same as for an online guest.

#include <cstdint>
#include <optional>

namespace tetrisphere {

namespace game_memory {
constexpr std::uint32_t hearts_shown_offset = 0x30FC;  // s16, follows hearts left
constexpr std::uint32_t heart_state_offset = 0x2FFC;   // u8 x3, stride 0x20: 0 heart, 1 turning, 2 skull
constexpr std::uint32_t clock_offset = 0x2B0C;         // s32 Time Trial clock, 1/60 s
constexpr std::int32_t casual_clock_floor = 60 * 30;   // keep at least 30 seconds
}

struct GameOptions {
    bool casual = false;
    std::optional<int> start_chapter;  // 1-based, as shown on screen
};

class GameOptionApplier {
public:
    explicit GameOptionApplier(GameOptions options) : options_(options) {}
    // Call often (every pass of the main loop). Writes only what is needed.
    void apply(std::uint8_t* rdram);
    // True when an option changes the game in a way that should not award trophies.
    bool affects_awards() const { return options_.casual || options_.start_chapter.has_value(); }

    // The player chose a chapter (1-based) in the chapter select: Rescue
    // begins there. Written until play begins; does not affect trophies.
    void choose_chapter(int chapter) { chosen_chapter_ = chapter; chosen_level_.reset(); }
    // The same with a level in the chapter (1-based; for Puzzle, the puzzle
    // number), for Restart level.
    void choose_start(int chapter, int level) { chosen_chapter_ = chapter; chosen_level_ = level; }

private:
    GameOptions options_;
    bool start_chapter_done_ = false;
    std::optional<int> chosen_chapter_;
    std::optional<int> chosen_level_;
};

} // namespace tetrisphere
