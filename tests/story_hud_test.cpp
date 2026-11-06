#include "tetrisphere/story_hud.h"

#include "tetrisphere/guest_memory.h"

#include <cassert>
#include <vector>

using namespace tetrisphere;

int main() {
    // An empty board above a full core: every square has 0 blocks.
    BoardView board;
    board.square = 4;
    board.cells.assign(9 * 1024, 0);
    assert(weakest_square_blocks(board) == 0);

    // Layer 1 full: every 4x4 square has 16 blocks; clear one square at an
    // even corner and one at an odd corner (which the game never checks).
    for (int c = 0; c < 1024; ++c) board.cells[1024 + c] = 288;
    assert(weakest_square_blocks(board) == 16);
    for (int y = 1; y < 5; ++y)
        for (int x = 1; x < 5; ++x) board.cells[1024 + y * 32 + x] = 0;  // odd corner (1,1)
    assert(weakest_square_blocks(board) == 7);  // best even square (2,2) overlaps 9 cleared cells
    for (int y = 10; y < 14; ++y)
        for (int x = 10; x < 14; ++x) board.cells[1024 + y * 32 + x] = 0;  // even corner (10,10)
    assert(weakest_square_blocks(board) == 0);
    // Squares wrap round the board's edges.
    board.cells.assign(9 * 1024, 0);
    for (int c = 0; c < 1024; ++c) board.cells[1024 + c] = 288;
    for (int y : {30, 31, 0, 1})
        for (int x : {30, 31, 0, 1}) board.cells[1024 + y * 32 + x] = 0;
    assert(weakest_square_blocks(board) == 0);
    // Higher layers count too.
    board.cells[2048 + 31 * 32 + 31] = 297;
    assert(weakest_square_blocks(board) == 0 || weakest_square_blocks(board) == 1);

    // Progress: the start count is 100%; the largest count in the first
    // updates of a level is kept as the start.
    ProgressTracker tracker;
    assert(tracker.update(101, 0) == 1.0f);
    assert(tracker.update(101, 40) == 0.0f);
    assert(tracker.update(101, 20) == 0.5f);
    for (int i = 0; i < 200; ++i) tracker.update(101, 20);
    assert(tracker.update(101, 50) == 0.0f);   // more blocks than at the start: 0, not negative
    assert(tracker.update(101, 10) == 0.75f);
    assert(tracker.update(102, 30) == 0.0f);   // a new level starts again

    // Reading from game memory.
    std::vector<std::uint8_t> memory(0x800000);
    auto* m = memory.data();
    using namespace guest;
    ProgressTracker progress;
    StoryHudState state;
    store16(m, 0x800E121C, 0);  // a menu
    assert(!read_story_hud(m, progress, state));
    store16(m, 0x800E121C, -1);
    store16(m, 0x800E44A8, 1);  // Rescue
    const std::uint32_t p1 = 0x80110220u;
    store16(m, p1 + 0x2D7E, 2);
    store16(m, p1 + 0x2B10, 150);
    store8(m, p1 + 0x702, 0xFF);
    store16(m, p1 + 0x2AEC, 7);
    store16(m, p1 + 0x2AEE, 14);
    store16(m, p1 + 0x2374, 0);
    store16(m, p1 + 0x2376, 22);
    store16(m, p1 + 0x2378, 1);
    store32(m, 0x8014CBC8u, 0x80127CF0u);
    store16(m, 0x80127CF0u + 2, 33);
    store16(m, 0x80127CF0u + 4, 4);
    store8(m, 0x80127CF0u + 8, 2);
    store32(m, 0x8014CBC8u + 4, 0x80128458u);
    store16(m, 0x80128458u + 2, 32);
    store16(m, 0x80128458u + 4, 3);
    store8(m, 0x80128458u + 8, 1);
    store16(m, 0x800E44A0, 0);
    store16(m, p1 + 0x23BA, 2);
    store32(m, 0x800E44D0, 0x80116BF0u);
    store16(m, p1 + 0x23E4, 4);
    assert(read_story_hud(m, progress, state));
    assert(state.hearts == 2 && state.drop_left == 0.75f && state.magic == -1 && state.meter == 0.5f);
    assert(state.next.size() == 3);
    assert(state.next[0].shape == 2 && state.next[0].material == 33 && state.next[0].cells == 4 && !state.next[0].mystery);
    assert(state.next[1].mystery);
    assert(state.next[2].shape == 1 && state.next[2].material == 32);
    assert(state.chapter == 1 && state.level == 3);
    store8(m, p1 + 0x702, 3);
    assert(read_story_hud(m, progress, state) && state.magic == 3);
    return 0;
}
