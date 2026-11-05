#include "tetrisphere/sphere_mesh.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <optional>
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
        // The game keeps the blended values in 16 bits.
        auto narrow = [&](std::int32_t value) { return in.wide ? value : static_cast<std::int32_t>(s16(value)); };
        auto current = blended(0);
        for (int column = 0; column < in.columns; ++column) {
            const auto next = blended(column + 1);
            std::int16_t v[3];
            for (int axis = 0; axis < 3; ++axis) {
                const std::int32_t n = narrow(next[axis]);
                // Blend toward the current column by the column fraction, then
                // drop to the game's fixed-point scale.
                v[axis] = s16(lerp_step(n, narrow(current[axis]), column_weight) >> 5);
            }
            if (in.scale_enabled) {
                for (int axis = 0; axis < 3; ++axis) v[axis] = apply_scale(v[axis], in.scale[axis]);
            }
            MeshEntry& e = out[index++];
            for (int axis = 0; axis < 3; ++axis) {
                if (in.core_scale == 1.0) {
                    e.inner[axis] = s16(v[axis] / 10);
                    e.outer[axis] = s16(v[axis] / 3);
                } else {
                    // Top of layer 8 stays at v x (1/3 + 7/10).
                    const double top = v[axis] * (1.0 / 3.0 + 7.0 / 10.0);
                    const double base = v[axis] / 3.0 * in.core_scale;
                    e.outer[axis] = s16(static_cast<std::int32_t>(std::lround(base)));
                    e.inner[axis] = s16(static_cast<std::int32_t>(std::lround((top - base) / 7.0)));
                }
            }
            if (in.two_player) e.outer[0] = s16(e.outer[0] + (in.player_one ? 0x40 : -0x20));
            current = next;
        }
    }
}

namespace {

std::atomic<Detail> requested_detail{Detail::Original};
std::atomic<double> requested_view_scale{[] {
    const char* text = std::getenv("BLOCKPLANET_SPHERE_SIZE");
    const double parsed = text != nullptr ? std::strtod(text, nullptr) : 1.0;
    return parsed > 0.3 && parsed < 1.5 ? parsed : 1.0;
}()};
std::atomic<Mode> frame_mode{Mode::Original};
std::atomic<int> frame_ring_value{0};

// BLOCKPLANET_SPHERE, when set to a known value.
std::optional<Mode> mode_override() {
    static const std::optional<Mode> value = []() -> std::optional<Mode> {
        const char* setting = std::getenv("BLOCKPLANET_SPHERE");
        if (setting == nullptr) return std::nullopt;
        if (std::strcmp(setting, "original") == 0) return Mode::Original;
        if (std::strcmp(setting, "verify") == 0) return Mode::Verify;
        if (std::strcmp(setting, "replace") == 0) return Mode::Replace;
        return std::nullopt;
    }();
    return value;
}

std::optional<int> ring_override() {
    static const std::optional<int> value = []() -> std::optional<int> {
        const char* setting = std::getenv("BLOCKPLANET_SPHERE_RING");
        if (setting == nullptr) return std::nullopt;
        const int n = std::atoi(setting);
        return n < 0 ? 0 : (n > 4 ? 4 : n);
    }();
    return value;
}

} // namespace

void set_view_scale(double scale) { requested_view_scale.store(std::clamp(scale, 0.3, 1.5)); }
double view_scale() { return requested_view_scale.load(); }

void set_detail(Detail detail) { requested_detail.store(detail); }

void latch_frame_mode() {
    const Detail detail = requested_detail.load();
    const Mode m = mode_override().value_or(detail == Detail::Extended ? Mode::Replace : Mode::Original);
    frame_mode.store(m);
    const int ring = ring_override().value_or(detail == Detail::Extended ? extended_detail_ring : 0);
    const int frame = m == Mode::Replace ? ring : 0;
    frame_ring_value.store(frame);
    // Log each change of drawing mode.
    static int last = -1;
    const int key = static_cast<int>(m) * 16 + frame;
    if (key != last) {
        last = key;
        static constexpr const char* names[]{"original", "verify", "replace"};
        std::fprintf(stdout, "{\"event\":\"sphere_mode\",\"mode\":\"%s\",\"ring\":%d}\n",
                     names[static_cast<int>(m)], frame);
        std::fflush(stdout);
    }
}

Mode mode() {
    // Before the first frame's mesh, follow the override alone.
    return frame_mode.load();
}

int frame_ring() { return frame_ring_value.load(); }

} // namespace tetrisphere::sphere
