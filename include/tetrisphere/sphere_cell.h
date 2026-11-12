#pragma once

// BlockPlanet's reimplementation of the sphere cell setup pass (originally
// func_8009D90C), stage 1 of the sphere renderer. Before the game draws one
// grid cell (one column of blocks) it calls this to work out the corners of
// the block at the top of that column and to point the drawing code at the
// cell's board entries. See docs/research/blockplanet-game-state.md.
//
// How the block shape follows from the mesh table: each table entry holds a
// base point (`outer`, direction / 3) and a layer step (`inner`,
// direction / 10). Layer n of a column sits between base + (n - 2) * step and
// base + (n - 1) * step, so every layer is the same thickness along its ray
// and neighbouring columns meet exactly because they share the corner rays.

#include <atomic>
#include <cstdint>

namespace tetrisphere::sphere {

// Guest memory as the pass sees it. Addresses are N64 virtual addresses.
class GuestMemory {
public:
    virtual ~GuestMemory() = default;
    virtual std::uint8_t read8(std::uint32_t address) const = 0;
    virtual std::int16_t read16(std::uint32_t address) const = 0;
    virtual std::uint32_t read32(std::uint32_t address) const = 0;
    virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void write16(std::uint32_t address, std::int16_t value) = 0;
    virtual void write32(std::uint32_t address, std::uint32_t value) = 0;
};

// Does exactly what func_8009D90C does to guest memory, in the same order.
void prepare_cell(GuestMemory& memory);

// After a cell setup: the board cell it set up (0 to 1023) and the corners
// of the top of its column (in the order (0, 0), (1, 0), (1, 1), (0, 1) of
// row and column) with each corner's layer step.
struct CellCorners {
    int cell = 0;
    std::int16_t top[4][3]{};
    std::int16_t step[4][3]{};
};
CellCorners read_cell_corners(const GuestMemory& memory);
// Training's gap filling in the cell setup (see sphere_cell.cpp): on unless
// a mode turns it off (Minefield on the sphere, whose single uncovered cells
// between cubes are real holes).
inline std::atomic<bool>& fill_training_gaps() {
    static std::atomic<bool> on{true};
    return on;
}
// Told about every cell the quadrant passes set up (src/graphics/
// sphere_quadrants.cpp), on the game thread; null for none.
using CellListener = void (*)(const CellCorners&);
inline std::atomic<CellListener>& cell_listener() {
    static std::atomic<CellListener> listener{nullptr};
    return listener;
}

} // namespace tetrisphere::sphere
