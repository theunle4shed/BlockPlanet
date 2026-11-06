#pragma once

// What BlockPlanet's Story HUD shows, read from game memory
// (docs/HUD_DESIGN.md; addresses in docs/research/blockplanet-game-state.md).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tetrisphere {

struct HudPiece {
    int shape = 0;     // the shape record's ID (+8), the same in every level
    int material = 0;  // the block material (+2), which sets the colour
    int cells = 0;
    bool mystery = false;  // the "?" piece
};

struct StoryHudState {
    int hearts = 0;            // 0 to 3
    float drop_left = 1.0f;    // the drop timer's time left, 1 (full) to 0
    int magic = -1;            // -1 none, 0 to 5 the held magic
    float meter = 0.0f;        // the magic meter, 0 to 1
    std::vector<HudPiece> next;  // the next pieces, next first
    int chapter = 0;           // 1-based
    int level = 0;             // 1-based
    float progress = -1.0f;    // 0 to 1, or -1 before it is known
    int top_layer = 1;         // the highest board layer holding a block (1 to 8)
    float camera_distance = 2424.0f;  // player context +0x2420
    std::vector<std::int16_t> cells;  // the board (9 layers x 1024), for ComboTracker
    std::uint32_t score = 0;          // player context +0x2AF8 (shown x 10)
    int combo = 0;                    // the running combo count (+0x2AE0)
    int cursor_x = 0, cursor_y = 0;   // the drop cursor (+0x2B2A, +0x2B2E)
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

// Reads the HUD's values for player 1. Returns false when Story is not in play.
bool read_story_hud(const std::uint8_t* rdram, ProgressTracker& progress, StoryHudState& out);

} // namespace tetrisphere
