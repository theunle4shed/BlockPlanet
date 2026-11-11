#include "grid_texture.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace blockplanet::ui {
namespace {

// The main archive and its marble (docs/research/blockplanet-assets.md).
constexpr std::size_t main_archive = 0x736C50;
constexpr unsigned marble_entry = 0x000;
constexpr int marble_width = 160, marble_height = 120;

std::uint32_t be16(std::span<const std::uint8_t> b, std::size_t o) { return std::uint32_t(b[o]) << 8 | b[o + 1]; }
std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t o) {
    return std::uint32_t(b[o]) << 24 | std::uint32_t(b[o + 1]) << 16 | std::uint32_t(b[o + 2]) << 8 | b[o + 3];
}

// Pass J's 16-bit LZ (func_80081264): a byte with the top bit set copies
// (byte & 0x7F) + 1 halfwords from an earlier halfword index (the next u16)
// in this block's output; otherwise byte + 1 literal halfwords follow.
bool unpack_j(std::span<const std::uint8_t> src, std::vector<std::uint16_t>& out) {
    std::size_t i = 0;
    while (i < src.size()) {
        const std::uint8_t c = src[i++];
        if (c & 0x80) {
            if (i + 2 > src.size()) return false;
            const std::size_t count = (c & 0x7Fu) + 1u, from = be16(src, i);
            i += 2;
            for (std::size_t k = 0; k < count; ++k) {
                if (from + k >= out.size()) return false;
                out.push_back(out[from + k]);
            }
        } else {
            const std::size_t count = std::size_t(c) + 1u;
            if (i + count * 2 > src.size()) return false;
            for (std::size_t k = 0; k < count; ++k, i += 2) out.push_back(std::uint16_t(be16(src, i)));
        }
    }
    return true;
}

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Value noise on a lattice, from a fixed hash (the same every run).
float lattice(int x, int y) {
    std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
}
float noise(float x, float y) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const float fx = smooth(x - float(ix)), fy = smooth(y - float(iy));
    const float a = lattice(ix, iy), b = lattice(ix + 1, iy), c = lattice(ix, iy + 1), d = lattice(ix + 1, iy + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

std::mutex atlas_mutex;
std::span<const std::uint8_t> atlas_rom;
std::optional<TileAtlas> atlas;
bool atlas_from_rom = false;

} // namespace

std::optional<std::vector<std::uint8_t>> unpack_archive_entry(std::span<const std::uint8_t> rom, std::size_t archive,
                                                              unsigned entry) {
    if (archive + 2 > rom.size()) return std::nullopt;
    const unsigned count = be16(rom, archive);
    if (entry >= count) return std::nullopt;
    const std::size_t table = archive + 2 + std::size_t(entry) * 4;
    if (table + 8 > rom.size()) return std::nullopt;
    const std::size_t start = archive + be32(rom, table), end = archive + be32(rom, table + 4);
    if (end <= start || end > rom.size()) return std::nullopt;
    const auto blob = rom.subspan(start, end - start);
    const std::size_t passes = blob[0];
    if (passes != 1 || blob.size() < 6 || blob[1] != 'J') return std::nullopt;
    std::size_t pos = 2;
    const std::size_t stop = std::min<std::size_t>(blob.size(), pos + 4 + be32(blob, pos));
    pos += 4;
    std::vector<std::uint8_t> out;
    while (pos + 12 <= stop && blob[pos] == 'S' && blob[pos + 1] == 'Q' && blob[pos + 2] == 'S' && blob[pos + 3] == 'H') {
        const std::size_t unpacked = be32(blob, pos + 4), packed = be32(blob, pos + 8);
        pos += 12;
        if (pos + packed > blob.size() || unpacked > (1u << 24)) return std::nullopt;
        const auto data = blob.subspan(pos, packed);
        if (unpacked == packed) {
            out.insert(out.end(), data.begin(), data.end());
        } else {
            std::vector<std::uint16_t> halves;
            if (!unpack_j(data, halves) || halves.size() * 2 < unpacked) return std::nullopt;
            for (std::size_t k = 0; k < unpacked; ++k)
                out.push_back(std::uint8_t(k % 2 == 0 ? halves[k / 2] >> 8 : halves[k / 2] & 0xFF));
        }
        if (out.size() % 2) out.push_back(0);
        pos += packed;
    }
    if (out.empty()) return std::nullopt;
    return out;
}

std::optional<GreyImage> marble_from_rom(std::span<const std::uint8_t> rom) {
    auto bytes = unpack_archive_entry(rom, main_archive, marble_entry);
    if (!bytes || bytes->size() != std::size_t(marble_width * marble_height)) return std::nullopt;
    return GreyImage{marble_width, marble_height, std::move(*bytes)};
}

GreyImage procedural_marble() {
    GreyImage image{marble_width, marble_height, std::vector<std::uint8_t>(marble_width * marble_height)};
    for (int y = 0; y < marble_height; ++y) {
        for (int x = 0; x < marble_width; ++x) {
            const float fx = float(x), fy = float(y);
            float turbulence = 0.0f, amplitude = 0.5f, scale = 1.0f / 24.0f;
            for (int octave = 0; octave < 4; ++octave, amplitude *= 0.5f, scale *= 2.0f)
                turbulence += amplitude * noise(fx * scale + 17.0f * float(octave), fy * scale);
            // Soft veins running diagonally, bent by the turbulence.
            const float vein = 0.5f + 0.5f * std::sin((fx * 0.6f + fy) * 0.07f + turbulence * 7.0f);
            const float value = 0.55f * turbulence + 0.45f * std::pow(vein, 3.0f);
            image.pixels[std::size_t(y) * marble_width + x] = std::uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        }
    }
    return image;
}

TileAtlas make_tile_atlas(const GreyImage& marble, int tile, int variants) {
    TileAtlas result;
    if (marble.width <= 1 || marble.height <= 1 || tile <= 0 || variants <= 0 ||
        marble.pixels.size() < std::size_t(marble.width) * std::size_t(marble.height))
        return result;
    result.tile = tile;
    result.variants = variants;
    result.width = tile * variants;
    result.height = tile;
    result.rgba.assign(std::size_t(result.width) * std::size_t(result.height) * 4, 255);

    // The marble's spread: its middle (median) becomes 1, the 5th and 95th
    // percentiles about 0.8 and 1.2.
    std::vector<std::uint8_t> sorted = marble.pixels;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];
    const float spread = std::max(8.0f, float(sorted[sorted.size() * 95 / 100]) - float(sorted[sorted.size() * 5 / 100]));
    const auto sample = [&](float x, float y) {
        x = std::clamp(x, 0.0f, float(marble.width - 1));
        y = std::clamp(y, 0.0f, float(marble.height - 1));
        const int ix = std::min(int(x), marble.width - 2), iy = std::min(int(y), marble.height - 2);
        const float fx = x - float(ix), fy = y - float(iy);
        const auto at = [&](int px, int py) { return float(marble.pixels[std::size_t(py) * marble.width + px]); };
        const float top = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * fx;
        const float bottom = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * fx;
        return top + (bottom - top) * fy;
    };

    // Each variant a square part of the marble (most of its height), at a
    // different place and mirrored differently, so neighbours differ.
    const float crop = float(std::min(marble.width, marble.height)) * 0.8f;
    const float free_x = float(marble.width) - crop, free_y = float(marble.height) - crop;
    const float bevel = std::max(2.0f, float(tile) * 0.06f);
    for (int v = 0; v < variants; ++v) {
        const float ox = free_x * float((v * 5) % 7) / 6.0f, oy = free_y * float((v * 3) % 5) / 4.0f;
        const bool mirror_x = v % 2 == 1, mirror_y = v % 4 >= 2;
        for (int y = 0; y < tile; ++y) {
            for (int x = 0; x < tile; ++x) {
                float u = (float(x) + 0.5f) / float(tile), w = (float(y) + 0.5f) / float(tile);
                if (mirror_x) u = 1.0f - u;
                if (mirror_y) w = 1.0f - w;
                float f = 1.0f + (sample(ox + u * crop, oy + w * crop) - median) / spread * 0.4f;
                // The bevel: highlights on the top and left edges, shadows on
                // the bottom and right ones (the nearer edge wins).
                const float left = float(x) + 0.5f, top = float(y) + 0.5f;
                const float right = float(tile) - left, bottom = float(tile) - top;
                const float light = std::min(left, top), dark = std::min(right, bottom);
                if (light < bevel && light <= dark) f *= 1.0f + 0.5f * (1.0f - light / bevel);
                else if (dark < bevel) f *= 1.0f - 0.4f * (1.0f - dark / bevel);
                const auto grey = std::uint8_t(std::clamp(f * 128.0f, 0.0f, 255.0f));
                std::uint8_t* p = &result.rgba[(std::size_t(y) * result.width + std::size_t(v * tile + x)) * 4];
                p[0] = p[1] = p[2] = grey;
                p[3] = 255;
            }
        }
    }
    return result;
}

void set_grid_rom(std::span<const std::uint8_t> rom) {
    std::lock_guard lock(atlas_mutex);
    atlas_rom = rom;
}

const TileAtlas& grid_tile_atlas() {
    std::lock_guard lock(atlas_mutex);
    if (!atlas) {
        auto marble = marble_from_rom(atlas_rom);
        atlas_from_rom = marble.has_value();
        atlas = make_tile_atlas(marble ? *marble : procedural_marble());
        atlas_rom = {};  // read once; the ROM is not kept
    }
    return *atlas;
}

bool grid_texture_from_rom() {
    std::lock_guard lock(atlas_mutex);
    return atlas_from_rom;
}

} // namespace blockplanet::ui
