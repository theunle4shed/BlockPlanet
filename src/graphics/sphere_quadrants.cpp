#include "tetrisphere/sphere_quadrants.h"

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/sphere_extended.h"

#include <array>

// Game functions called by the passes (generated code).
extern "C" {
void func_8009D90C(std::uint8_t* rdram, recomp_context* ctx);  // cell setup (sphere_cell.cpp)
void func_8009E314(std::uint8_t* rdram, recomp_context* ctx);  // per-block setup, a0 = face mask
void func_8009E214(std::uint8_t* rdram, recomp_context* ctx);  // per-block setup
void func_8009E3E4(std::uint8_t* rdram, recomp_context* ctx);  // block value 0x2D
void func_80097B74(std::uint8_t* rdram, recomp_context* ctx);  // block value 0x12
void func_80097848(std::uint8_t* rdram, recomp_context* ctx);  // block value 0x3F
void func_8009C330(std::uint8_t* rdram, recomp_context* ctx);  // outer face (mask 0x10)
void func_8009C6D4(std::uint8_t* rdram, recomp_context* ctx);  // inner face (never called by the game)
void func_8009CA98(std::uint8_t* rdram, recomp_context* ctx);  // side face (mask 0x04)
void func_8009CE38(std::uint8_t* rdram, recomp_context* ctx);  // side face (mask 0x08)
void func_8009D1D4(std::uint8_t* rdram, recomp_context* ctx);  // side face (mask 0x01)
void func_8009D570(std::uint8_t* rdram, recomp_context* ctx);  // side face (mask 0x02)
}

namespace tetrisphere::sphere {
namespace {

using namespace tetrisphere::guest;
using GameFunction = void (*)(std::uint8_t*, recomp_context*);

// Loop state the game keeps in memory (the face functions read it).
constexpr std::uint32_t cell_row = 0x8015F836u;
constexpr std::uint32_t cell_column = 0x8015F83Au;
constexpr std::uint32_t cell_layer = 0x8015F83Eu;
// Written by the cell setup: the column's first layer and its end.
constexpr std::uint32_t first_layer = 0x8015EDB2u;
constexpr std::uint32_t end_layer = 0x8015EDAEu;
// Pointers into the per-block planes, one 32 x 32 layer after another.
constexpr std::uint32_t block_value_pointer = 0x8015E990u;  // 2 bytes per block
constexpr std::uint32_t face_mask_pointer = 0x8015ED98u;    // 1 byte per block
constexpr std::uint32_t block_extra_pointer = 0x8015EDA8u;  // 4 bytes per block
constexpr std::uint32_t layer_cells = 1024;
// Per-block values for the drawing functions.
constexpr std::uint32_t face_mask = 0x8015E588u;
constexpr std::uint32_t block_type = 0x8015F59Cu;  // block value / 9

constexpr std::uint32_t outer_face_bit = 0x10;
constexpr std::int16_t value_special_a = 0x2D;
constexpr std::int16_t value_special_b = 0x12;
constexpr std::int16_t value_special_c = 0x3F;

// Each corner coordinate of the block's faces, and the layer step that lifts
// it by one layer (see sphere_cell.cpp), in the game's order.
struct Lift {
    std::uint32_t coordinate;
    std::uint32_t step;
};
constexpr std::array<Lift, 24> layer_lifts{{
    {0x8015ED9Cu, 0x8015F7CCu}, {0x8015EDA4u, 0x8015F7DCu}, {0x8015EDACu, 0x8015F7EAu},
    {0x8015F4D2u, 0x8015F7D0u}, {0x8015F4DCu, 0x8015F7E0u}, {0x8015F4E0u, 0x8015F7EEu},
    {0x8015EDBCu, 0x8015F7D4u}, {0x8015EE1Au, 0x8015F7E4u}, {0x8015EEC8u, 0x8015F7F2u},
    {0x8015EDB0u, 0x8015F7D8u}, {0x8015EDB4u, 0x8015F7E8u}, {0x8015EDB8u, 0x8015F7F6u},
    {0x8015F4E4u, 0x8015F7CCu}, {0x8015F4E8u, 0x8015F7DCu}, {0x8015F4EEu, 0x8015F7EAu},
    {0x8015F59Au, 0x8015F7D0u}, {0x8015F5A0u, 0x8015F7E0u}, {0x8015F5A6u, 0x8015F7EEu},
    {0x8015F536u, 0x8015F7D4u}, {0x8015F566u, 0x8015F7E4u}, {0x8015F594u, 0x8015F7F2u},
    {0x8015F4F6u, 0x8015F7D8u}, {0x8015F4FEu, 0x8015F7E8u}, {0x8015F506u, 0x8015F7F6u},
}};

struct Side {
    std::uint32_t bit;
    GameFunction draw;
};
struct QuadrantShape {
    bool rows_down;     // rows 14 down to 8, else 0 up to 7
    bool columns_down;  // columns likewise
    std::uint32_t visible_mask;  // faces this quarter can draw
    Side first_side;
    Side second_side;
};
constexpr std::array<QuadrantShape, 4> shapes{{
    {false, false, 0x1F1Au, {0x08, func_8009CE38}, {0x02, func_8009D570}},
    {true, false, 0x1F16u, {0x04, func_8009CA98}, {0x02, func_8009D570}},
    {false, true, 0x1F19u, {0x08, func_8009CE38}, {0x01, func_8009D1D4}},
    {true, true, 0x1F15u, {0x04, func_8009CA98}, {0x01, func_8009D1D4}},
}};

// Rows and columns run upwards to 7 or downwards to 8; the game's patch
// starts at 0 or 14 (15 cells across, between 16 mesh points), an extended
// patch `ring` cells further out.
constexpr std::int16_t start(bool down, int ring) { return static_cast<std::int16_t>(down ? 14 + ring : -ring); }
constexpr bool in_range(std::int16_t v, bool down) { return down ? v >= 8 : v < 8; }

void advance(std::uint8_t* rdram, std::uint32_t address, bool down) {
    store16(rdram, address, static_cast<std::int16_t>(load16(rdram, address) + (down ? -1 : 1)));
}

// Moves the plane pointers and the face corners up one layer.
void lift_one_layer(std::uint8_t* rdram) {
    store32(rdram, block_value_pointer, load32(rdram, block_value_pointer) + 2 * layer_cells);
    store32(rdram, face_mask_pointer, load32(rdram, face_mask_pointer) + layer_cells);
    store32(rdram, block_extra_pointer, load32(rdram, block_extra_pointer) + 4 * layer_cells);
    for (const Lift& lift : layer_lifts) {
        store16(rdram, lift.coordinate,
                static_cast<std::int16_t>(load16(rdram, lift.coordinate) + load16(rdram, lift.step)));
    }
}

void draw_block(std::uint8_t* rdram, recomp_context* ctx, const QuadrantShape& shape) {
    const std::uint8_t faces = load8(rdram, load32(rdram, face_mask_pointer));
    store16(rdram, face_mask, faces);
    if ((faces & shape.visible_mask) == 0) return;
    const std::int16_t value = load16(rdram, load32(rdram, block_value_pointer));
    if (value == 0) return;
    store16(rdram, block_type, static_cast<std::int16_t>(value / 9));
    if (value == value_special_a) return func_8009E3E4(rdram, ctx);
    if (value == value_special_b) return func_80097B74(rdram, ctx);
    if (value == value_special_c) return func_80097848(rdram, ctx);
    ctx->r4 = faces;
    func_8009E314(rdram, ctx);
    func_8009E214(rdram, ctx);
    // The functions may change the mask, so it is read back each time.
    auto marked = [&](std::uint32_t bit) {
        return (static_cast<std::uint16_t>(load16(rdram, face_mask)) & bit) != 0;
    };
    if (marked(outer_face_bit)) func_8009C330(rdram, ctx);
    if (marked(shape.first_side.bit)) shape.first_side.draw(rdram, ctx);
    if (marked(shape.second_side.bit)) shape.second_side.draw(rdram, ctx);
}

} // namespace

namespace {

// The quadrant loop, with the cell setup and the patch size as parameters.
template <typename PrepareCell, typename Room>
void walk_quadrant(std::uint8_t* rdram, recomp_context* ctx, Quadrant quadrant, int ring, PrepareCell&& prepare_cell,
                   Room&& room_left) {
    const QuadrantShape& shape = shapes[static_cast<std::size_t>(quadrant)];
    // Reserve the original's stack frame, so the functions called below put
    // their own frames (and the argument save area above them) where they
    // always have.
    constexpr std::int32_t original_frame_bytes = 0x40;
    ctx->r29 = static_cast<std::int32_t>(ctx->r29) - original_frame_bytes;
    // The loop counters live in guest memory and are read back after every
    // call, as the original does.
    for (store16(rdram, cell_row, start(shape.rows_down, ring)); in_range(load16(rdram, cell_row), shape.rows_down);
         advance(rdram, cell_row, shape.rows_down)) {
        for (store16(rdram, cell_column, start(shape.columns_down, ring));
             in_range(load16(rdram, cell_column), shape.columns_down);
             advance(rdram, cell_column, shape.columns_down)) {
            if (!room_left()) continue;
            prepare_cell();
            for (store16(rdram, cell_layer, load16(rdram, first_layer));
                 load16(rdram, cell_layer) < load16(rdram, end_layer); advance(rdram, cell_layer, false)) {
                lift_one_layer(rdram);
                draw_block(rdram, ctx, shape);
            }
        }
    }
    ctx->r29 = static_cast<std::int32_t>(ctx->r29) + original_frame_bytes;
}

// Guest memory read and written in place.
class LiveMemory final : public GuestMemory {
public:
    explicit LiveMemory(std::uint8_t* rdram) : rdram_(rdram) {}
    std::uint8_t read8(std::uint32_t a) const override { return load8(rdram_, a); }
    std::int16_t read16(std::uint32_t a) const override { return load16(rdram_, a); }
    std::uint32_t read32(std::uint32_t a) const override { return load32(rdram_, a); }
    void write8(std::uint32_t a, std::uint8_t v) override { store8(rdram_, a, v); }
    void write16(std::uint32_t a, std::int16_t v) override { store16(rdram_, a, v); }
    void write32(std::uint32_t a, std::uint32_t v) override { store32(rdram_, a, v); }

private:
    std::uint8_t* rdram_;
};

} // namespace

void draw_inside(std::uint8_t* rdram, recomp_context* ctx) {
    // As the passes do, but for every cell of the game's patch: the cell
    // setup, then the column's first block above the core (layer 1) instead
    // of its exposed ones, drawn by its inner face only. The core (layer 0)
    // is not drawn; a cell dug down to it is left open.
    constexpr std::int32_t original_frame_bytes = 0x40;
    ctx->r29 = static_cast<std::int32_t>(ctx->r29) - original_frame_bytes;
    for (std::int16_t row = 0; row < 15; ++row)
        for (std::int16_t column = 0; column < 15; ++column) {
            store16(rdram, cell_row, row);
            store16(rdram, cell_column, column);
            func_8009D90C(rdram, ctx);
            // The setup leaves the planes and corners one layer below the
            // column's first drawn layer (the passes lift before each block):
            // move them to layer 1.
            const int first = load16(rdram, first_layer);
            const int lift = 1 - (first - 1);
            for (int k = 0; k < lift; ++k) lift_one_layer(rdram);
            for (int k = 0; k > lift; --k) {  // down: the reverse of lift_one_layer
                store32(rdram, block_value_pointer, load32(rdram, block_value_pointer) - 2 * layer_cells);
                store32(rdram, face_mask_pointer, load32(rdram, face_mask_pointer) - layer_cells);
                store32(rdram, block_extra_pointer, load32(rdram, block_extra_pointer) - 4 * layer_cells);
                for (const Lift& l : layer_lifts)
                    store16(rdram, l.coordinate, static_cast<std::int16_t>(load16(rdram, l.coordinate) - load16(rdram, l.step)));
            }
            store16(rdram, cell_layer, 1);
            const std::int16_t value = load16(rdram, load32(rdram, block_value_pointer));
            if (value == 0 || value / 9 == 10) continue;  // dug to the core, or the core itself
            const std::uint8_t faces = load8(rdram, load32(rdram, face_mask_pointer));
            store16(rdram, face_mask, faces);
            store16(rdram, block_type, static_cast<std::int16_t>(value / 9));
            ctx->r4 = faces;
            func_8009E314(rdram, ctx);
            func_8009E214(rdram, ctx);
            func_8009C6D4(rdram, ctx);
        }
    ctx->r29 = static_cast<std::int32_t>(ctx->r29) + original_frame_bytes;
}

void draw_quadrant(std::uint8_t* rdram, recomp_context* ctx, Quadrant quadrant) {
    walk_quadrant(rdram, ctx, quadrant, 0, [&] { func_8009D90C(rdram, ctx); }, [] { return true; });
}

void draw_extended_quadrant(std::uint8_t* rdram, recomp_context* ctx, Quadrant quadrant,
                            const ExtendedSphere& sphere) {
    LiveMemory memory(rdram);
    // The extended patch shares the game's display list (112 KB per frame,
    // about 38 KB used); stop adding cells well before it could fill.
    constexpr std::uint32_t display_list_start = 0x800F22B8u;
    constexpr std::uint32_t display_list_current = 0x800F22B4u;
    constexpr std::uint32_t display_list_budget = 0x18000u;
    walk_quadrant(
        rdram, ctx, quadrant, sphere.ring(),
        [&] { prepare_extended_cell(memory, sphere, load16(rdram, cell_row), load16(rdram, cell_column)); },
        [&] { return load32(rdram, display_list_current) - load32(rdram, display_list_start) < display_list_budget; });
}

} // namespace tetrisphere::sphere
