#pragma once

// What BlockPlanet's Story HUD shows, read from game memory
// (docs/HUD_DESIGN.md; addresses in docs/research/blockplanet-game-state.md).

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tetrisphere {

struct HudPiece {
    int shape = 0;     // the shape record's ID (+8), the same in every level
    int material = 0;  // the block material (+2), which sets the colour
    int cells = 0;
    bool random_block = false;  // the "?" piece
};

struct StoryHudState {
    int mode = 1;              // 1 Story (Rescue; Story's sub-modes all run it), 5 Hide & Seek, 9 Lines
    int hearts = 0;            // 0 to 3
    float drop_left = 1.0f;    // the drop timer's time left, 1 (full) to 0
    int magic = -1;            // -1 none, 0 to 5 the held magic
    float meter = 0.0f;        // the magic meter, 0 to 1
    std::vector<HudPiece> next;  // the next pieces, next first
    int chapter = 0;           // 1-based
    int level = 0;             // 1-based
    float progress = -1.0f;    // 0 to 1, or -1 before it is known
    int top_layer = 1;         // the highest board layer holding a block (1 to 8)
    int layers = 0;            // the stage's playfield layers (level structure 0x8013D17C), 0 if unknown
    float camera_distance = 2424.0f;  // player context +0x2420
    std::vector<std::int16_t> cells;  // the board (9 layers x 1024), for ComboTracker
    std::uint32_t score = 0;          // player context +0x2AF8 (shown x 10)
    int combo = 0;                    // the running combo count (+0x2AE0)
    int cursor_x = 0, cursor_y = 0;   // the drop cursor (+0x2B2A, +0x2B2E)
    bool drop_zoom = false;           // the drop timer's fall is pulling the view in (or springing back)
    // The piece's whole time to the drop in 60 Hz steps, and the steps from
    // its start to the two warnings (the fall starting, then speeding up).
    int drop_steps = 0, drop_warn_first = -1, drop_warn_second = -1;
    // The bar in two stages (the owner's choice, 2026-11-19): 0 calm (the
    // time to the first warning), 1 falling (the time to the drop), 2 falling
    // fast (after the second warning, or the last 2 seconds where the piece
    // drops before it). The stage's time left (1 to 0), the seconds to the
    // drop, and where the second warning falls on the falling stage (-1 none).
    int drop_phase = 0;
    float drop_stage_left = 1.0f, drop_seconds = 0.0f, drop_fall_notch = -1.0f;
};

// The time left until the game drops the piece itself (found 2026-11-19,
// docs/research/blockplanet-drop-timer.md). Each 60 Hz step the timer
// (+0x2408) gains the episode's speed (+0x3254: 1, 2 or 3 ...); at 2,000 the
// fall starts (+0x2404 = 25) and at 4,000 speeds up (75); the fall (+0x2414)
// gains that speed each step up to 30,845; its eased copy (+0x2416) gains a
// ninth of the gap plus 1; the piece drops when the eased copy reaches 30,690,
// or when the timer reaches 6,000. Where the piece would land makes no
// difference. This steps through the same rules to the drop.
struct DropRun {
    int steps = 0;         // 60 Hz steps to the drop
    int warn_first = -1;   // steps to the timer's 2,000 (the fall starts), or -1 if passed
    int warn_second = -1;  // steps to 4,000 (the fall speeds up), or -1 if passed
};
DropRun drop_run(float timer, float speed_per_step, int fall, int eased);

class DropClock {
public:
    // Returns the time left, 1 (a new piece) to 0 (the drop).
    float update(float timer, float speed_per_step, int fall, int eased);
    // While the fall pulls the view in, and until it has sprung back.
    bool zoom() const { return zoom_; }
    // The piece's whole time and where its warnings fall (from its start).
    const DropRun& whole() const { return whole_; }
    int phase() const { return phase_; }
    float stage_left() const { return stage_left_; }
    float seconds() const { return seconds_; }
    float fall_notch() const { return fall_notch_; }

private:
    float last_timer_ = -1.0f;
    int last_fall_ = 0;
    int base_ = 0;           // the fall's value when the piece came
    DropRun whole_{};
    bool zoom_ = false;
    bool dropped_ = false;   // the piece has gone; empty until the next
    int phase_ = 0;
    float stage_left_ = 1.0f, seconds_ = 0.0f, fall_notch_ = -1.0f;
};


// A finished combo, for the floating numbers (docs/HUD_DESIGN.md): the points
// it earned (as shown, x 10), how many blocks it cleared, and where its
// cleared cells are, as their mean offset from the cursor in cells around the
// sphere from the front (where the last piece landed, which faces the camera),
// along the board's columns (dx) and rows (dy; rows run upwards on screen).
struct ComboEvent {
    std::int64_t points = 0;
    int blocks = 0;
    float dx = 0.0f, dy = 0.0f;
};

// Watches the board from frame to frame: cells that empty are cleared blocks.
// A combo ends when nothing has cleared for `quiet_frames` updates.
class ComboTracker {
public:
    explicit ComboTracker(int quiet_frames = 45) : quiet_frames_(quiet_frames) {}
    // Returns a combo when one has just ended.
    std::optional<ComboEvent> update(const StoryHudState& state);

private:
    int quiet_frames_;
    std::vector<std::int16_t> last_;
    bool active_ = false;
    int quiet_ = 0;
    std::uint32_t score_at_start_ = 0;
    std::uint32_t last_score_ = 0;
    double front_x_ = 7.5, front_y_ = 7.5;  // the board cell facing the camera (a 2 x 2 at the start lands at 7-8)
    int blocks_ = 0;
    double sx_ = 0, sy_ = 0, cx_ = 0, cy_ = 0;  // sums of the angles' sines and cosines
};

// Board measures for the progress bar: blocks above the core in the S x S
// squares the game checks for a win (even corners, wrapping).
struct BoardView {
    // block values, 9 layers x 1024 cells (layer 0 is the core)
    std::vector<std::int16_t> cells;
    int square = 4;  // S
};
// The highest layer (1 to 8) holding a block, or 1.
int top_layer(const BoardView& board);
// Blocks above the core in the square with the fewest.
int weakest_square_blocks(const BoardView& board);

// Follows the progress through a level: the count at the weakest square when
// the level starts is 100%.
class ProgressTracker {
public:
    // Returns 0 to 1. `level_key` changes when a new level starts.
    float update(int level_key, int weakest_now);

private:
    int key_ = -1;
    int start_ = 0;
    int settling_ = 0;
};

// Episode splits, LiveSplit style (docs/HUD_DESIGN.md): game time per level
// within an episode (chapter), compared with the best time of each level.
struct SplitRow {
    int level = 0;           // 1-based
    double time = 0.0;       // seconds (running for the current level)
    bool done = false;       // completed (else the current level)
    bool has_best = false;   // a best time to compare with
    double delta = 0.0;      // time - best (negative: ahead)
};
struct SplitsView {
    int chapter = 0;
    double total = 0.0;              // the episode so far
    bool has_delta = false;          // any completed level had a best
    double net_delta = 0.0;          // sum of the completed levels' deltas
    std::vector<SplitRow> rows;      // completed levels, then the current one
};

class EpisodeSplits {
public:
    // Called each frame in play with the chapter and level shown and the
    // play time that passed (0 while paused). Returns the completed level's
    // number and time when a level has just finished (to save as a best).
    struct Finished { int chapter, level; double time; bool episode_done; double episode_total; };
    std::optional<Finished> update(int chapter, int level, double play_seconds, const std::vector<double>& best_levels);
    // Compared with the best times as they were when the run started (a level
    // finishing in this run updates the profile, not this comparison).
    SplitsView view() const;
    // Forget the run (a restart, or leaving the mode).
    void reset() { chapter_ = -1; }

private:
    int chapter_ = -1;
    int level_ = 0;
    double current_ = 0.0;
    std::vector<double> done_;  // completed levels' times, in order from level `first_`
    int first_ = 1;
    std::vector<double> bests_;  // the best level times when the run started
};

// Score Attack splits: points scored in each stage of a run (an episode of
// Story, or one stage), compared with the best points scored in that stage
// (more is better).
struct ScoreRow {
    int stage = 0;
    std::int64_t points = 0;
    bool done = false;
    bool has_best = false;
    std::int64_t delta = 0;  // points - best (positive: ahead)
};
struct ScoreSplitsView {
    std::int64_t total = 0;
    bool has_delta = false;
    std::int64_t net_delta = 0;
    std::vector<ScoreRow> rows;
};
class ScoreSplits {
public:
    struct Finished { int stage; std::int64_t points; };
    // Each frame in play: the stage shown and the score (as shown). A new run
    // starts when the score goes down or the stage goes back; its points count
    // from the score it started with.
    void reset() { stage_ = -1; }
    std::optional<Finished> update(int stage, std::int64_t score, const std::vector<std::int64_t>& best_stages);
    ScoreSplitsView view() const;
    // Bonus points for the stage just finished (added to the game's score
    // too, so they are kept out of the next stage's points).
    void credit_finished(std::int64_t points);
    // The current stage finished with no next stage to start (an episode's
    // last stage).
    std::optional<Finished> finish();

private:
    bool ended_ = false;
    int stage_ = -1;
    std::int64_t stage_start_ = 0, score_ = 0, run_start_ = 0;
    std::vector<std::int64_t> done_;
    int first_ = 1;
    std::vector<std::int64_t> bests_;
};

// Score Attack's clock (docs/HUD_DESIGN.md): two minutes per stage for the
// thinnest spheres and ten seconds more for each further layer. A cleared
// stage carries its whole minutes left to the next stage (in a whole
// episode) and turns the remaining seconds into bonus points; combos add time.
class AttackClock {
public:
    static constexpr double base_seconds = 120.0;   // a one-layer sphere (Story's thinnest)
    static constexpr double layer_seconds = 10.0;   // each layer more
    static constexpr double overtime_seconds = 120.0;  // the Overtime trophy: this long past the stage's time
    static constexpr std::int64_t points_per_second = 1000;  // as shown (the game's score x 10)
    // A stage's own time for its playfield layers (1 to 8).
    static double stage_seconds(int layers) { return base_seconds + layer_seconds * std::clamp(layers - 1, 0, 7); }
    void start(int layers) { stage_ = stage_seconds(layers), left_ = stage_, played_ = 0.0; }
    // The stage's layers, once known (they load as it starts): changes the
    // clock by the difference from the time it was given.
    void set_layers(int layers) {
        const double now = stage_seconds(layers);
        left_ = std::max(0.0, left_ + now - stage_);
        stage_ = now;
    }
    double stage() const { return stage_; }
    void tick(double seconds) {
        left_ = std::max(0.0, left_ - std::max(0.0, seconds));
        played_ += std::max(0.0, seconds);
    }
    void add(double seconds) { left_ += std::max(0.0, seconds); }
    double left() const { return left_; }
    bool out() const { return left_ <= 0.0; }
    // The stage is cleared: returns the bonus points (whole seconds), and sets the next
    // stage's clock (this stage's time plus the carried minutes, until
    // set_layers gives the next stage's own).
    std::int64_t clear(bool next_stage);
    // Play time in the current stage.
    double played() const { return played_; }
    // Seconds a finished combo adds: one a block past two, up to ten, scaled
    // down the longer the stage has been played (combo_scale).
    static double combo_seconds(int blocks, double played);
    // The share of a combo's time still given after `played` seconds of the
    // stage: all of it in the first minute, two thirds in the second, a
    // quarter in the third, none after.
    static double combo_scale(double played);
    // Adds a finished combo's time, scaled by this stage's play time.
    void add_combo(int blocks) { add(combo_seconds(blocks, played_)); }

private:
    double stage_ = base_seconds;
    double left_ = base_seconds;
    double played_ = 0.0;
};

// Reads the HUD's values for player 1. Returns false unless Story (mode 1) or
// Hide & Seek (mode 5) or Lines (mode 9) is in play.
bool read_story_hud(const std::uint8_t* rdram, ProgressTracker& progress, StoryHudState& out);

} // namespace tetrisphere
