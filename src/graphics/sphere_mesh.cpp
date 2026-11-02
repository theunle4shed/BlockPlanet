#include "tetrisphere/sphere_mesh.h"

#include <cstdlib>
#include <cstring>

namespace tetrisphere::sphere {
namespace {

// MIPS mult keeps the low 32 bits; the game then shifts them right by 16.
std::int32_t lerp_step(std::int32_t from, std::int32_t to, std::int32_t weight) {
    const auto product = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(to - from) * static_cast<std::uint32_t>(weight));
    return from + (product >> 16);
}

std::int16_t s16(std::int32_t value) { return static_cast<std::int16_t>(value); }

// (value * scale) >> 14, rounding toward zero like the original.
std::int16_t apply_scale(std::int16_t value, std::int16_t scale) {
    std::int32_t product = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(value) * static_cast<std::uint32_t>(scale));
    if (product < 0) product += 0x3FFF;
    return s16(product >> 14);
}

} // namespace

void build_mesh(const MeshInputs& in, std::vector<MeshEntry>& out) {
    out.assign(static_cast<std::size_t>(in.rows) * static_cast<std::size_t>(in.columns), MeshEntry{});
    const std::int32_t row_weight = static_cast<std::int32_t>(in.row_fraction) << 8;
    const std::int32_t column_weight = static_cast<std::int32_t>(in.column_fraction) << 8;
    std::size_t index = 0;
    for (int row = 0; row < in.rows; ++row) {
        // Blend this row with the next by the row fraction, column by column.
        auto blended = [&](int column) {
            const auto a = in.base(row, column);
            const auto b = in.base(row + 1, column);
            return std::array<std::int32_t, 3>{
                lerp_step(a[0], b[0], row_weight),
                lerp_step(a[1], b[1], row_weight),
                lerp_step(a[2], b[2], row_weight)};
        };
        auto current = blended(0);
        for (int column = 0; column < in.columns; ++column) {
            const auto next = blended(column + 1);
            std::int16_t v[3];
            for (int axis = 0; axis < 3; ++axis) {
                const std::int32_t n = s16(next[axis]);
                // Blend toward the current column by the column fraction, then
                // drop to the game's fixed-point scale.
                v[axis] = s16(lerp_step(n, s16(current[axis]), column_weight) >> 5);
            }
            if (in.scale_enabled) {
                for (int axis = 0; axis < 3; ++axis) v[axis] = apply_scale(v[axis], in.scale[axis]);
            }
            MeshEntry& e = out[index++];
            for (int axis = 0; axis < 3; ++axis) {
                e.inner[axis] = s16(v[axis] / 10);
                e.outer[axis] = s16(v[axis] / 3);
            }
            if (in.two_player) e.outer[0] = s16(e.outer[0] + (in.player_one ? 0x40 : -0x20));
            current = next;
        }
    }
}

Mode mode() {
    static const Mode value = [] {
        const char* setting = std::getenv("BLOCKPLANET_SPHERE");
        if (setting == nullptr) return Mode::Original;
        if (std::strcmp(setting, "verify") == 0) return Mode::Verify;
        if (std::strcmp(setting, "replace") == 0) return Mode::Replace;
        return Mode::Original;
    }();
    return value;
}

} // namespace tetrisphere::sphere
