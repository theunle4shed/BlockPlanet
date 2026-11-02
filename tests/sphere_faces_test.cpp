#include "tetrisphere/sphere_faces.h"

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

constexpr std::uint32_t display_list = 0x80264000u;
constexpr std::uint32_t vertices = 0x80246000u;
constexpr std::uint32_t textures = 0x80230000u;

FakeMemory setup(std::int16_t batch) {
    FakeMemory m;
    m.write32(0x800F22B4u, display_list);
    m.write32(0x8015F848u, vertices);
    m.write16(0x8015F84Eu, batch);
    m.write32(0x8015EDA8u, 0x80220000u);
    m.write32(0x80220000u, textures);
    for (std::uint32_t i = 0; i < 0x50; i += 2) m.write16(textures + i, static_cast<std::int16_t>(i));
    m.write16(0x8015EDB6u, 0xFF);
    m.write16(0x8015ED9Cu, 100);  // top corner 0, x
    m.write16(0x8015EDB0u, 200);  // top corner 1, x
    m.write16(0x8015F4D2u, 300);  // top corner 3, x
    m.write16(0x8015F7ECu, 40);   // corner 0 shade, first channel
    m.write16(0x8015F538u, -2);   // column end side shade, first channel
    return m;
}

} // namespace

int main() {
    // Outer face, batch has room: two triangles, four vertices.
    FakeMemory m = setup(4);
    draw_face(m, Face::Outer);
    assert(m.read32(0x800F22B4u) == display_list + 16);
    assert(m.read32(display_list) == 0xBF000000u);
    // Slots 4, 7, 5 (x 10) with the first slot in the top byte.
    assert(m.read32(display_list + 4) == (4u << 24 | 40u << 16 | 70u << 8 | 50u));
    assert(m.read32(display_list + 12) == (7u << 24 | 70u << 16 | 40u << 8 | 60u));
    assert(m.read16(vertices + 0) == 100);
    assert(m.read16(vertices + 16) == 200);
    assert(m.read16(vertices + 32) == 300);  // third vertex is corner 3
    assert(m.read16(vertices + 8) == 0x4C << 6);
    assert(m.read8(vertices + 12) == 40);
    assert(m.read8(vertices + 15) == 0xFF);
    assert(m.read32(0x8015F848u) == vertices + 64);
    assert(m.read16(0x8015F84Eu) == 8);
    assert(m.read32(0x800E44F0u) == 4);

    // Full batch: a vertex load comes first and the batch restarts at slot 0.
    FakeMemory n = setup(16);
    draw_face(n, Face::ColumnEnd);
    assert(n.read32(display_list) == 0x04F00100u);
    assert(n.read32(display_list + 4) == vertices);
    assert(n.read32(display_list + 12) == (0u << 24 | 0u << 16 | 10u << 8 | 20u));
    assert(n.read32(display_list + 20) == (2u << 24 | 20u << 16 | 30u << 8 | 0u));
    assert(n.read16(0x8015F84Eu) == 4);
    assert(n.read16(vertices) == 300);           // corner 3 top first
    assert(n.read8(vertices + 12) == 0xFE);      // side shade, low byte
    assert(n.read16(vertices + 8) == 0x10 << 6);

    // Row start face: the first triangle's tag is the batch slot itself.
    FakeMemory r = setup(4);
    draw_face(r, Face::RowStart);
    assert(r.read32(display_list + 4) == (4u << 24 | 50u << 16 | 70u << 8 | 40u));
    assert(r.read32(display_list + 12) == (7u << 24 | 70u << 16 | 50u << 8 | 60u));

    // Inner face: shading negated.
    FakeMemory k = setup(0);
    draw_face(k, Face::Inner);
    assert(k.read8(vertices + 12) == static_cast<std::uint8_t>(-40));
    return 0;
}
