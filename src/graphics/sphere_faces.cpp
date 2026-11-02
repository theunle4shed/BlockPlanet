#include "tetrisphere/sphere_faces.h"

#include <array>

namespace tetrisphere::sphere {
namespace {

// Display list and vertex buffer.
constexpr std::uint32_t display_list_pointer = 0x800F22B4u;  // next free command
constexpr std::uint32_t vertex_pointer = 0x8015F848u;        // next free vertex (16 bytes each)
constexpr std::uint32_t batch_count = 0x8015F84Eu;           // u16, vertices since the last load
constexpr std::uint32_t vertex_statistic = 0x800E44F0u;      // running vertex count
constexpr std::uint16_t batch_size = 16;

// F3D commands (the game uses the original Fast3D triangle format).
constexpr std::uint32_t load_16_vertices = 0x04F00100u;  // G_VTX: 16 vertices into slot 0, 0x100 bytes
constexpr std::uint32_t triangle = 0xBF000000u;          // G_TRI1
constexpr std::int32_t slot_scale = 10;                  // F3D vertex indices are slot x 10

// Per-block values written by the cell setup and the quadrant passes.
constexpr std::uint32_t block_extra_pointer = 0x8015EDA8u;  // -> pointer to the block's texture coordinates
constexpr std::uint32_t block_alpha = 0x8015EDB6u;

// Face corners in vertex (x, y, z) order. Corner 0 is the cell's (row,
// column) mesh point, 1 is (row + 1, column), 2 is (row + 1, column + 1),
// 3 is (row, column + 1). "Top" is the block's outer face, "bottom" its inner.
using Vec = std::array<std::uint32_t, 3>;
constexpr std::array<Vec, 4> top{{
    {0x8015ED9Cu, 0x8015EDA4u, 0x8015EDACu},
    {0x8015EDB0u, 0x8015EDB4u, 0x8015EDB8u},
    {0x8015EDBCu, 0x8015EE1Au, 0x8015EEC8u},
    {0x8015F4D2u, 0x8015F4DCu, 0x8015F4E0u},
}};
constexpr std::array<Vec, 4> bottom{{
    {0x8015F4E4u, 0x8015F4E8u, 0x8015F4EEu},
    {0x8015F4F6u, 0x8015F4FEu, 0x8015F506u},
    {0x8015F536u, 0x8015F566u, 0x8015F594u},
    {0x8015F59Au, 0x8015F5A0u, 0x8015F5A6u},
}};
// Shading (vertex colour channels) per corner, and per side.
constexpr std::array<Vec, 4> corner_shade{{
    {0x8015F7ECu, 0x8015F7FCu, 0x8015F80Cu},
    {0x8015F7F4u, 0x8015F804u, 0x8015F814u},
    {0x8015F7F8u, 0x8015F808u, 0x8015F818u},
    {0x8015F7F0u, 0x8015F800u, 0x8015F810u},
}};
constexpr Vec row_start_shade{0x8015F4D4u, 0x8015F4DEu, 0x8015F4E2u};
constexpr Vec row_end_shade{0x8015F4E6u, 0x8015F4EAu, 0x8015F4F2u};
constexpr Vec column_start_shade{0x8015F4F8u, 0x8015F500u, 0x8015F508u};
constexpr Vec column_end_shade{0x8015F538u, 0x8015F568u, 0x8015F596u};

// A triangle over the face's four vertices (numbered 0..3). `tag` is added to
// the batch slot to form the command's top byte; the game sets it to 0 for
// the first triangle and to the second triangle's first vertex.
struct Triangle {
    int tag;
    std::array<int, 3> vertices;
};

struct FaceVertex {
    int corner;
    bool on_bottom;
    std::uint32_t texture_offset;  // into the block's 0x50-byte texture coordinate table
};
enum class Shading { PerCorner, PerCornerNegated, Side };
struct FaceShape {
    std::array<FaceVertex, 4> vertices;
    Shading shading;
    Vec side_shade;  // when shading is Side
    std::array<Triangle, 2> triangles;
};

constexpr std::array<FaceShape, 6> faces{{
    // Outer
    {{{{0, false, 0x4C}, {1, false, 0x48}, {3, false, 0x44}, {2, false, 0x40}}},
     Shading::PerCorner, {}, {{{0, {0, 3, 1}}, {3, {3, 0, 2}}}}},
    // Inner: the same quad on the bottom, wound the other way, shading negated
    {{{{0, true, 0x4C}, {1, true, 0x48}, {3, true, 0x44}, {2, true, 0x40}}},
     Shading::PerCornerNegated, {}, {{{0, {0, 1, 3}}, {3, {3, 2, 0}}}}},
    // RowStart
    {{{{0, false, 0x30}, {0, true, 0x34}, {3, true, 0x3C}, {3, false, 0x38}}},
     Shading::Side, row_start_shade, {{{0, {1, 3, 0}}, {3, {3, 1, 2}}}}},
    // RowEnd
    {{{{1, false, 0x20}, {1, true, 0x24}, {2, true, 0x2C}, {2, false, 0x28}}},
     Shading::Side, row_end_shade, {{{0, {0, 2, 1}}, {2, {2, 0, 3}}}}},
    // ColumnStart
    {{{{0, false, 0x00}, {0, true, 0x04}, {1, true, 0x0C}, {1, false, 0x08}}},
     Shading::Side, column_start_shade, {{{0, {0, 2, 1}}, {2, {2, 0, 3}}}}},
    // ColumnEnd
    {{{{3, false, 0x10}, {3, true, 0x14}, {2, true, 0x1C}, {2, false, 0x18}}},
     Shading::Side, column_end_shade, {{{0, {0, 1, 2}}, {2, {2, 3, 0}}}}},
}};

void append_command(GuestMemory& m, std::uint32_t w0, std::uint32_t w1) {
    const std::uint32_t at = m.read32(display_list_pointer);
    m.write32(display_list_pointer, at + 8);
    m.write32(at, w0);
    m.write32(at + 4, w1);
}

// G_TRI1 second word: a tag byte, then the three vertex slots x 10.
std::uint32_t triangle_word(std::uint32_t batch_slot, const Triangle& t) {
    auto index = [&](int v) {
        return static_cast<std::uint32_t>(slot_scale) * (batch_slot + static_cast<std::uint32_t>(v)) & 0xFFu;
    };
    return (batch_slot + static_cast<std::uint32_t>(t.tag)) << 24 | index(t.vertices[0]) << 16 |
           index(t.vertices[1]) << 8 | index(t.vertices[2]);
}

} // namespace

void draw_face(GuestMemory& m, Face face) {
    const FaceShape& shape = faces[static_cast<std::size_t>(face)];

    // A full batch: load the next 16 vertices (written after this command is
    // built, before the display list runs) and start counting again.
    if (static_cast<std::uint16_t>(m.read16(batch_count)) == batch_size) {
        append_command(m, load_16_vertices, m.read32(vertex_pointer));
        m.write16(batch_count, 0);
    }
    for (const auto& t : shape.triangles) {
        const auto batch_slot = static_cast<std::uint16_t>(m.read16(batch_count));
        append_command(m, triangle, triangle_word(batch_slot, t));
    }

    const std::uint32_t textures = m.read32(m.read32(block_extra_pointer));
    const std::uint32_t vertex = m.read32(vertex_pointer);
    for (std::size_t k = 0; k < shape.vertices.size(); ++k) {
        const FaceVertex& fv = shape.vertices[k];
        const std::uint32_t v = vertex + static_cast<std::uint32_t>(k) * 16u;
        const Vec& position = fv.on_bottom ? bottom[fv.corner] : top[fv.corner];
        for (int axis = 0; axis < 3; ++axis) m.write16(v + 2u * axis, m.read16(position[axis]));
        // Texture coordinates are stored in whole texels; vertices use 10.5 fixed point.
        m.write16(v + 8, static_cast<std::int16_t>(m.read16(textures + fv.texture_offset) << 6));
        m.write16(v + 10, static_cast<std::int16_t>(m.read16(textures + fv.texture_offset + 2) << 6));
        const Vec& shade = shape.shading == Shading::Side ? shape.side_shade : corner_shade[fv.corner];
        for (int axis = 0; axis < 3; ++axis) {
            const std::int16_t value = m.read16(shade[axis]);
            const auto channel = static_cast<std::uint8_t>(shape.shading == Shading::PerCornerNegated ? -value : value);
            m.write8(v + 12u + static_cast<std::uint32_t>(axis), channel);
        }
        m.write8(v + 15, static_cast<std::uint8_t>(m.read16(block_alpha)));
    }
    m.write32(vertex_pointer, vertex + 4u * 16u);
    m.write16(batch_count, static_cast<std::int16_t>(static_cast<std::uint16_t>(m.read16(batch_count)) + 4));
    m.write32(vertex_statistic, m.read32(vertex_statistic) + 4);
}

} // namespace tetrisphere::sphere
