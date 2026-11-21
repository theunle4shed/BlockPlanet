#include "tetrisphere/brick_textures.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
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
    std::uint32_t tlut_image = 0, tlut_to = 0, tlut_count = 0;  // the last palette load (G_LOADTLUT)
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
                tlut_image = image, tlut_to = to, tlut_count = count;
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
                    {
                        const std::uint32_t at = 0x800u + std::uint32_t(t.palette) * 0x80u + 8u * 8u;  // slot 8 in TMEM
                        if (tlut_count != 0u && at >= tlut_to && (at - tlut_to) / 8u < tlut_count) d.slot8_source = tlut_image + 2u * ((at - tlut_to) / 8u);
                    }
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

std::vector<std::uint32_t> arrow_triangles(const Read32& read32, std::uint32_t list, int budget) {
    std::vector<std::uint32_t> out;
    std::array<std::uint32_t, 16> segments{};
    std::uint32_t image = 0, loaded = 0;
    const auto resolve = [&](std::uint32_t a) { return (segments[(a >> 24) & 0xFu] + (a & 0x00FFFFFFu)) & 0x007FFFFFu; };
    const auto walk = [&](auto&& self, std::uint32_t at, int depth) -> void {
        for (; budget-- > 0; at += 8u) {
            const std::uint32_t w0 = read32(at), w1 = read32(at + 4u);
            const std::uint32_t op = w0 >> 24;
            if (op == 0xBCu && (w0 & 0xFFu) == 6u) segments[((w0 >> 8) & 0xFFFFu) / 4u & 0xFu] = w1 & 0x00FFFFFFu;
            if (op == 0xFDu) image = resolve(w1);
            if (op == 0xF3u || op == 0xF4u) loaded = image;  // G_LOADBLOCK, G_LOADTILE
            // Research: BLOCKPLANET_ARROW_LOG=1 prints the commands after an
            // arrow frame's image is set (a few times), to see how it is drawn.
            static const bool log = std::getenv("BLOCKPLANET_ARROW_LOG") != nullptr;
            static int logged = 0, follow = 0;
            if (log && op == 0xFDu && image >= arrow_first && image < arrow_first + arrow_stride * arrow_frames && logged < 4) ++logged, follow = 24;
            if (log && follow > 0) {
                --follow;
                std::fprintf(stdout, "{\"event\":\"arrow_cmd\",\"at\":\"%08X\",\"w0\":\"%08X\",\"w1\":\"%08X\",\"depth\":%d}\n", at, w0, w1, depth);
                std::fflush(stdout);
            }
            if ((op == 0xBFu || op == 0xB1u || op == 0xB5u) && loaded >= arrow_first &&
                loaded < arrow_first + arrow_stride * arrow_frames && (loaded - arrow_first) % arrow_stride == 0u)
                out.push_back(at);
            if (op == 0x06u && depth < 8) {
                self(self, resolve(w1), depth + 1);
                if (((w0 >> 16) & 0xFFu) == 1u) return;
            }
            if (op == 0xB8u) return;
        }
    };
    walk(walk, list & 0x007FFFFFu, 0);
    return out;
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

// A texel's palette index, and a palette entry as RGBA8 (as decode reads them).
std::uint32_t texel_index(const Draw& d, std::uint32_t x, std::uint32_t y) {
    std::uint32_t at = std::uint32_t(d.tile.tmem) * 8u + y * std::uint32_t(d.tile.line) * 8u + x / 2u;
    if (y & 1u) at ^= 4u;
    const std::uint8_t b = d.tmem[at & tmem_mask];
    return (x & 1u) ? (b & 0xFu) : (b >> 4);
}

std::uint32_t palette_colour(const Draw& d, std::uint32_t index) {
    const std::uint32_t p = 0x800u + std::uint32_t(d.tile.palette) * 0x80u + index * 8u;
    const std::uint16_t c = std::uint16_t(d.tmem[p & tmem_mask] << 8 | d.tmem[(p + 1u) & tmem_mask]);
    const auto five = [](std::uint32_t v) { return std::uint32_t((v << 3) | (v >> 2)); };
    return five((c >> 11) & 31u) | five((c >> 6) & 31u) << 8 | five((c >> 1) & 31u) << 16 | ((c & 1u) ? 0xFF000000u : 0u);
}

// The crystal's sparkle frame, marked in palette slot 8 (used only by the
// crystal tile and one other patterned tile): its two lowest blue bits.
constexpr std::uint32_t crystal_slot = 8;

Draw with_crystal_phase(const Draw& d, int phase) {
    Draw out = d;
    const std::uint32_t p = 0x800u + std::uint32_t(d.tile.palette) * 0x80u + crystal_slot * 8u;
    // The TLUT entry, repeated four times in TMEM (as the hardware loads it).
    for (std::uint32_t k = 0; k < 8u; k += 2u) {
        const std::uint32_t at = (p + k) & tmem_mask;
        std::uint16_t c = std::uint16_t(out.tmem[at] << 8 | out.tmem[(at + 1u) & tmem_mask]);
        c = std::uint16_t((c & ~0x6u) | ((std::uint32_t(phase) & 3u) << 1));
        out.tmem[at] = std::uint8_t(c >> 8), out.tmem[(at + 1u) & tmem_mask] = std::uint8_t(c);
    }
    return out;
}

bool has_crystal_tile(const Draw& d) { return d.height == 16 && d.width >= 13 * 16; }

std::vector<std::uint8_t> crafted(const Draw& d) {
    const std::vector<std::uint8_t> source = decode(d);
    const int sw = d.width, sh = d.height, scale = 8, tile = 16, big = tile * scale;
    const int w = sw * scale, h = sh * scale;
    const auto colour = [&](int x, int y) {
        std::uint32_t v;
        std::memcpy(&v, &source[(std::size_t(y) * sw + x) * 4u], 4);
        return v;
    };
    const auto tone = [](std::uint32_t c) { return double((c & 0xFFu) + ((c >> 8) & 0xFFu) + ((c >> 16) & 0xFFu)) / 3.0; };
    const auto scaled = [](std::uint32_t c, double f) {
        std::uint32_t out = c & 0xFF000000u;
        for (int k = 0; k < 3; ++k) out |= std::uint32_t(std::clamp(double((c >> (8 * k)) & 0xFFu) * f, 0.0, 255.0)) << (8 * k);
        return out;
    };
    // The patterned tiles (more than three colours, such as the power piece's
    // pattern) keep Sharp's look; the crystal tile is drawn below.
    std::vector<std::uint8_t> sharp = source;
    for (int i = 0, sw2 = sw, sh2 = sh; i < 3; ++i, sw2 *= 2, sh2 *= 2) sharp = scale2x(sharp, sw2, sh2);
    // The highlight sheet (the drop highlight laid over the bricks: black
    // darkens, the light rings make its bevelled frame) is not a brick
    // colour: its single cube is dark in the middle and light at the edge,
    // the other way round from a brick's. Kept as the original (sharpened):
    // drawn as bricks its frame was lost (the owner, 2026-11-19).
    if (sh == tile && sw >= 6 * tile &&
        tone(palette_colour(d, texel_index(d, 5 * tile + 8, 8))) + 10.0 < tone(palette_colour(d, texel_index(d, 5 * tile, 0))))
        return sharp;
    std::vector<std::uint8_t> out(std::size_t(w) * h * 4u);
    const auto put = [&](int x, int y, std::uint32_t c) { std::memcpy(&out[(std::size_t(y) * w + x) * 4u], &c, 4); };
    for (int t = 0; t < sw / tile; ++t) {
        const int tx = t * tile;
        std::vector<std::uint32_t> colours;
        for (int y = 0; y < tile; ++y)
            for (int x = 0; x < tile; ++x)
                if (std::find(colours.begin(), colours.end(), colour(tx + x, y)) == colours.end()) colours.push_back(colour(tx + x, y));
        // A crystal (the 13th tile: a dark block with a glowing orb, blurry in
        // the original; first called rubble): a square block with bevelled
        // edges (the owner's design, 2026-11-19), and since 2026-11-21 (the
        // owner: crystal-like, not too far from the original, coloured shards
        // inside) a cut gem where the orb was and a small shard in each
        // corner, its facets lit from the top left, some tinted, in the tile's
        // own colours (so palettes that cycle still cycle).
        if (t == 12 && sh == tile && colours.size() >= 3) {
            std::sort(colours.begin(), colours.end(), [&](std::uint32_t a, std::uint32_t b) { return tone(a) < tone(b); });
            std::uint32_t common = colours[colours.size() / 2];
            {
                int best = -1;
                for (const std::uint32_t c : colours) {
                    int n = 0;
                    for (int y = 0; y < tile; ++y)
                        for (int x = 0; x < tile; ++x) n += colour(tx + x, y) == c;
                    if (n > best && c != colours.front()) best = n, common = c;
                }
            }
            // Fixed colours since 2026-11-21 (the owner chose the tile as drawn
            // with the yellow bricks' palette: a near-black block, a yellow gem
            // with teal-green facets), whatever palette the stage gives it. Only
            // looks: the game tells blocks apart by their cell values, not colours.
            // (So stages whose crystal palette cycles no longer shimmer.)
            const std::uint32_t dark = 0xFF022A2Au, light = 0xFF12DCE2u;  // (ABGR) #2a2a02, #e2dc12
            common = 0xFF22222Au;                                          // #2a2222
            // The sparkle (the owner, 2026-11-21: a glimmer every few seconds):
            // the frame is marked in palette slot 8's lowest blue bits
            // (crystal_phase); 0 none, 1 a small star on the gem, 2 a larger one
            // and a glint on the top-right shard, 3 the star fading.
            const int phase = int((palette_colour(d, crystal_slot) >> 19) & 3u);
            const auto star = [](double px, double py, double cx, double cy, double size) {
                const double dx = std::abs(px - cx), dy = std::abs(py - cy);
                const auto ray = [&](double along, double across) {  // a thin ray fading to its tip
                    return along < size ? std::max(0.0, 1.0 - along / size) * std::max(0.0, 1.0 - across / (2.0 + 0.12 * size)) : 0.0;
                };
                const double d1 = (dx + dy) * 0.7071, d2 = std::abs(dx - dy) * 0.7071;
                const double glow = std::max(0.0, 1.0 - std::sqrt(dx * dx + dy * dy) / (size * 0.35));
                return std::min(1.0, std::max({ray(dx, dy), ray(dy, dx), 0.6 * ray(d1 * 1.6, d2), 0.6 * ray(d2 * 1.6, d1)}) + 1.3 * glow);
            };
            const auto sparkle = [&](double px, double py) {
                switch (phase) {
                case 1: return star(px, py, 54, 52, 26);
                case 2: return std::max(star(px, py, 54, 52, 46), star(px, py, 100, 28, 20));
                case 3: return 0.6 * star(px, py, 54, 52, 32);
                default: return 0.0;
                }
            };
            const auto mix = [](std::uint32_t a, std::uint32_t b, double f) {
                std::uint32_t out = 0xFF000000u;
                for (int k = 0; k < 3; ++k) {
                    const double va = double((a >> (8 * k)) & 0xFFu), vb = double((b >> (8 * k)) & 0xFFu);
                    out |= std::uint32_t(std::clamp(va + (vb - va) * f, 0.0, 255.0)) << (8 * k);
                }
                return out;
            };
            constexpr int edge = 7, band = 11;
            constexpr double pi = 3.14159265358979323846;
            // A tint for some facets: the light colour with its channels turned
            // round (red to green to blue), a quarter of the way.
            const std::uint32_t tint = 0xFF000000u | ((light & 0xFFu) << 8) | (((light >> 8) & 0xFFu) << 16) | ((light >> 16) & 0xFFu);
            // How lit a point is (0 dark to 1 light), or -1 outside the gem and shards.
            // The gem: an octagon (radius 31), its table an octagon of radius 14;
            // between them eight facets, each split in two, lit by which way it
            // faces (light from the top left). Shards: a long diamond in each
            // corner, pointing to the middle, its two halves light and dark.
            const auto facet = [&](double px, double py, bool& tinted) -> double {
                tinted = false;
                const double dx = px - 64.0, dy = py - 64.0, r = std::sqrt(dx * dx + dy * dy);
                const double a = std::atan2(dy, dx);
                // The octagon's edge distance at this angle (radius to a corner R).
                const auto oct = [&](double R) {
                    const double sector = std::fmod(a + 2.0 * pi + pi / 8.0, pi / 4.0) - pi / 8.0;
                    return R * std::cos(pi / 8.0) / std::cos(sector);
                };
                if (r <= oct(31.0)) {
                    if (r <= oct(14.0)) return 0.78 + 0.12 * (-(dx + dy) / 28.0);  // the table, brighter to the top left
                    const int k = int(std::floor((a + 2.0 * pi + pi / 8.0) / (pi / 4.0))) % 8;  // which facet
                    const double mid = k * pi / 4.0;
                    const double half = std::fmod(a - mid + 3.0 * pi, 2.0 * pi) - pi > 0 ? 1.0 : -1.0;
                    const double nx = std::cos(mid + half * 0.18), ny = std::sin(mid + half * 0.18);
                    tinted = (k % 3) == 1;
                    return std::clamp(0.5 + 0.45 * (-0.7071 * nx - 0.7071 * ny) + 0.06 * half, 0.05, 0.98);
                }
                const double corners[4][2] = {{28, 28}, {100, 28}, {28, 100}, {100, 100}};
                for (const auto& c : corners) {
                    const double ux = (64.0 - c[0]) / 50.9, uy = (64.0 - c[1]) / 50.9;  // towards the middle
                    const double qx = px - c[0], qy = py - c[1];
                    const double along = qx * ux + qy * uy, across = -qx * uy + qy * ux;
                    if (std::abs(along) / 13.0 + std::abs(across) / 6.0 > 1.0) continue;
                    // Its halves face either side of the diagonal.
                    const double nx = across > 0 ? -uy : uy, ny = across > 0 ? ux : -ux;
                    tinted = c[0] > 64.0 && c[1] < 64.0;
                    return std::clamp(0.5 + 0.45 * (-0.7071 * nx - 0.7071 * ny) + (along > 0 ? -0.08 : 0.08), 0.05, 0.98);
                }
                return -1.0;
            };
            for (int y = 0; y < big; ++y)
                for (int x = 0; x < big; ++x) {
                    double rgb[3] = {0, 0, 0};
                    for (int sy = 0; sy < 4; ++sy)
                        for (int sx = 0; sx < 4; ++sx) {
                            const double px = x + (sx + 0.5) / 4.0, py = y + (sy + 0.5) / 4.0;
                            const double light_from_above = 1.06 - 0.12 * py / double(big);
                            std::uint32_t c;
                            const double from_edge = std::min({px, py, big - px, big - py});
                            if (from_edge < edge) {
                                c = scaled(dark, 0.74 * light_from_above);
                            } else {
                                c = scaled(common, light_from_above);
                                if (from_edge < edge + band) {  // the bevel, nearest edge deciding
                                    const double up = py, left = px, down = big - py, right = big - px;
                                    const double nearest = std::min({up, left, down, right});
                                    c = scaled(c, nearest == up ? 1.24 : nearest == left ? 1.12 : nearest == down ? 0.78 : 0.88);
                                }
                                bool tinted = false;
                                if (const double lit = facet(px, py, tinted); lit >= 0.0) {
                                    c = lit < 0.5 ? mix(dark, common, lit * 2.0) : mix(common, light, (lit - 0.5) * 2.0);
                                    if (tinted) c = mix(c, tint, 0.28);
                                }
                                if (const double glint = sparkle(px, py); glint > 0.0) c = mix(c, 0xFFFFFFFFu, glint);
                            }
                            for (int k = 0; k < 3; ++k) rgb[k] += double((c >> (8 * k)) & 0xFFu);
                        }
                    std::uint32_t v = 0xFF000000u;
                    for (int k = 0; k < 3; ++k) v |= std::uint32_t(std::clamp(rgb[k] / 16.0, 0.0, 255.0)) << (8 * k);
                    put(tx * scale + x, y, v);
                }
            continue;
        }
        // Research: BLOCKPLANET_BRICK_MARK=1 paints the 9th and 10th tiles
        // (the patterned piece parts) red and cyan, to see when the game uses them.
        if (static const bool mark = std::getenv("BLOCKPLANET_BRICK_MARK") != nullptr; mark && (t == 8 || t == 9) && sh == tile) {
            for (int y = 0; y < big; ++y)
                for (int x = 0; x < big; ++x) put(tx * scale + x, y, t == 8 ? 0xFF2020F0u : 0xFFF0F020u);
            continue;
        }
        const bool piece_tile = t < 8 && sh == tile && sw >= 6 * tile;
        if (!piece_tile && (colours.size() > 3 || sh != tile)) {
            for (int y = 0; y < h; ++y)
                std::memcpy(&out[(std::size_t(y) * w + tx * scale) * 4u], &sharp[(std::size_t(y) * w + tx * scale) * 4u], std::size_t(big) * 4u);
            continue;
        }
        // A piece tile: the darkest colour is the outline, the rest is the
        // brick (the original's mid-tone ring inside the outline, its own
        // bevel, becomes brick too). Square: each texel is an 8 x 8 block.
        // The roles come from the palette indices, not the colours: some
        // palettes give the outline the brick's own colour (the green bricks:
        // the owner, 2026-11-19, "an edge problem"), but the tile still marks
        // it with its own index. The single cube (the 6th tile) shows which
        // index is the outline (its corner) and which the brick (its middle).
        const std::uint32_t outline_index = texel_index(d, 5 * tile, 0), base_index = texel_index(d, 5 * tile + 8, 8);
        const std::uint32_t base = palette_colour(d, base_index);
        std::uint32_t dark = palette_colour(d, outline_index);
        // Where the palette gives no darker outline, one is made from the brick.
        if (tone(base) - tone(dark) <= 30.0) dark = scaled(base, 0.45);
        std::vector<std::uint8_t> outline(std::size_t(big) * big);
        for (int y = 0; y < big; ++y)
            for (int x = 0; x < big; ++x)
                outline[std::size_t(y) * big + x] = outline_index != base_index && texel_index(d, tx + x / scale, y / scale) == outline_index;
        const auto is_outline = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < big && y < big && outline[std::size_t(y) * big + x] != 0;
        };
        // The bevel: a band inside the outline, light on the top and left,
        // dark on the bottom and right, meeting on the diagonal at the corners.
        constexpr int band = 11;
        for (int y = 0; y < big; ++y)
            for (int x = 0; x < big; ++x) {
                const double light = 1.06 - 0.12 * double(y) / double(big - 1);  // from above
                if (is_outline(x, y)) {
                    put(tx * scale + x, y, scaled(dark, 0.74 * light));
                    continue;
                }
                int up = band, left = band, down = band, right = band;
                for (int k = 1; k < band; ++k) {
                    if (up == band && is_outline(x, y - k)) up = k;
                    if (left == band && is_outline(x - k, y)) left = k;
                    if (down == band && is_outline(x, y + k)) down = k;
                    if (right == band && is_outline(x + k, y)) right = k;
                }
                double f = 1.0;
                const int nearest = std::min({up, left, down, right});
                if (nearest < band) {
                    if (nearest == up) f = 1.24;
                    else if (nearest == left) f = 1.12;
                    else if (nearest == down) f = 0.78;
                    else f = 0.88;
                }
                put(tx * scale + x, y, scaled(base, f * light));
            }
    }
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
