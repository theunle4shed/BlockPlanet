#include "tetrisphere/game_options.h"

#include "tetrisphere/game_event_watcher.h"

#include <cstring>

namespace tetrisphere {
namespace {

std::uint8_t* at(std::uint8_t* rdram, std::uint32_t address, unsigned swizzle) {
    return rdram + ((address & 0x00FFFFFFu) ^ swizzle);
}

void set16(std::uint8_t* rdram, std::uint32_t address, std::int16_t value) {
    std::memcpy(at(rdram, address, 2), &value, 2);
}

std::int16_t get16(std::uint8_t* rdram, std::uint32_t address) {
    std::int16_t value;
    std::memcpy(&value, at(rdram, address, 2), 2);
    return value;
}

std::int32_t get32(std::uint8_t* rdram, std::uint32_t address) {
    std::int32_t value;
    std::memcpy(&value, at(rdram, address, 0), 4);
    return value;
}

void set32(std::uint8_t* rdram, std::uint32_t address, std::int32_t value) {
    std::memcpy(at(rdram, address, 0), &value, 4);
}

} // namespace

void GameOptionApplier::apply(std::uint8_t* rdram) {
    if (rdram == nullptr) return;
    const auto state = read_game_state(rdram);
    const bool playing = state.scene == -1;

    // The chapter must be in place before the level is built, so keep writing
    // it until the first moment of play, then let the game advance normally.
    if (options_.start_chapter && !start_chapter_done_) {
        if (playing) start_chapter_done_ = true;
        else set16(rdram, game_state::chapter, static_cast<std::int16_t>(*options_.start_chapter - 1));
    }
    if (chosen_chapter_) {
        if (playing) {
            chosen_chapter_.reset();
            chosen_level_.reset();
            level_count_written_ = level_count_taken_ = false;
        } else {
            // Chapter 0 or less: the level only (Hide & Seek keeps its own chapter).
            if (*chosen_chapter_ > 0)
                set16(rdram, game_state::chapter, static_cast<std::int16_t>(*chosen_chapter_ - 1));
            // Story: the game's next-level counter (0x800E449C, copied to
            // 0x800E4498 and the shown level as each level is set up, then
            // counted on), so the chosen level really starts there and the
            // next one follows it. Written until the set-up has taken it
            // (the counter then reads the level after it).
            if (chosen_level_ && *chosen_chapter_ > 0 && !level_count_taken_) {
                if (level_count_written_ && get16(rdram, game_state::next_level) == *chosen_level_) {
                    level_count_taken_ = true;
                } else {
                    set16(rdram, game_state::next_level, static_cast<std::int16_t>(*chosen_level_ - 1));
                    level_count_written_ = true;
                }
            }
            if (chosen_level_)
                set16(rdram, game_state::p1_context + game_state::level_offset,
                      static_cast<std::int16_t>(*chosen_level_ - 1));
        }
    }

    if (!(options_.casual || menu_casual_) || !playing || state.mode == game_state::mode_vs_cpu) return;
    const auto context = game_state::p1_context;
    // Hearts only ever drop by one at a time, so refilling as soon as one is
    // lost keeps them from reaching -1 (game over).
    if (state.hearts >= 0 && state.hearts < 3) {
        set16(rdram, context + game_state::hearts_offset, 3);
        set16(rdram, context + game_memory::hearts_shown_offset, 3);
        for (std::uint32_t i = 0; i < 3; ++i)
            *at(rdram, context + game_memory::heart_state_offset + 0x20 * i, 3) = 0;
    }
    if (state.mode == 2) {  // Time Trial
        const auto clock = context + game_memory::clock_offset;
        if (get32(rdram, clock) < game_memory::casual_clock_floor)
            set32(rdram, clock, game_memory::casual_clock_floor);
    }
}

} // namespace tetrisphere
