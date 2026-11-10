// Tests for the Escape view's camera (src/graphics/escape_view.cpp) on a
// made-up mesh table: from the sphere's centre, the patch's middle is ahead
// at the screen's centre, its row 0 above, and the view is not mirrored.

#include "tetrisphere/escape_view.h"
#include "tetrisphere/guest_memory.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace tetrisphere;
using namespace tetrisphere::guest;

using Mat = std::array<std::array<double, 4>, 4>;
Mat read_matrix(const std::uint8_t* r, std::uint32_t a) {
    Mat m{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const int k = 4 * i + j;
            const std::int32_t hi = load16(r, a + 2 * k);
            const std::uint16_t lo = std::uint16_t(load16(r, a + 32 + 2 * k));
            m[i][j] = double(std::int32_t(std::uint32_t(hi) << 16 | lo)) / 65536.0;
        }
    return m;
}
std::array<double, 4> apply(std::array<double, 4> v, const Mat& m) {
    std::array<double, 4> o{};
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) o[j] += v[i] * m[i][j];
    return o;
}

int main() {
    std::vector<std::uint8_t> ram(0x800000);
    std::uint8_t* m = ram.data();
    const std::uint32_t mesh = 0x80127000u;
    store32(m, 0x800E44E4u, mesh);
    // Points about 10.5 degrees apart round a middle facing (0.6, 0, 0.8), row 0 towards +y.
    const double c = 0.8, s = 0.6;
    for (int row = 0; row < 16; ++row)
        for (int column = 0; column < 16; ++column) {
            const double a = (column - 7.5) * 0.184, b = (7.5 - row) * 0.184;
            const double d0[3] = {std::sin(a) * std::cos(b), std::sin(b), std::cos(a) * std::cos(b)};
            const double d[3] = {c * d0[0] + s * d0[2], d0[1], -s * d0[0] + c * d0[2]};  // turned about y
            const std::uint32_t e = mesh + std::uint32_t(row) * 0xC0u + std::uint32_t(column) * 12u;
            for (int k = 0; k < 3; ++k) {
                store16(m, e + 2u * std::uint32_t(k), std::int16_t(std::lround(d[k] * 16384.0 / 3.0)));
                store16(m, e + 6u + 2u * std::uint32_t(k), std::int16_t(std::lround(d[k] * 16384.0 / 10.0)));
            }
        }
    assert(escape_view::write_camera(m, 100.0f));
    const Mat p = read_matrix(m, escape_view::memory), v = read_matrix(m, escape_view::memory + 0x40);
    const auto screen = [&](int row, int column) {
        const std::uint32_t e = mesh + std::uint32_t(row) * 0xC0u + std::uint32_t(column) * 12u;
        const auto clip = apply(apply({double(load16(m, e)), double(load16(m, e + 2)), double(load16(m, e + 4)), 1.0}, v), p);
        assert(clip[3] > 0);  // ahead of the camera
        return std::array<double, 2>{clip[0] / clip[3], clip[1] / clip[3]};
    };
    const auto mid = screen(7, 7), top = screen(0, 7), bottom = screen(15, 7), left = screen(7, 0), right = screen(7, 15);
    assert(std::fabs(mid[0]) < 0.15 && std::fabs(mid[1]) < 0.15);
    assert(top[1] > mid[1] && bottom[1] < mid[1]);
    // From inside the columns run the other way round to the outside view
    // (seen from the far side), and the view itself is not mirrored.
    assert(left[0] > right[0]);
    double det = 0;
    {
        const auto& a = v;
        det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
              a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    }
    assert(det > 0.99 && det < 1.01);
    std::puts("escape_view ok");
    return 0;
}
