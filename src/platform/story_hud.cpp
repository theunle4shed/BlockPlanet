#include "tetrisphere/story_hud.h"

#include "tetrisphere/guest_memory.h"

#include <algorithm>
#include <climits>
#include <cstring>

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
    if (load16(rdram, 0x800E121C) != -1 || load16(rdram, 0x800E44A8) != 1) return false;
    out = {};
    out.hearts = std::clamp<int>(load16(rdram, player1 + 0x2D7E), 0, 3);
    const int drop = load16(rdram, player1 + 0x2B10);
    out.drop_left = std::clamp(1.0f - float(drop) / 600.0f, 0.0f, 1.0f);
    out.magic = static_cast<std::int8_t>(load8(rdram, player1 + 0x702));
    if (out.magic > 5) out.magic = 5;
    const int meter_full = load16(rdram, player1 + 0x2AEE);
    const int meter = static_cast<std::uint16_t>(load16(rdram, player1 + 0x2AEC));
    out.meter = meter_full > 0 ? std::clamp(float(meter) / float(meter_full), 0.0f, 1.0f) : 0.0f;
    for (int slot = 0; slot < 3; ++slot) {
        const int index = load16(rdram, player1 + 0x2374 + 2u * slot);
        if (index < 0 || index >= 48) continue;
        HudPiece piece;
        piece.mystery = index == 22;
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
    out.chapter = load16(rdram, 0x800E44A0) + 1;
    out.level = load16(rdram, player1 + 0x23BA) + 1;

    const std::uint32_t board_address = load32(rdram, 0x800E44D0);
    if (board_address >= 0x80000000u && board_address < 0x80800000u - layers * layer_cells * 2) {
        BoardView board;
        board.square = load16(rdram, player1 + 0x23E4);
        board.cells.resize(std::size_t(layers * layer_cells));
        for (int i = 0; i < layers * layer_cells; ++i) board.cells[std::size_t(i)] = load16(rdram, board_address + 2u * i);
        out.progress = progress.update(out.chapter * 100 + out.level, weakest_square_blocks(board));
        out.top_layer = top_layer(board);
    }
    return true;
}

} // namespace tetrisphere
