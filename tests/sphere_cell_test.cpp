#include "tetrisphere/sphere_cell.h"

#include <cassert>
#include <cstdint>
#include <map>

using namespace tetrisphere::sphere;

namespace {

// Byte-addressed big-endian guest memory, as the N64 sees it.
class FakeMemory final : public GuestMemory {
public:
    std::uint8_t read8(std::uint32_t a) const override {
        const auto it = bytes.find(a);
        return it == bytes.end() ? 0 : it->second;
    }
    std::int16_t read16(std::uint32_t a) const override {
        return static_cast<std::int16_t>(read8(a) << 8 | read8(a + 1));
    }
    std::uint32_t read32(std::uint32_t a) const override {
        return static_cast<std::uint32_t>(read16(a) & 0xFFFF) << 16 | static_cast<std::uint16_t>(read16(a + 2));
    }
    void write8(std::uint32_t a, std::uint8_t v) override { bytes[a] = v; }
    void write16(std::uint32_t a, std::int16_t v) override {
        write8(a, static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) >> 8));
        write8(a + 1, static_cast<std::uint8_t>(v));
    }
    void write32(std::uint32_t a, std::uint32_t v) override {
        write16(a, static_cast<std::int16_t>(v >> 16));
        write16(a + 2, static_cast<std::int16_t>(v));
    }
    std::map<std::uint32_t, std::uint8_t> bytes;
};

constexpr std::uint32_t mesh = 0x80200000u;
constexpr std::uint32_t context = 0x80210000u;
constexpr std::uint32_t board = 0x80220000u;

void set_entry(FakeMemory& m, int row, int column, int base, int step) {
    const std::uint32_t e = mesh + static_cast<std::uint32_t>(row * 0xC0 + column * 0xC);
    for (int axis = 0; axis < 3; ++axis) {
        m.write16(e + 2u * axis, static_cast<std::int16_t>(base + axis));
        m.write16(e + 6u + 2u * axis, static_cast<std::int16_t>(step + axis));
    }
}

FakeMemory setup(std::uint8_t board_byte) {
    FakeMemory m;
    m.write16(0x8015F836u, 3);  // row
    m.write16(0x8015F83Au, 5);  // column
    m.write32(0x800E44E4u, mesh);
    set_entry(m, 3, 5, 1000, 10);
    set_entry(m, 4, 5, 2000, 20);
    set_entry(m, 4, 6, 3000, 30);
    set_entry(m, 3, 6, 4000, 40);
    m.write32(0x8013DD00u, context);
    m.write32(context + 0x2B20u, 30);  // board x = (3 + 30) & 31 = 1
    m.write32(context + 0x2B24u, 0);   // board y = 5
    m.write32(0x800E44DCu, board);
    m.write8(board + 5 * 32 + 1, board_byte);
    m.write32(0x800E44D0u, 0x80300000u);
    m.write32(0x800E44D4u, 0x80310000u);
    m.write32(0x800E44D8u, 0x80320000u);
    m.write16(0x8015EDC0u + 3 * 6 + 6, 77);  // row shade, second vector x
    m.write16(0x8015EED0u + (3 * 16 + 5) * 6 + 0x60, 55);  // corner (row + 1, column) x
    return m;
}

} // namespace

int main() {
    // Height 3, kind 2: faces are raised to the third layer.
    FakeMemory m = setup(0x32);
    prepare_cell(m);
    const int top_block = 2 * 1024 + 5 * 32 + 1;  // third layer
    assert(m.read16(0x8015F842u) == top_block);
    assert(m.read16(0x8015EDB2u) == 3);
    assert(m.read16(0x8015EDAEu) == 3);  // kind + 1
    assert(m.read8(0x8015D770u) == 0);
    // Corner (row, column), y axis: top = base + (height - 1) * step.
    assert(m.read16(0x8015ED9Cu) == 1001 + 2 * 11);
    assert(m.read16(0x8015F4E4u) == 1001 + 1 * 11);  // bottom, one layer lower
    assert(m.read16(0x8015F7CCu) == 11);              // step copy
    // Corner (row + 1, column + 1), x axis.
    assert(m.read16(0x8015EE1Au) == 3000 + 2 * 30);
    assert(m.read16(0x8015F566u) == 3000 + 30);
    // Shading copies and negation.
    assert(m.read16(0x8015F4E6u) == -77);
    assert(m.read16(0x8015F7F4u) == 55);
    // Plane pointers.
    assert(m.read32(0x8015E990u) == 0x80300000u + 2u * top_block);
    assert(m.read32(0x8015ED98u) == 0x80310000u + top_block);
    assert(m.read32(0x8015EDA8u) == 0x80320000u + 4u * top_block);

    // Height 0 counts as 1; kind 7 becomes 8, which is capped to 7 and flagged.
    FakeMemory n = setup(0x07);
    prepare_cell(n);
    assert(n.read16(0x8015EDB2u) == 1);
    assert(n.read16(0x8015EDAEu) == 7);
    assert(n.read8(0x8015D770u) == 1);
    assert(n.read16(0x8015F842u) == 5 * 32 + 1);
    assert(n.read16(0x8015ED9Cu) == 1001);
    assert(n.read16(0x8015F4E4u) == 1001 - 11);
    return 0;
}
