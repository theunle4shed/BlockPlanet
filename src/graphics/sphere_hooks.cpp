// Hooks that connect the generated game code to BlockPlanet's sphere mesh
// builder (stage 1 of the sphere renderer).
//
// func_800ADD6C start:    blockplanet_sphere_mesh_begin — returns true to skip
//                         the original (replace mode).
// func_800ADD6C epilogue: blockplanet_sphere_mesh_end — compares the original's
//                         table with BlockPlanet's (verify mode).

#include "tetrisphere/recomp_support.h"
#include "tetrisphere/sphere_mesh.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace tetrisphere::sphere;

constexpr std::uint32_t mesh_pointer = 0x800E44E4u;
constexpr std::uint32_t base_grid = 0x800E3CD8u;
constexpr std::uint32_t base_row_bytes = 0x6C;
constexpr std::uint32_t active_context = 0x8013DD00u;
constexpr std::uint32_t player_count = 0x8015D980u;

std::int16_t read16(const std::uint8_t* rdram, std::uint32_t address) {
    std::int16_t v;
    std::memcpy(&v, rdram + ((address & 0x00FFFFFFu) ^ 2u), 2);
    return v;
}
void write16(std::uint8_t* rdram, std::uint32_t address, std::int16_t v) {
    std::memcpy(rdram + ((address & 0x00FFFFFFu) ^ 2u), &v, 2);
}
std::uint32_t read32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t v;
    std::memcpy(&v, rdram + (address & 0x00FFFFFFu), 4);
    return v;
}

MeshInputs gather(std::uint8_t* rdram, recomp_context* ctx, bool apply_caps) {
    MeshInputs in;
    in.row_fraction = static_cast<std::int16_t>(ctx->r4);
    in.column_fraction = static_cast<std::int16_t>(ctx->r5);
    in.base = [rdram](int row, int column) {
        const std::uint32_t a = base_grid + static_cast<std::uint32_t>(row) * base_row_bytes +
                                static_cast<std::uint32_t>(column) * 6u;
        return std::array<std::int16_t, 3>{read16(rdram, a), read16(rdram, a + 2), read16(rdram, a + 4)};
    };
    const std::uint32_t player = read32(rdram, active_context);
    for (int axis = 0; axis < 3; ++axis) {
        const std::uint32_t a = player + 0x247Au + 2u * static_cast<std::uint32_t>(axis);
        std::int16_t value = read16(rdram, a);
        if (value > 0x4000) {
            value = 0x4000;
            if (apply_caps) write16(rdram, a, value);  // the original caps these in place
        }
        in.scale[axis] = value;
    }
    in.scale_enabled = read16(rdram, player + 0x2478u) != 0;
    in.two_player = read16(rdram, player_count) >= 2;
    in.player_one = read16(rdram, player + 0x23B2u) == 1;
    return in;
}

void store(std::uint8_t* rdram, const std::vector<MeshEntry>& mesh) {
    const std::uint32_t table = read32(rdram, mesh_pointer);
    for (std::size_t i = 0; i < mesh.size(); ++i) {
        const std::uint32_t a = table + static_cast<std::uint32_t>(i) * 12u;
        for (int axis = 0; axis < 3; ++axis) {
            write16(rdram, a + 2u * static_cast<std::uint32_t>(axis), mesh[i].outer[axis]);
            write16(rdram, a + 6u + 2u * static_cast<std::uint32_t>(axis), mesh[i].inner[axis]);
        }
    }
}

std::vector<MeshEntry> expected;
std::atomic<std::uint64_t> verified_calls{0};
std::atomic<std::uint64_t> mismatched_calls{0};

} // namespace

extern "C" bool blockplanet_sphere_mesh_begin(std::uint8_t* rdram, recomp_context* ctx) {
    const Mode m = mode();
    if (m == Mode::Original) return false;
    if (m == Mode::Verify) {
        // Inputs are read before the original caps the scale values, so read
        // them the same way without writing.
        build_mesh(gather(rdram, ctx, false), expected);
        return false;
    }
    std::vector<MeshEntry> mesh;
    build_mesh(gather(rdram, ctx, true), mesh);
    store(rdram, mesh);
    return true;
}

extern "C" void blockplanet_sphere_mesh_end(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (mode() != Mode::Verify || expected.empty()) return;
    const std::uint32_t table = read32(rdram, mesh_pointer);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const std::uint32_t a = table + static_cast<std::uint32_t>(i) * 12u;
        for (int axis = 0; axis < 3; ++axis) {
            if (read16(rdram, a + 2u * static_cast<std::uint32_t>(axis)) != expected[i].outer[axis] ||
                read16(rdram, a + 6u + 2u * static_cast<std::uint32_t>(axis)) != expected[i].inner[axis]) {
                ++differing;
                break;
            }
        }
    }
    const auto calls = ++verified_calls;
    if (differing != 0) ++mismatched_calls;
    if (differing != 0 || (calls & (calls - 1)) == 0) {  // every mismatch, and calls 1, 2, 4, 8...
        std::fprintf(stdout,
                     "{\"event\":\"sphere_mesh_verify\",\"calls\":%llu,\"mismatched_calls\":%llu,"
                     "\"differing_entries\":%zu}\n",
                     static_cast<unsigned long long>(calls),
                     static_cast<unsigned long long>(mismatched_calls.load()), differing);
        std::fflush(stdout);
    }
}
