#include "tetrisphere/story_hud.h"

#include "tetrisphere/guest_memory.h"

#include <cassert>
#include <cmath>
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

    // The highest layer holding a block.
    board.cells.assign(9 * 1024, 0);
    assert(top_layer(board) == 1);
    board.cells[1024 * 5 + 7] = 288;
    assert(top_layer(board) == 5);

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

    // Combos: cells that empty are counted until nothing clears for a while;
    // their place is the mean offset from the cursor (wrapping round the board).
    {
        ComboTracker tracker(3);
        StoryHudState frame;
        frame.cells.assign(9 * 1024, 288);
        for (int c = 0; c < 1024; ++c) frame.cells[std::size_t(2048 + c)] = 0;
        frame.score = 100;
        assert(!tracker.update(frame));
        // A square lands at (29-30, 9-10): the front is (29.5, 9.5).
        for (int y : {9, 10})
            for (int x : {29, 30}) frame.cells[std::size_t(2048 + y * 32 + x)] = 297;
        assert(!tracker.update(frame));
        for (int x : {31, 0, 1}) frame.cells[std::size_t(1024 + 12 * 32 + x)] = 0;  // 3 cells, 2.5 right and 2.5 along
        frame.score = 225;
        assert(!tracker.update(frame));
        assert(!tracker.update(frame) && !tracker.update(frame));
        const auto event = tracker.update(frame);
        assert(event && event->blocks == 3 && event->points == 1250);
        assert(std::abs(event->dx - 2.5f) < 0.05f && std::abs(event->dy - 2.5f) < 0.05f);
    }

    // Episode splits: levels' game times, against the best of each level.
    {
        EpisodeSplits splits;
        const std::vector<double> none;
        assert(!splits.update(1, 1, 0.0, none));
        assert(!splits.update(1, 1, 30.0, none));
        auto f = splits.update(1, 2, 0.5, none);  // level 1 done in 30 s
        assert(f && f->level == 1 && f->time == 30.0 && !f->episode_done);
        assert(!splits.update(1, 2, 20.0, none));
        auto v = splits.view();
        assert(v.rows.size() == 2 && v.rows[0].done && v.rows[0].time == 30.0 && !v.rows[0].has_best);
        assert(!v.rows[1].done && v.rows[1].time == 20.5 && v.total == 50.5 && !v.has_delta);
        // Try Again: back to level 1 starts a new run, compared with the bests then.
        const std::vector<double> best{25.0, 30.0};
        assert(!splits.update(1, 1, 3.0, best));
        v = splits.view();
        assert(v.rows.size() == 1 && v.total == 3.0 && v.rows[0].has_best);
        splits.update(1, 1, 27.0, std::vector<double>{1.0, 1.0});  // later bests do not change this run's comparison
        splits.update(1, 2, 20.5, std::vector<double>{1.0, 1.0});
        v = splits.view();
        assert(v.has_delta && v.net_delta == 5.0 && v.rows[0].delta == 5.0 && v.rows[1].has_best && v.rows[1].delta == -9.5);
        // The episode's last level finishing: the next chapter starts at level 1.
        for (int l = 2; l <= 10; ++l) splits.update(1, l, 10.0, none);
        f = splits.update(2, 1, 0.0, none);
        assert(f && f->chapter == 1 && f->level == 10 && f->episode_done && f->episode_total > 90.0);
    }

    // Score Attack splits: points per stage against the best per stage.
    {
        ScoreSplits sc;
        const std::vector<std::int64_t> best{1000, 5000};
        assert(!sc.update(1, 0, best));
        assert(!sc.update(1, 1500, best));
        auto f = sc.update(2, 1500, best);
        assert(f && f->stage == 1 && f->points == 1500);
        sc.update(2, 4000, best);
        auto v = sc.view();
        assert(v.total == 4000 && v.rows.size() == 2 && v.rows[0].delta == 500 && v.has_delta && v.net_delta == 500);
        assert(!v.rows[1].done && v.rows[1].points == 2500);
        sc.update(1, 0, best);  // a new run
        assert(sc.view().rows.size() == 1 && sc.view().total == 0);
        // A run that starts with points already scored (Story's score runs on).
        tetrisphere::ScoreSplits mid;
        mid.update(3, 9000, best);
        mid.update(3, 9700, best);
        assert(mid.view().total == 700 && mid.view().rows.back().points == 700);
        // Bonus points go to the finished stage; finishing the last stage ends the run.
        tetrisphere::ScoreSplits run;
        run.update(1, 0, best);
        run.update(1, 1000, best);
        run.update(2, 1000, best);
        run.credit_finished(500);
        run.update(2, 1500, best);  // the game's score with the bonus
        assert(run.view().rows[0].points == 1500 && run.view().rows[1].points == 0);
        run.update(2, 1800, best);
        const auto last = run.finish();
        assert(last && last->stage == 2 && last->points == 300 && !run.finish());
        assert(!run.update(2, 1800, best) && run.view().rows.size() == 2 && run.view().total == 1800);
    }
    {
        // Score Attack's clock: 2:00 for one layer, 10 s a layer more; whole
        // minutes carry, the rest is bonus.
        using tetrisphere::AttackClock;
        assert(AttackClock::stage_seconds(1) == 120.0 && AttackClock::stage_seconds(6) == 170.0);
        AttackClock clock;
        clock.start(1);
        clock.set_layers(3);  // the layers load: 2:20
        assert(clock.left() == 140.0);
        clock.tick(47.0);  // 1:33 left
        assert(clock.clear(true) == 33000 && clock.left() == 200.0);  // 2:20 + the minute carried
        clock.set_layers(4);  // the next stage has 4 layers: 2:30 + 1:00
        assert(clock.left() == 210.0 && clock.played() == 0.0);
        clock.tick(10.0);
        clock.add_combo(5);  // 10 s into the stage: all 3 seconds
        assert(clock.left() == 203.0 && AttackClock::combo_seconds(40, 0.0) == 10.0);
        // Diminishing: full in the first minute, 2/3 in the second, 1/4 in the third, then none.
        assert(AttackClock::combo_seconds(8, 59.0) == 6.0);
        assert(AttackClock::combo_seconds(8, 60.0) == 4.0);
        assert(AttackClock::combo_seconds(8, 150.0) == 1.5);
        assert(AttackClock::combo_seconds(8, 180.0) == 0.0);
        assert(clock.clear(false) == 203000 && clock.left() == 150.0);  // no next stage: all of it
        clock.tick(500.0);
        assert(clock.out() && clock.left() == 0.0);
    }

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
    assert(state.next[0].shape == 2 && state.next[0].material == 33 && state.next[0].cells == 4 && !state.next[0].random_block);
    assert(state.next[1].random_block);
    assert(state.next[2].shape == 1 && state.next[2].material == 32);
    assert(state.chapter == 1 && state.level == 3);
    store8(m, p1 + 0x702, 3);
    assert(read_story_hud(m, progress, state) && state.magic == 3);
    return 0;
}
