// Captures of BlockPlanet's menu screens over the menu grid, drawn in
// software (no game, ROM or graphics card needed), for reviewing menu
// changes at several screen sizes (AGENTS.md, UI changes). Written for
// BlockPlanet.
//
//   ui_capture <assets/ui folder> <output folder> [ROM (.z64)]
//
// Writes PNG files named <screen>-<width>x<height>.png, and a few frames of
// a colour change (flip-*.png). Without a ROM the grid shows its procedural
// texture, and pictures (controller art) are left out. The game's own
// picture behind the menus is a plain grey pattern.
#include "grid_texture.h"
#include "ui_grid.h"
#include "ui_navigator.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace blockplanet::ui;

namespace {

// A plain software rasteriser for RmlUi's triangles: transforms (with
// perspective), premultiplied alpha, scissoring and bilinear textures.
class SoftRenderer final : public Rml::RenderInterface {
public:
    int width = 0, height = 0;
    std::vector<std::array<float, 4>> pixels;  // premultiplied RGBA

    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign(std::size_t(w) * std::size_t(h), {0, 0, 0, 1});
    }
    void clear_to_game() {
        // Stands in for the game's picture: a grey checker pattern.
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const float v = ((x / 40 + y / 40) % 2) ? 0.42f : 0.30f;
                pixels[std::size_t(y) * width + x] = {v, v * 1.05f, v * 0.9f, 1};
            }
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        auto* g = new Geometry{{vertices.begin(), vertices.end()}, {indices.begin(), indices.end()}};
        return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { delete reinterpret_cast<Geometry*>(handle); }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& size, const Rml::String& source) override {
        if (source != "blockplanet-grid-tile") return 0;  // pictures are left out
        const auto& atlas = grid_tile_atlas();
        size = {atlas.width, atlas.height};
        return make_texture(atlas.rgba.data(), atlas.width, atlas.height);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i size) override {
        return make_texture(source.data(), size.x, size.y);
    }
    void ReleaseTexture(Rml::TextureHandle handle) override { delete reinterpret_cast<Texture*>(handle); }
    void EnableScissorRegion(bool enable) override { scissor_on_ = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override { scissor_ = region; }
    void SetTransform(const Rml::Matrix4f* transform) override {
        transform_ = transform ? *transform : Rml::Matrix4f::Identity();
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        const auto* g = reinterpret_cast<const Geometry*>(handle);
        const auto* t = reinterpret_cast<const Texture*>(texture);
        int x0 = 0, y0 = 0, x1 = width, y1 = height;
        if (scissor_on_) {
            x0 = std::max(x0, scissor_.Left());
            y0 = std::max(y0, scissor_.Top());
            x1 = std::min(x1, scissor_.Right());
            y1 = std::min(y1, scissor_.Bottom());
        }
        for (std::size_t i = 0; i + 2 < g->indices.size(); i += 3) {
            Corner c[3];
            for (int k = 0; k < 3; ++k) {
                const Rml::Vertex& v = g->vertices[std::size_t(g->indices[i + k])];
                const Rml::Vector4f p = transform_ * Rml::Vector4f(v.position.x + translation.x, v.position.y + translation.y, 0, 1);
                const float w = p.w != 0 ? p.w : 1e-6f;
                c[k] = {p.x / w, p.y / w, 1.0f / w,
                        {v.colour.red / 255.0f, v.colour.green / 255.0f, v.colour.blue / 255.0f, v.colour.alpha / 255.0f},
                        v.tex_coord.x, v.tex_coord.y};
            }
            triangle(c, t, x0, y0, x1, y1);
        }
    }

    bool write_png(const std::filesystem::path& path) const;

private:
    struct Geometry {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };
    struct Texture {
        int width, height;
        std::vector<std::uint8_t> rgba;
    };
    struct Corner {
        float x, y, inv_w;
        std::array<float, 4> colour;
        float u, v;
    };

    Rml::TextureHandle make_texture(const std::uint8_t* rgba, int w, int h) {
        if (rgba == nullptr || w <= 0 || h <= 0) return 0;
        auto* t = new Texture{w, h, std::vector<std::uint8_t>(rgba, rgba + std::size_t(w) * std::size_t(h) * 4)};
        return reinterpret_cast<Rml::TextureHandle>(t);
    }

    std::array<float, 4> sample(const Texture* t, float u, float v) const {
        if (t == nullptr) return {1, 1, 1, 1};
        const float x = std::clamp(u * float(t->width) - 0.5f, 0.0f, float(t->width - 1));
        const float y = std::clamp(v * float(t->height) - 0.5f, 0.0f, float(t->height - 1));
        const int ix = std::min(int(x), t->width - 1), iy = std::min(int(y), t->height - 1);
        const int jx = std::min(ix + 1, t->width - 1), jy = std::min(iy + 1, t->height - 1);
        const float fx = x - float(ix), fy = y - float(iy);
        std::array<float, 4> out{};
        for (int c = 0; c < 4; ++c) {
            const auto at = [&](int px, int py) { return float(t->rgba[(std::size_t(py) * t->width + px) * 4 + c]) / 255.0f; };
            const float top = at(ix, iy) + (at(jx, iy) - at(ix, iy)) * fx;
            const float bottom = at(ix, jy) + (at(jx, jy) - at(ix, jy)) * fx;
            out[c] = top + (bottom - top) * fy;
        }
        return out;
    }

    void triangle(const Corner* c, const Texture* t, int x0, int y0, int x1, int y1) {
        const float area = (c[1].x - c[0].x) * (c[2].y - c[0].y) - (c[2].x - c[0].x) * (c[1].y - c[0].y);
        if (std::abs(area) < 1e-8f) return;
        const int bx0 = std::max(x0, int(std::floor(std::min({c[0].x, c[1].x, c[2].x}))));
        const int by0 = std::max(y0, int(std::floor(std::min({c[0].y, c[1].y, c[2].y}))));
        const int bx1 = std::min(x1, int(std::ceil(std::max({c[0].x, c[1].x, c[2].x}))));
        const int by1 = std::min(y1, int(std::ceil(std::max({c[0].y, c[1].y, c[2].y}))));
        for (int y = by0; y < by1; ++y) {
            for (int x = bx0; x < bx1; ++x) {
                const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                float b[3];
                b[0] = ((c[1].x - px) * (c[2].y - py) - (c[2].x - px) * (c[1].y - py)) / area;
                b[1] = ((c[2].x - px) * (c[0].y - py) - (c[0].x - px) * (c[2].y - py)) / area;
                b[2] = 1.0f - b[0] - b[1];
                if (b[0] < 0 || b[1] < 0 || b[2] < 0) continue;
                // Perspective-correct: interpolate attributes over w.
                const float iw = b[0] * c[0].inv_w + b[1] * c[1].inv_w + b[2] * c[2].inv_w;
                float wt[3];
                for (int k = 0; k < 3; ++k) wt[k] = b[k] * c[k].inv_w / iw;
                std::array<float, 4> colour{};
                for (int ch = 0; ch < 4; ++ch) colour[ch] = wt[0] * c[0].colour[ch] + wt[1] * c[1].colour[ch] + wt[2] * c[2].colour[ch];
                const auto texel = sample(t, wt[0] * c[0].u + wt[1] * c[1].u + wt[2] * c[2].u, wt[0] * c[0].v + wt[1] * c[1].v + wt[2] * c[2].v);
                auto& dst = pixels[std::size_t(y) * width + x];
                const float a = colour[3] * texel[3];
                for (int ch = 0; ch < 4; ++ch) {
                    const float src = ch < 3 ? colour[ch] * texel[ch] : a;
                    dst[ch] = src + dst[ch] * (1.0f - a);
                }
            }
        }
    }

    Rml::Matrix4f transform_ = Rml::Matrix4f::Identity();
    bool scissor_on_ = false;
    Rml::Rectanglei scissor_{};
};

std::uint32_t crc_table(std::uint32_t n) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    return c;
}
std::uint32_t crc32(const std::vector<std::uint8_t>& data, std::size_t from) {
    static std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) t[n] = crc_table(n);
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = from; i < data.size(); ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
}
void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    be32(out, std::uint32_t(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    be32(out, crc32(out, start));
}

// An uncompressed PNG (stored deflate blocks): large, but needs no library.
bool SoftRenderer::write_png(const std::filesystem::path& path) const {
    std::vector<std::uint8_t> raw;
    raw.reserve(std::size_t(height) * (std::size_t(width) * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            const auto& p = pixels[std::size_t(y) * width + x];
            for (int c = 0; c < 3; ++c) raw.push_back(std::uint8_t(std::clamp(p[c], 0.0f, 1.0f) * 255.0f + 0.5f));
        }
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    for (std::size_t at = 0; at < raw.size(); at += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.push_back(std::uint8_t(n));
        z.push_back(std::uint8_t(n >> 8));
        z.push_back(std::uint8_t(~n));
        z.push_back(std::uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(at), raw.begin() + std::ptrdiff_t(at + n));
    }
    std::uint32_t a = 1, b = 0;
    for (auto byte : raw) a = (a + byte) % 65521, b = (b + a) % 65521;
    be32(z, b << 16 | a);
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    be32(header, std::uint32_t(width));
    be32(header, std::uint32_t(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    return bool(file);
}

class QuietSystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 10.0; }  // screens' own animations finished
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING) std::fprintf(stderr, "ui: %s\n", message.c_str());
        return true;
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: ui_capture <assets/ui folder> <output folder> [ROM (.z64)]\n");
        return 64;
    }
    const std::filesystem::path assets = argv[1], out = argv[2];
    // With the player's ROM, the grid shows the game's marble (read here,
    // never written out).
    std::vector<std::uint8_t> rom;
    if (argc == 4) {
        std::ifstream file(argv[3], std::ios::binary);
        rom.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        set_grid_rom(rom);
    }
    std::filesystem::create_directories(out);
    SoftRenderer renderer;
    QuietSystem system;
    Rml::SetRenderInterface(&renderer);
    Rml::SetSystemInterface(&system);
    if (!Rml::Initialise()) return 1;
    for (const char* font : {"LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf", "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf", "lucide.ttf"})
        Rml::LoadFontFace((assets / "fonts" / font).string());

    const std::array<Rml::Vector2i, 4> sizes = {Rml::Vector2i(1440, 1080), Rml::Vector2i(1920, 1080), Rml::Vector2i(2560, 1080),
                                                Rml::Vector2i(1080, 1350)};
    int failures = 0;
    for (const auto size : sizes) {
        Rml::Context* context = Rml::CreateContext("capture", size);
        context->SetDensityIndependentPixelRatio(float(size.y) / 1080.0f);
        const std::string suffix = "-" + std::to_string(size.x) + "x" + std::to_string(size.y) + ".png";
        {
            Navigator navigator(context, assets / "screens");
            MenuGrid grid((assets / "grid.rml").string());
            double now = 0.0;
            // The grid at 60 frames a second up to `to` (it flies in and out over time).
            const auto advance = [&](double to, bool cover = false) {
                for (; now < to; now += 1.0 / 60.0) grid.update(context, grid_colour_of(navigator.documents()), now, cover);
            };
            const auto frame = [&](const std::string& name, bool cover = false) {
                grid.update(context, grid_colour_of(navigator.documents()), now, cover);
                for (auto* document : navigator.documents()) document->SetClass("on-grid", grid.visible());
                context->Update();
                renderer.resize(size.x, size.y);
                renderer.clear_to_game();
                context->Render();
                if (!renderer.write_png(out / (name + suffix))) ++failures;
            };
            navigator.open("main");
            advance(grid_fly_total * 0.5);
            frame("fly-in");  // half way in, over the game
            advance(2.0);
            frame("main");
            navigator.show("solo");
            grid.update(context, grid_colour_of(navigator.documents()), now);  // the change starts
            const double start = now;
            for (const float at : {0.12f, 0.3f, 0.5f}) {
                advance(start + at);
                char name[32];
                std::snprintf(name, sizeof(name), "flip-%03d", int(at * 1000));
                frame(name);
            }
            advance(start + 2.0);
            frame("solo");
            navigator.show("story");
            advance(now + 2.0);
            frame("story");
            navigator.open("options");
            advance(now + 2.0);
            frame("options");
            navigator.open("pause");
            frame("pause-start");  // the change into the pause's cycle starts
            advance(now + 2.0);
            frame("pause");
            for (const auto& [setting, value] : grid_lab_values()) navigator.set_setting(setting, value);
            navigator.set_setting("grid_theme", "blue");
            navigator.open("grid_lab");
            advance(now + 2.0);
            frame("grid_lab");
            // Closing the menus: the grid flies out, revealing the game.
            navigator.close();
            advance(now + grid_fly_total * 0.4);
            frame("fly-out");
        }
        Rml::RemoveContext("capture");
    }
    Rml::Shutdown();
    std::printf("captures in %s (grid texture: %s)\n", out.string().c_str(), grid_texture_from_rom() ? "ROM" : "procedural");
    return failures == 0 ? 0 : 1;
}
