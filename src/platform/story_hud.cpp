#include "tetrisphere/story_hud.h"

#include "tetrisphere/guest_memory.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <iterator>

namespace tetrisphere {
namespace {

constexpr std::uint32_t player1 = 0x80110220u;  // player 1's context
constexpr int board_size = 32;
constexpr int layer_cells = board_size * board_size;
constexpr int layers = 9;

} // namespace

int weakest_square_blocks(const BoardView& board) {
    if (board.cells.size() < std::size_t(layers * layer_cells)) return 0;
    const int s = std::clamp(board.square, 1, board_size);
    // Blocks above the core in each column.
    int column[layer_cells] = {};
    for (int layer = 1; layer < layers; ++layer)
        for (int cell = 0; cell < layer_cells; ++cell)
            if (board.cells[std::size_t(layer * layer_cells + cell)] != 0) ++column[cell];
    int best = INT_MAX;
    for (int y = 0; y < board_size; y += 2) {
        for (int x = 0; x < board_size; x += 2) {
            int total = 0;
            for (int dy = 0; dy < s; ++dy)
                for (int dx = 0; dx < s; ++dx)
                    total += column[((y + dy) & 31) * board_size + ((x + dx) & 31)];
            best = std::min(best, total);
        }
    }
    return best;
}

int top_layer(const BoardView& board) {
    if (board.cells.size() < std::size_t(layers * layer_cells)) return 1;
    for (int layer = layers - 1; layer >= 1; --layer)
        for (int cell = 0; cell < layer_cells; ++cell)
            if (board.cells[std::size_t(layer * layer_cells + cell)] != 0) return layer;
    return 1;
}

float ProgressTracker::update(int level_key, int weakest_now) {
    if (level_key != key_) {
        key_ = level_key;
        start_ = weakest_now;
        settling_ = 120;
    }
    // The board may still be filling as the level starts: for the first
    // updates (about two seconds), keep the largest count as the start.
    if (settling_ > 0) {
        --settling_;
        start_ = std::max(start_, weakest_now);
    }
    if (start_ <= 0) return 1.0f;
    return std::clamp(1.0f - float(weakest_now) / float(start_), 0.0f, 1.0f);
}

bool read_story_hud(const std::uint8_t* rdram, ProgressTracker& progress, StoryHudState& out) {
    using namespace guest;
    const int mode = load16(rdram, 0x800E44A8);
    // Story, Training (its HUD reveals parts as they are taught), Hide & Seek, Lines
    if (load16(rdram, 0x800E121C) != -1 || (mode != 1 && mode != 3 && mode != 5 && mode != 9)) return false;
    out = {};
    out.mode = mode;
    const int hearts = load16(rdram, player1 + 0x2D7E);
    if (hearts < 0) return false;  // game over (-1): the game's own screens follow
    out.hearts = std::clamp(hearts, 0, 3);
    {
        static DropClock clock;
        const std::uint32_t bits = load32(rdram, player1 + 0x2408);
        float timer = 0.0f;
        std::memcpy(&timer, &bits, 4);
        if (!(timer >= 0.0f && timer <= 7000.0f)) timer = 0.0f;
        out.drop_left = clock.update(timer, load16(rdram, player1 + 0x2414), load16(rdram, player1 + 0x2416),
                                     static_cast<int>(load32(rdram, player1 + 0x2404) >> 16), load16(rdram, 0x800E44A0) + 1);
        out.drop_zoom = clock.zoom();
    }
    out.magic = static_cast<std::int8_t>(load8(rdram, player1 + 0x702));
    if (out.magic > 5) out.magic = 5;
    const int meter_full = load16(rdram, player1 + 0x2AEE);
    const int meter = static_cast<std::uint16_t>(load16(rdram, player1 + 0x2AEC));
    out.meter = meter_full > 0 ? std::clamp(float(meter) / float(meter_full), 0.0f, 1.0f) : 0.0f;
    for (int slot = 0; slot < 3; ++slot) {
        const int index = load16(rdram, player1 + 0x2374 + 2u * slot);
        if (index < 0 || index >= 48) continue;
        HudPiece piece;
        piece.random_block = index == 22;
        const std::uint32_t record = load32(rdram, 0x8014CBC8u + 4u * index);
        if (record >= 0x80000000u && record < 0x80800000u) {
            piece.material = load16(rdram, record + 2);
            piece.cells = load16(rdram, record + 4);
            piece.shape = load8(rdram, record + 8);
        }
        out.next.push_back(piece);
    }
    const std::uint32_t distance_bits = load32(rdram, player1 + 0x2420);
    float distance = 0.0f;
    std::memcpy(&distance, &distance_bits, 4);
    if (distance > 100.0f && distance < 100000.0f) out.camera_distance = distance;
    out.score = load32(rdram, player1 + 0x2AF8);
    out.combo = load16(rdram, player1 + 0x2AE0);
    out.cursor_x = load16(rdram, player1 + 0x2B2A);
    out.cursor_y = load16(rdram, player1 + 0x2B2E);
    out.chapter = load16(rdram, 0x800E44A0) + 1;
    if (const int layers = load16(rdram, 0x8013D17C); layers >= 1 && layers <= 8) out.layers = layers;
    out.level = load16(rdram, player1 + 0x23BA) + 1;

    const std::uint32_t board_address = load32(rdram, 0x800E44D0);
    if (board_address >= 0x80000000u && board_address < 0x80800000u - layers * layer_cells * 2) {
        BoardView board;
        board.square = load16(rdram, player1 + 0x23E4);
        board.cells.resize(std::size_t(layers * layer_cells));
        for (int i = 0; i < layers * layer_cells; ++i) board.cells[std::size_t(i)] = load16(rdram, board_address + 2u * i);
        out.progress = progress.update(out.chapter * 100 + out.level, weakest_square_blocks(board));
        out.top_layer = top_layer(board);
        out.cells = std::move(board.cells);
    }
    if (mode == 5) out.progress = -1.0f;  // Hide & Seek: no progress bar (it would hint where the pictures are)
    return true;
}

std::optional<ComboEvent> ComboTracker::update(const StoryHudState& state) {
    std::optional<ComboEvent> done;
    if (state.cells.size() != std::size_t(layers * layer_cells)) return done;
    int cleared = 0;
    constexpr double step = 6.283185307179586 / board_size;
    if (last_.size() == state.cells.size()) {
        // A piece landing (cells filling, outside a combo) marks the front of
        // the sphere: the drop lands at the cursor, which faces the camera.
        double lsx = 0, lcx = 0, lsy = 0, lcy = 0;
        int landed = 0;
        for (int layer = 1; layer < layers && !active_; ++layer) {
            for (int cell = 0; cell < layer_cells; ++cell) {
                const auto i = std::size_t(layer * layer_cells + cell);
                if (last_[i] != 0 || state.cells[i] == 0) continue;
                ++landed;
                lsx += std::sin((cell % board_size) * step);
                lcx += std::cos((cell % board_size) * step);
                lsy += std::sin((cell / board_size) * step);
                lcy += std::cos((cell / board_size) * step);
            }
        }
        if (landed > 0 && landed <= 6) {
            front_x_ = std::atan2(lsx, lcx) / step;
            front_y_ = std::atan2(lsy, lcy) / step;
        }
        for (int layer = 1; layer < layers; ++layer) {
            for (int cell = 0; cell < layer_cells; ++cell) {
                const auto i = std::size_t(layer * layer_cells + cell);
                if (last_[i] == 0 || state.cells[i] != 0) continue;
                ++cleared;
                const int x = cell % board_size, y = cell / board_size;
                const double ax = (x - front_x_) * step, ay = (y - front_y_) * step;
                sx_ += std::sin(ax);
                cx_ += std::cos(ax);
                sy_ += std::sin(ay);
                cy_ += std::cos(ay);
            }
        }
    }
    last_ = state.cells;
    const std::uint32_t previous_score = last_score_;
    last_score_ = state.score;
    if (cleared > 0 && !active_) score_at_start_ = previous_score;
    if (cleared > 0) {
        active_ = true;
        quiet_ = 0;
        blocks_ += cleared;
    } else if (active_ && ++quiet_ >= quiet_frames_) {
        ComboEvent event;
        event.blocks = blocks_;
        event.points = (static_cast<std::int64_t>(state.score) - static_cast<std::int64_t>(score_at_start_)) * 10;
        event.dx = static_cast<float>(std::atan2(sx_, cx_) / step);
        event.dy = static_cast<float>(std::atan2(sy_, cy_) / step);
        done = event;
        active_ = false;
        blocks_ = 0;
        sx_ = sy_ = cx_ = cy_ = 0;
    }
    return done;
}


std::optional<EpisodeSplits::Finished> EpisodeSplits::update(int chapter, int level, double play_seconds,
                                                             const std::vector<double>& best_levels) {
    std::optional<Finished> finished;
    if (chapter != chapter_ || level < level_) {
        // A new episode, or Try Again / Restart (back to an earlier level).
        const bool next_episode = chapter_ >= 0 && chapter == chapter_ + 1 && level == 1;
        if (next_episode && level_ > 0) {
            // The last level of the previous episode just finished.
            done_.push_back(current_);
            double total = 0.0;
            for (double t : done_) total += t;
            finished = Finished{chapter_, level_, current_, first_ == 1, total};
        }
        chapter_ = chapter;
        level_ = level;
        first_ = level;
        bests_ = best_levels;
        current_ = 0.0;
        done_.clear();
    } else if (level > level_) {
        done_.push_back(current_);
        finished = Finished{chapter_, level_, current_, false, 0.0};
        // Levels skipped (should not happen) count as nothing.
        for (int l = level_ + 1; l < level; ++l) done_.push_back(0.0);
        level_ = level;
        current_ = 0.0;
    }
    current_ += std::max(0.0, play_seconds);
    return finished;
}

SplitsView EpisodeSplits::view() const {
    const auto& best_levels = bests_;
    SplitsView v;
    v.chapter = chapter_;
    const auto best = [&](int level) {
        const auto i = std::size_t(level - 1);
        return i < best_levels.size() ? best_levels[i] : 0.0;
    };
    for (std::size_t i = 0; i < done_.size(); ++i) {
        SplitRow row;
        row.level = first_ + int(i);
        row.time = done_[i];
        row.done = true;
        const double b = best(row.level);
        row.has_best = b > 0.0;
        row.delta = row.has_best ? row.time - b : 0.0;
        if (row.has_best) v.has_delta = true, v.net_delta += row.delta;
        v.total += row.time;
        v.rows.push_back(row);
    }
    if (chapter_ >= 0) {
        SplitRow row;
        row.level = level_;
        row.time = current_;
        const double b = best(level_);
        row.has_best = b > 0.0;
        row.delta = row.has_best ? row.time - b : 0.0;
        v.total += current_;
        v.rows.push_back(row);
    }
    return v;
}

std::optional<ScoreSplits::Finished> ScoreSplits::update(int stage, std::int64_t score,
                                                         const std::vector<std::int64_t>& best_stages) {
    std::optional<Finished> finished;
    if (ended_ && stage_ >= 0 && stage >= stage_ && score >= score_) {
        score_ = score;
        return finished;  // the run is over; wait for a new one
    }
    if (stage_ < 0 || stage < stage_ || score < score_) {
        ended_ = false;
        // A new run: count from the score it starts with (Story's score runs
        // on across episodes; a restart may keep or reset it).
        stage_ = stage;
        first_ = stage;
        stage_start_ = run_start_ = score;
        done_.clear();
        bests_ = best_stages;
    } else if (stage > stage_) {
        done_.push_back(score_ - stage_start_);
        finished = Finished{stage_, score_ - stage_start_};
        stage_ = stage;
        stage_start_ = score_;
    }
    score_ = score;
    return finished;
}

void ScoreSplits::credit_finished(std::int64_t points) {
    if (done_.empty()) return;
    done_.back() += points;
    stage_start_ += points;
}

std::optional<ScoreSplits::Finished> ScoreSplits::finish() {
    if (stage_ < 0 || ended_) return std::nullopt;
    ended_ = true;
    done_.push_back(score_ - stage_start_);
    const Finished f{stage_, score_ - stage_start_};
    stage_start_ = score_;
    return f;
}

std::int64_t AttackClock::clear(bool next_stage) {
    const double carry = next_stage ? std::floor(left_ / 60.0) * 60.0 : 0.0;
    const double bonus = std::floor(std::max(0.0, left_ - carry));  // whole seconds
    left_ = stage_ + carry;
    played_ = 0.0;
    return static_cast<std::int64_t>(std::lround(bonus * points_per_second));
}

double AttackClock::combo_scale(double played) {
    // Minute by minute of the stage (docs/HUD_DESIGN.md has the table).
    static constexpr double scale[] = {1.0, 2.0 / 3.0, 1.0 / 4.0};
    const auto minute = static_cast<std::size_t>(std::max(0.0, played) / 60.0);
    return minute < std::size(scale) ? scale[minute] : 0.0;
}

double AttackClock::combo_seconds(int blocks, double played) {
    return std::clamp(blocks - 2, 0, 10) * combo_scale(played);
}

ScoreSplitsView ScoreSplits::view() const {
    ScoreSplitsView v;
    const auto best = [&](int stage) {
        const auto i = std::size_t(stage - 1);
        return i < bests_.size() ? bests_[i] : std::int64_t{0};
    };
    for (std::size_t i = 0; i < done_.size(); ++i) {
        ScoreRow row;
        row.stage = first_ + int(i);
        row.points = done_[i];
        row.done = true;
        const auto b = best(row.stage);
        row.has_best = b > 0;
        row.delta = row.has_best ? row.points - b : 0;
        if (row.has_best) v.has_delta = true, v.net_delta += row.delta;
        v.rows.push_back(row);
    }
    if (stage_ >= 0 && !ended_) {
        ScoreRow row;
        row.stage = stage_;
        row.points = score_ - stage_start_;
        v.rows.push_back(row);
    }
    v.total = score_ - run_start_;
    return v;
}

namespace {
constexpr float fall_cap = 30845.0f;   // the double at 0x800F10B8
constexpr float drop_eased = 30690.0f; // the double at 0x800F10B0
constexpr float ease_frames = 10.0f;   // the eased value's lag behind the cap
}

float DropClock::frames_left(float t, float fall) const {
    const float rate = rate_ > 0.0f ? rate_ : 1.0f;
    const float limit = std::max(0.0f, 6000.0f - t) / rate;  // the timer's own end
    float frames = 0.0f;
    if (t < 2000.0f) {
        frames += (2000.0f - t) / rate;
        t = 2000.0f;
    }
    if (t < 4000.0f) {
        const float to_4000 = (4000.0f - t) / rate;
        const float need = std::max(0.0f, fall_cap - fall) / 25.0f;
        if (need <= to_4000) return std::min(limit, frames + need + ease_frames);
        frames += to_4000;
        fall += 25.0f * to_4000;
    }
    frames += std::max(0.0f, fall_cap - fall) / 75.0f + ease_frames;
    return std::min(limit, frames);
}

float DropClock::update(float timer, int fall, int eased, int speed, int chapter) {
    // The timer's speed: guessed from the episode (1 a frame in Episode 1,
    // about 6 in Episode 10), then measured from the fall, which moves a
    // known amount a frame.
    if (chapter != rate_chapter_) {
        rate_chapter_ = chapter;
        rate_ = 1.0f + 0.55f * float(std::max(0, chapter - 1));
    }
    if (speed > 0 && last_timer_ >= 0.0f && timer > last_timer_ && fall > last_fall_ && float(fall) < fall_cap - 100.0f &&
        float(last_fall_) > 0.0f) {
        const float frames = float(fall - last_fall_) / float(speed);
        if (frames >= 1.0f) {
            const float measured = (timer - last_timer_) / frames;
            if (measured > 0.1f && measured < 50.0f) rate_ = rate_ * 0.8f + measured * 0.2f;
        }
    }
    // The drop: the fall set back (before the timer is), or the timer at its
    // end. Empty until the next piece.
    if ((last_timer_ >= 0.0f && fall + 500 < last_fall_) || timer >= 5999.0f) dropped_ = true;
    // A new piece: the timer back near 0 (it starts again after each drop).
    if (timer < 1.0f || (last_timer_ >= 0.0f && timer + 500.0f < last_timer_)) {
        base_ = fall;
        total_ = 0.0f;
        dropped_ = false;
    }
    if (speed == 0 && timer < 2000.0f) base_ = fall;
    last_timer_ = timer;
    last_fall_ = fall;
    zoom_ = eased > base_ + 25;
    if (timer < 1.0f) return 1.0f;
    if (dropped_) return 0.0f;
    const float left = frames_left(timer, float(fall));
    if (total_ <= 0.0f) total_ = frames_left(0.0f, float(base_));
    total_ = std::max(total_, left);
    return std::clamp(left / total_, 0.0f, 1.0f);
}
} // namespace tetrisphere
