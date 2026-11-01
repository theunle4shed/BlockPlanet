#include "tetrisphere/game_event_watcher.h"

#include <algorithm>
#include <cstring>

namespace tetrisphere {
namespace {

std::uint32_t physical(std::uint32_t address) { return address & 0x00FFFFFFu; }

std::uint8_t read_u8(const std::uint8_t* rdram, std::uint32_t address) {
    return rdram[physical(address) ^ 3u];
}

std::int16_t read_s16(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint16_t value = 0;
    std::memcpy(&value, rdram + (physical(address) ^ 2u), sizeof(value));
    return static_cast<std::int16_t>(value);
}

std::int32_t read_s32(const std::uint8_t* rdram, std::uint32_t address) {
    std::int32_t value = 0;
    std::memcpy(&value, rdram + physical(address), sizeof(value));
    return value;
}

GameEvent event(const char* name, const std::string& mode) {
    GameEvent result{name, {}};
    if (!mode.empty()) result.fields["mode"] = mode;
    return result;
}

} // namespace

GameStateSnapshot read_game_state(const std::uint8_t* rdram) {
    using namespace game_state;
    GameStateSnapshot snapshot;
    snapshot.scene = read_s16(rdram, scene);
    snapshot.mode = read_s16(rdram, mode);
    snapshot.score = read_s32(rdram, p1_context + score_offset);
    snapshot.hearts = read_s16(rdram, p1_context + hearts_offset);
    snapshot.p2_hearts = read_s16(rdram, p2_context + hearts_offset);
    snapshot.piece = read_u8(rdram, piece);
    return snapshot;
}

std::string game_mode_name(std::int16_t mode) {
    switch (mode) {
        case 1: return "rescue";
        case 2: return "time_trial";
        case 3: return "training";
        case 4: return "puzzle";
        case 5: return "hide_and_seek";
        case 6: return "vs_cpu";
        default: return "";
    }
}

std::vector<GameEvent> GameEventWatcher::update(const GameStateSnapshot& now) {
    std::vector<GameEvent> events;
    const bool playing = now.scene == -1;
    // Entering play with full hearts and no score is a fresh game. The hearts
    // refill in the same frame the first game becomes playable.
    if (playing && (!previous_ || previous_->scene != -1) && now.hearts == 3 && now.score == 0) {
        events.push_back(event("mode_started", game_mode_name(now.mode)));
        best_score_this_game_ = 0;
    }
    if (previous_ && playing && previous_->scene == -1) {
        const auto& before = *previous_;
        const auto mode = game_mode_name(now.mode);
        const bool vs = now.mode == game_state::mode_vs_cpu;
        // A fresh game resets the hearts to three. In VS they refill every round.
        if (!vs && now.hearts == 3 && before.hearts != 3) {
            events.push_back(event("mode_started", mode));
            best_score_this_game_ = 0;
        }
        if (now.piece == game_state::piece_dropping && before.piece != game_state::piece_dropping)
            events.push_back(event("piece_placed", mode));
        if (now.hearts < before.hearts && before.hearts > 0 && before.hearts <= 3)
            events.push_back(event("life_lost", mode));
        best_score_this_game_ = std::max(best_score_this_game_, now.score);
        if (vs) {
            // A player whose hearts run out loses the round. P2's hearts are
            // inferred from P1's layout and not yet confirmed in a recording.
            auto round = [&](const char* result) {
                GameEvent e{"vs_round", {{"mode", mode}, {"result", std::string(result)}}};
                events.push_back(std::move(e));
            };
            if (now.hearts == -1 && before.hearts != -1) round("loss");
            else if (now.p2_hearts == -1 && before.p2_hearts != -1) round("win");
        } else if (now.hearts == -1 && before.hearts != -1) {
            auto over = event("game_over", mode);
            // Report the score as the HUD shows it.
            over.fields["score"] = static_cast<std::int64_t>(best_score_this_game_) * 10;
            events.push_back(std::move(over));
        }
    }
    previous_ = now;
    return events;
}

} // namespace tetrisphere
