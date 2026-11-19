#include "tetrisphere/brick_textures.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tetrisphere::bricks {
namespace {

constexpr std::uint32_t tmem_mask = 4095;

}  // namespace

void find_draws(const Read32& read32, std::uint32_t list, const std::function<void(const Draw&)>& found, int budget) {
    std::array<std::uint8_t, 4096> tmem{};
    std::array<Tile, 8> tiles{};
    std::array<std::uint32_t, 16> segments{};
    std::uint32_t image = 0, image_siz = 0, image_width = 1;
    std::uint32_t other_h = 0;
    std::uint32_t size_uls = 0, size_ult = 0, size_lrs = 0, size_lrt = 0;  // tile 0's size
    const auto resolve = [&](std::uint32_t a) { return (segments[(a >> 24) & 0xFu] + (a & 0x00FFFFFFu)) & 0x007FFFFFu; };
    const auto byte = [&](std::uint32_t a) { return std::uint8_t(read32(a & ~3u) >> (24u - 8u * (a & 3u))); };

    const auto walk = [&](auto&& self, std::uint32_t at, int depth) -> void {
        for (; budget-- > 0; at += 8u) {
            const std::uint32_t w0 = read32(at), w1 = read32(at + 4u);
            switch (w0 >> 24) {
            case 0xBC:  // G_MOVEWORD: a segment's base
                if ((w0 & 0xFFu) == 6u) segments[((w0 >> 8) & 0xFFFFu) / 4u & 0xFu] = w1 & 0x00FFFFFFu;
                break;
            case 0xBA: {  // G_SETOTHERMODE_H
                const std::uint32_t shift = (w0 >> 8) & 0xFFu, length = w0 & 0xFFu;
                const std::uint32_t mask = length >= 32 ? ~0u : ((1u << length) - 1u) << shift;
                other_h = (other_h & ~mask) | (w1 & mask);
                break;
            }
            case 0xFD:  // G_SETTIMG
                image = resolve(w1);
                image_siz = (w0 >> 19) & 3u;
                image_width = (w0 & 0xFFFu) + 1u;
                break;
            case 0xF5: {  // G_SETTILE
                Tile& t = tiles[(w1 >> 24) & 7u];
                t.fmt = std::uint8_t((w0 >> 21) & 7u);
                t.siz = std::uint8_t((w0 >> 19) & 3u);
                t.line = std::uint16_t((w0 >> 9) & 0x1FFu);
                t.tmem = std::uint16_t(w0 & 0x1FFu);
                t.palette = std::uint8_t((w1 >> 20) & 0xFu);
                break;
            }
            case 0xF2:  // G_SETTILESIZE
                if (((w1 >> 24) & 7u) == 0u) {
                    size_uls = (w0 >> 12) & 0xFFFu, size_ult = w0 & 0xFFFu;
                    size_lrs = (w1 >> 12) & 0xFFFu, size_lrt = w1 & 0xFFFu;
                }
                break;
            case 0xF3: {  // G_LOADBLOCK: texels in a run; odd lines (by dxt) swap their words
                const Tile& t = tiles[(w1 >> 24) & 7u];
                const std::uint32_t uls = (w0 >> 12) & 0xFFFu, ult = w0 & 0xFFFu;
                const std::uint32_t lrs = (w1 >> 12) & 0xFFFu, dxt = w1 & 0xFFFu;
                const std::uint32_t texels = lrs >= uls ? lrs - uls + 1u : 0u;
                const std::uint32_t bytes = std::min<std::uint32_t>(4096u, (texels << image_siz) >> 1);
                const std::uint32_t from = image + (((ult * image_width + uls) << image_siz) >> 1);
                const std::uint32_t to = std::uint32_t(t.tmem) * 8u;
                for (std::uint32_t i = 0; i < bytes; ++i) {
                    const std::uint32_t word = i / 8u;
                    const bool odd = dxt != 0 && (((word * dxt) >> 11) & 1u) != 0;
                    tmem[(to + (odd ? (i ^ 4u) : i)) & tmem_mask] = byte(from + i);
                }
                break;
            }
            case 0xF0: {  // G_LOADTLUT: each 16-bit colour four times
                const Tile& t = tiles[(w1 >> 24) & 7u];
                const std::uint32_t count = ((w1 >> 14) & 0x3FFu) + 1u;
                const std::uint32_t to = std::uint32_t(t.tmem) * 8u;
                for (std::uint32_t i = 0; i < count && i < 256u; ++i) {
                    const std::uint8_t hi = byte(image + 2u * i), lo = byte(image + 2u * i + 1u);
                    for (std::uint32_t k = 0; k < 4; ++k) {
                        tmem[(to + i * 8u + 2u * k) & tmem_mask] = hi;
                        tmem[(to + i * 8u + 2u * k + 1u) & tmem_mask] = lo;
                    }
                }
                break;
            }
            case 0xBF: case 0xB1: {  // G_TRI1 (F3D), G_TRI2 (F3DEX)
                const Tile& t = tiles[0];
                const std::uint32_t width = ((size_lrs - size_uls) >> 2) + 1u, height = ((size_lrt - size_ult) >> 2) + 1u;
                // A brick sheet: 4-bit colour indices, 256 x 16, rows of 128 bytes.
                if (t.fmt == 2u && t.siz == 0u && t.line == 16u && width == 256u && height == 16u && (other_h & 0xC000u) != 0u) {
                    Draw d;
                    d.tile = t;
                    d.width = std::uint16_t(width);
                    d.height = std::uint16_t(height);
                    d.tlut = other_h & 0xC000u;
                    d.tmem = tmem;
                    found(d);
                }
                break;
            }
            default: break;
            }
            if ((w0 >> 24) == 0x06u && depth < 8) {  // G_DL: a call, or a branch
                self(self, resolve(w1), depth + 1);
                if (((w0 >> 16) & 0xFFu) == 1u) return;
            }
            if ((w0 >> 24) == 0xB8u) return;  // G_ENDDL
        }
    };
    walk(walk, list & 0x007FFFFFu, 0);
}

std::vector<std::uint8_t> decode(const Draw& d) {
    std::vector<std::uint8_t> out(std::size_t(d.width) * d.height * 4u);
    const std::uint32_t base = std::uint32_t(d.tile.tmem) * 8u, row = std::uint32_t(d.tile.line) * 8u;
    const std::uint32_t palette = 0x800u + std::uint32_t(d.tile.palette) * 0x80u;
    for (std::uint32_t y = 0; y < d.height; ++y)
        for (std::uint32_t x = 0; x < d.width; ++x) {
            std::uint32_t at = base + y * row + x / 2u;
            if (y & 1u) at ^= 4u;
            const std::uint8_t b = d.tmem[at & tmem_mask];
            const std::uint32_t index = (x & 1u) ? (b & 0xFu) : (b >> 4);
            const std::uint32_t p = palette + index * 8u;
            const std::uint16_t c = std::uint16_t(d.tmem[p & tmem_mask] << 8 | d.tmem[(p + 1u) & tmem_mask]);
            std::uint8_t* o = &out[(std::size_t(y) * d.width + x) * 4u];
            const auto five = [](std::uint32_t v) { return std::uint8_t((v << 3) | (v >> 2)); };
            o[0] = five((c >> 11) & 31u);
            o[1] = five((c >> 6) & 31u);
            o[2] = five((c >> 1) & 31u);
            o[3] = (c & 1u) ? 255 : 0;
        }
    return out;
}

std::vector<std::uint8_t> scale2x(const std::vector<std::uint8_t>& in, int w, int h) {
    std::vector<std::uint8_t> out(std::size_t(w) * h * 16u);
    const auto px = [&](int x, int y) {
        x = std::clamp(x, 0, w - 1), y = std::clamp(y, 0, h - 1);
        std::uint32_t v;
        std::memcpy(&v, &in[(std::size_t(y) * w + x) * 4u], 4);
        return v;
    };
    const auto put = [&](int x, int y, std::uint32_t v) { std::memcpy(&out[(std::size_t(y) * w * 2 + x) * 4u], &v, 4); };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint32_t p = px(x, y), a = px(x, y - 1), b = px(x + 1, y), c = px(x - 1, y), d = px(x, y + 1);
            std::uint32_t e0 = p, e1 = p, e2 = p, e3 = p;
            if (c == a && c != d && a != b) e0 = a;
            if (a == b && a != c && b != d) e1 = b;
            if (d == c && d != b && c != a) e2 = c;
            if (b == d && b != a && d != c) e3 = d;
            put(2 * x, 2 * y, e0);
            put(2 * x + 1, 2 * y, e1);
            put(2 * x, 2 * y + 1, e2);
            put(2 * x + 1, 2 * y + 1, e3);
        }
    return out;
}

std::vector<std::uint8_t> dds(const std::vector<std::uint8_t>& rgba, int width, int height, int levels) {
    std::vector<std::uint8_t> out;
    const auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
    };
    u32(0x20534444u);  // "DDS "
    u32(124);           // header size
    u32(0x1u | 0x2u | 0x4u | 0x8u | 0x1000u | 0x20000u);  // caps, height, width, pitch, pixel format, mipmap count
    u32(std::uint32_t(height));
    u32(std::uint32_t(width));
    u32(std::uint32_t(width) * 4u);
    u32(0);  // depth
    u32(std::uint32_t(levels));
    for (int i = 0; i < 11; ++i) u32(0);
    u32(32);           // pixel format size
    u32(0x4u);         // FOURCC
    u32(0x30315844u);  // "DX10"
    for (int i = 0; i < 5; ++i) u32(0);
    u32(0x1000u | 0x400000u | 0x8u);  // texture, mipmap, complex
    for (int i = 0; i < 4; ++i) u32(0);
    u32(28);  // DXGI_FORMAT_R8G8B8A8_UNORM
    u32(3);   // 2D texture
    u32(0);
    u32(1);   // one texture
    u32(0);
    std::vector<std::uint8_t> level = rgba;
    int w = width, h = height;
    for (int l = 0; l < levels; ++l) {
        out.insert(out.end(), level.begin(), level.end());
        if (l + 1 == levels) break;
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<std::uint8_t> next(std::size_t(nw) * nh * 4u);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    const auto at = [&](int xx, int yy) { return int(level[(std::size_t(std::min(yy, h - 1)) * w + std::min(xx, w - 1)) * 4u + c]); };
                    next[(std::size_t(y) * nw + x) * 4u + c] =
                        std::uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        level = std::move(next);
        w = nw, h = nh;
    }
    return out;
}

std::vector<std::uint8_t> crafted(const Draw& d) {
    const std::vector<std::uint8_t> source = decode(d);
    const int sw = d.width, sh = d.height, scale = 8, tile = 16;
    const int w = sw * scale, h = sh * scale;
    // The tone of each colour (its brightness), to deepen the darkest.
    const auto colour = [&](int x, int y) {
        std::uint32_t v;
        std::memcpy(&v, &source[(std::size_t(y) * sw + x) * 4u], 4);
        return v;
    };
    std::vector<std::uint8_t> out(std::size_t(w) * h * 4u);
    // One sample: the colour whose region covers this point, from the 4 x 4
    // nearest texels of the same tile weighted by a Gaussian of their
    // distance (smooth, crisp boundaries with rounded corners; never across
    // tiles).
    const auto sample = [&](double u, double v, double out_rgba[4]) {
        const int tx = int(u) / tile * tile, ty = int(v) / tile * tile;
        const int cx = int(std::floor(u - 0.5)), cy = int(std::floor(v - 0.5));
        std::uint32_t colours[16];
        double weights[16];
        int n = 0;
        for (int dy = -1; dy <= 2; ++dy)
            for (int dx = -1; dx <= 2; ++dx) {
                const int x = std::clamp(cx + dx, tx, tx + tile - 1), y = std::clamp(cy + dy, ty, ty + tile - 1);
                const double ddx = (x + 0.5) - u, ddy = (y + 0.5) - v;
                const double wgt = std::exp(-(ddx * ddx + ddy * ddy) / (2.0 * 0.45 * 0.45));
                const std::uint32_t c = colour(x, y);
                int k = 0;
                while (k < n && colours[k] != c) ++k;
                if (k == n) colours[n] = c, weights[n++] = 0;
                weights[k] += wgt;
            }
        int best = 0;
        for (int k = 1; k < n; ++k)
            if (weights[k] > weights[best]) best = k;
        for (int k = 0; k < 4; ++k) out_rgba[k] = double((colours[best] >> (8 * k)) & 0xFFu);
    };
    std::vector<double> tone(std::size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // 4 x 4 samples a pixel, for smooth edges.
            double sum[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    double c[4];
                    sample((x + (sx + 0.5) / 4.0) / scale, (y + (sy + 0.5) / 4.0) / scale, c);
                    // A deeper outline: dark colours darker still.
                    const double t = (c[0] + c[1] + c[2]) / 3.0;
                    const double deepen = t < 110.0 ? 0.78 : 1.0;
                    for (int k = 0; k < 3; ++k) sum[k] += c[k] * deepen;
                    sum[3] += c[3];
                }
            std::uint8_t* o = &out[(std::size_t(y) * w + x) * 4u];
            for (int k = 0; k < 4; ++k) o[k] = std::uint8_t(std::clamp(sum[k] / 16.0, 0.0, 255.0));
            tone[std::size_t(y) * w + x] = (sum[0] + sum[1] + sum[2]) / 48.0;
        }
    // The bevel: a light line just inside an edge above (darker pixels a few
    // pixels up, in the same tile), a soft shade just inside an edge below;
    // and a gentle light from above within each tile.
    const int reach = 4;
    std::vector<std::uint8_t> lit = out;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int in_tile = y % (tile * scale);
            const double here = tone[std::size_t(y) * w + x];
            double factor = 1.08 - 0.16 * double(in_tile) / double(tile * scale - 1);
            if (in_tile >= reach && tone[std::size_t(y - reach) * w + x] < here - 35.0) factor *= 1.18;
            else if (in_tile < tile * scale - reach && tone[std::size_t(y + reach) * w + x] < here - 35.0) factor *= 0.88;
            std::uint8_t* o = &lit[(std::size_t(y) * w + x) * 4u];
            for (int k = 0; k < 3; ++k) o[k] = std::uint8_t(std::clamp(out[(std::size_t(y) * w + x) * 4u + k] * factor, 0.0, 255.0));
        }
    out = std::move(lit);
    return out;
}

std::vector<std::uint8_t> replacement(const Draw& d, Style style) {
    int w = d.width, h = d.height;
    std::vector<std::uint8_t> image;
    if (style == Style::Crafted) {
        image = crafted(d);
        w *= 8, h *= 8;
    } else {
        image = decode(d);
        for (int i = 0; i < 3; ++i) {  // 8 times larger
            image = scale2x(image, w, h);
            w *= 2, h *= 2;
        }
    }
    return dds(image, w, h, 4);  // 8x, 4x, 2x, the original size
}

}  // namespace tetrisphere::bricks
