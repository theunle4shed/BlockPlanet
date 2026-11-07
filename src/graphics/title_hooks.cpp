// The title screen's 3D letters (docs/research/blockplanet-game-state.md, the
// title screen's artwork).
//
// The intro (func_80067798) draws the letters as a LightWave scene (pointer at
// 0x800E1670; each orange letter is one object, 11 to 21). When the title
// starts, the game stops drawing it (the pointer is cleared) but the scene and
// its geometry stay in memory. With BLOCKPLANET_TITLE_LETTERS=1, a hook in the
// title routine (func_800685FC, after the title picture at 0x8006923C) draws
// the scene again, letters only, held at the intro's last frame.

#include "tetrisphere/recomp_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t letters_pointer = 0x800E1670u;
std::uint32_t letters_scene = 0;     // remembered from the intro
std::atomic<bool> drawing{false};    // set while BlockPlanet draws the letters
// When the title picture last came up to be drawn (steady clock, ms) and its
// address, for BlockPlanet's title overlay (the copyright lines).
std::atomic<long long> title_seen_ms{0};
std::atomic<std::uint32_t> title_seen_image{0};

std::uint32_t load32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t v;
    std::memcpy(&v, rdram + (address & 0x00FFFFFFu), 4);
    return v;
}

// The frame the title's 3D letters are held at: 150 (fully formed, before
// they fade into the picture); BLOCKPLANET_TITLE_FREEZE=frame changes it, -1
// keeps the original title.
int title_freeze_frame() {
    static const int frame = [] {
        const char* v = std::getenv("BLOCKPLANET_TITLE_FREEZE");
        return v ? std::atoi(v) : 150;
    }();
    return frame;
}

} // namespace

// The title's painted picture (logo, characters, ring, rockets, copyright;
// image pointer at 0x800E1614) and its soft shadow (0x800E1638) are not drawn
// while the 3D letters are held: they are to be replaced (docs/HUD_DESIGN.md
// has the plan in the research notes).
extern "C" bool blockplanet_title_skip_image(std::uint8_t* rdram, std::uint32_t image) {
    if (title_freeze_frame() < 0 || image == 0) return false;
    if (image == load32(rdram, 0x800E1614u)) {
        title_seen_image.store(image);
        title_seen_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        return true;
    }
    return image == load32(rdram, 0x800E1638u);
}

// Whether the title picture came up in the last quarter second (and so the
// title is on screen), with its address.
extern "C" bool blockplanet_title_recent(std::uint32_t* image) {
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - title_seen_ms.load() > 250) return false;
    if (image != nullptr) *image = title_seen_image.load();
    return true;
}

namespace {

// N64 fixed-point matrix (4 x 4, row vectors): 16 integer halves then 16
// fraction halves, in 32-bit words kept in host order (halves at ^2).
float mtx_get(const std::uint8_t* rdram, std::uint32_t m, int i, int j) {
    const auto half = [&](std::uint32_t a) {
        const std::uint32_t o = (a & 0x00FFFFFFu) ^ 2u;
        return static_cast<std::uint16_t>(rdram[o] | (rdram[o + 1] << 8));
    };
    const int k = 4 * i + j;
    return static_cast<float>(static_cast<std::int16_t>(half(m + 2u * k))) + half(m + 32u + 2u * k) / 65536.0f;
}
void mtx_set(std::uint8_t* rdram, std::uint32_t m, int i, int j, float v) {
    const auto put = [&](std::uint32_t a, std::uint16_t x) {
        const std::uint32_t o = (a & 0x00FFFFFFu) ^ 2u;
        rdram[o] = static_cast<std::uint8_t>(x & 0xFF);
        rdram[o + 1] = static_cast<std::uint8_t>(x >> 8);
    };
    const auto fixed = static_cast<std::int32_t>(std::lround(v * 65536.0f));
    const int k = 4 * i + j;
    put(m + 2u * k, static_cast<std::uint16_t>(static_cast<std::uint32_t>(fixed) >> 16));
    put(m + 32u + 2u * k, static_cast<std::uint16_t>(fixed & 0xFFFF));
}

// Face-on letters (experiment, BLOCKPLANET_TITLE_FACE=1; off by default:
// the letters drift out of place, because their geometry sits far from their
// pivots and the centre used here is not the real one). Each letter is drawn with its object matrix multiplied by
// the scene's camera matrix (scene +0xF8, double-buffered by +0x3D). The
// object's rotation is replaced by the inverse of the camera's, so the letter
// faces the camera squarely (turned by BLOCKPLANET_TITLE_TILT degrees about the
// vertical, default 0), keeping its size; it turns about its own centre (the
// three floats at its parts data +0x10, in object space), so it stays in place.
void face_letter(std::uint8_t* rdram, std::uint32_t scene, int object, std::uint32_t parts) {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_TITLE_FACE");
        return v != nullptr && std::atoi(v) != 0;
    }();
    static const float tilt = [] {
        const char* v = std::getenv("BLOCKPLANET_TITLE_TILT");
        return v ? std::strtof(v, nullptr) * 3.14159265f / 180.0f : 0.0f;
    }();
    if (!on) return;
    const std::uint32_t objects = load32(rdram, scene + 0x14);
    const std::uint8_t sel = rdram[((scene + 0x3D) & 0x00FFFFFFu) ^ 3u];
    const std::uint32_t m = objects + static_cast<std::uint32_t>(object) * 0x120u + sel * 0x40u;
    const std::uint32_t cam = scene + 0xF8u + sel * 0x40u;
    if (m < 0x80000000u || m >= 0x80800000u || parts < 0x80000000u || parts >= 0x80800000u) return;
    float R[3][3], C[3][3], T[3], centre[3];
    for (int i = 0; i < 3; ++i) {
        T[i] = mtx_get(rdram, m, 3, i);
        for (int j = 0; j < 3; ++j) {
            R[i][j] = mtx_get(rdram, m, i, j);
            C[i][j] = mtx_get(rdram, cam, i, j);
        }
        std::memcpy(&centre[i], rdram + ((parts + 0x10u + 4u * i) & 0x00FFFFFFu), 4);
    }
    // The object's size, and the camera's rotation normalised.
    float size = 0.0f, cam_scale = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float r = 0.0f, c = 0.0f;
        for (int j = 0; j < 3; ++j) r += R[i][j] * R[i][j], c += C[i][j] * C[i][j];
        size += std::sqrt(r) / 3.0f;
        cam_scale += std::sqrt(c) / 3.0f;
    }
    if (cam_scale <= 0.0f) return;
    // New rotation: size * Y(tilt) * transpose(C / |C|) (row vectors).
    const float ct = std::cos(tilt), st = std::sin(tilt);
    const float Y[3][3] = {{ct, 0, -st}, {0, 1, 0}, {st, 0, ct}};
    float N[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k) v += Y[i][k] * C[j][k] / cam_scale;
            N[i][j] = size * v;
        }
    // Keep the centre where it was: T' = T + centre * R - centre * N.
    for (int j = 0; j < 3; ++j) {
        float before = 0.0f, after = 0.0f;
        for (int k = 0; k < 3; ++k) before += centre[k] * R[k][j], after += centre[k] * N[k][j];
        mtx_set(rdram, m, 3, j, T[j] + before - after);
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) mtx_set(rdram, m, i, j, N[i][j]);
}

} // namespace

extern "C" bool blockplanet_title_object(std::uint8_t* rdram, std::uint32_t scene, int object, std::uint32_t parts) {
    if (title_freeze_frame() >= 0 && scene == letters_scene && object >= 11 && object <= 21) face_letter(rdram, scene, object, parts);
    return blockplanet_title_skip_object(scene, object);
}

extern "C" bool blockplanet_title_skip_object(std::uint32_t scene, int object) {
    return drawing.load(std::memory_order_relaxed) && scene == letters_scene && (object < 11 || object > 21);
}

// A snapshot of the letters scene's memory (its state and per-object data,
// up to the next scene) taken when its animation reaches the held frame.
std::vector<std::uint8_t> snapshot;
std::uint32_t snapshot_start = 0;

int held_frame() {
    static const int hold = [] {  // research: BLOCKPLANET_TITLE_FRAME
        const char* v = std::getenv("BLOCKPLANET_TITLE_FRAME");
        return v ? std::atoi(v) : 160;
    }();
    return hold;
}

// Called at every model draw: remembers the letters scene while the intro has
// it, and snapshots it at the held frame.
extern "C" void blockplanet_title_watch(std::uint8_t* rdram) {
    const std::uint32_t current = load32(rdram, letters_pointer);
    if (current < 0x80000000u || current >= 0x80800000u) return;
    letters_scene = current;
    const int frame = static_cast<int>(load32(rdram, current + 0x24));
    if (snapshot.empty() && frame >= held_frame()) {
        static const std::uint32_t size = [] {  // research: BLOCKPLANET_TITLE_SNAPSHOT (bytes)
            const char* v = std::getenv("BLOCKPLANET_TITLE_SNAPSHOT");
            return v ? static_cast<std::uint32_t>(std::strtoul(v, nullptr, 0)) : 0x9F70u;
        }();
        snapshot_start = current;
        snapshot.assign(rdram + (current & 0x00FFFFFFu), rdram + (current & 0x00FFFFFFu) + size);
        std::fprintf(stdout, "{\"event\":\"title_snapshot\",\"frame\":%d,\"bytes\":%u}\n", frame, size);
        std::fflush(stdout);
    }
}

// Before the title's own draw of the letters scene: puts the scene back and
// holds it at a frame, so the title routine draws it every frame; only the
// letters (objects 11 to 21) are drawn.
extern "C" void blockplanet_title_letters(std::uint8_t* rdram, recomp_context*) {
    static const bool enabled = std::getenv("BLOCKPLANET_TITLE_LETTERS") != nullptr;
    if (!enabled || letters_scene == 0) return;
    const int hold = held_frame();
    if (snapshot.empty()) return;
    std::memcpy(rdram + (snapshot_start & 0x00FFFFFFu), snapshot.data(), snapshot.size());
    std::memcpy(rdram + (letters_pointer & 0x00FFFFFFu), &letters_scene, 4);
    static const bool no_filter = std::getenv("BLOCKPLANET_TITLE_NOFILTER") != nullptr;  // research
    drawing.store(!no_filter);
    static int logged = 0;
    if (std::getenv("BLOCKPLANET_TITLE_LOG") != nullptr && logged++ % 60 == 0) {
        std::fprintf(stdout, "{\"event\":\"title_letters\",\"scene\":\"0x%08X\",\"frame\":%d}\n", letters_scene, hold);
        std::fflush(stdout);
    }
}

extern "C" void blockplanet_title_letters_end(std::uint8_t*, recomp_context*) { drawing.store(false); }

// BLOCKPLANET_TITLE_FREEZE=frame: just before the title advances and draws the
// letters scene (0x80069460, a1 = frames elapsed, from 0x800E1208 - 0x800E1210),
// the scene's frame is set so that after advancing it lands on that frame, so
// the animation never ends and the game keeps drawing the letters.
extern "C" void blockplanet_title_freeze(std::uint8_t* rdram, recomp_context* ctx) {
    static const int freeze = title_freeze_frame();
    if (freeze < 0) return;
    const std::uint32_t scene = static_cast<std::uint32_t>(ctx->r4);
    if (scene < 0x80000000u || scene >= 0x80800000u) return;
    const int frame = static_cast<int>(load32(rdram, scene + 0x24));
    const int elapsed = static_cast<int>(load32(rdram, 0x800E1208u) - load32(rdram, 0x800E1210u));
    if (frame + elapsed < freeze) return;  // not there yet
    const std::uint32_t target = static_cast<std::uint32_t>(std::max(0, freeze - std::max(elapsed, 0)));
    std::memcpy(rdram + ((scene + 0x24) & 0x00FFFFFFu), &target, 4);
}


namespace {
// The title's ring (docs/research/blockplanet-game-state.md, rebuilding the
// title): the painted picture's orange oval, rebuilt as BlockPlanet's own 3D
// model, an elliptical ring extruded towards the viewer, so it is sharp at any
// size. Added to the game's display list where the picture was drawn (before
// the letters, so they stay in front), in the picture's place on the 320 x 240
// frame. All of it is generated here; nothing comes from the game.
struct RingStyle {
    float centre_x = 161.0f, centre_y = 138.0f;      // in the 320 x 240 frame
    float outer_x = 124.0f, outer_y = 64.0f;          // half-widths of the outer edge
    float rim = 21.0f;                                // the rim's width at the sides
    float rim_vertical = 0.55f;                       // ... and its share at the top and bottom
    float depth = 12.0f;                              // extrusion
    float back_dx = 2.0f, back_dy = -6.0f;            // where the back shows (seen from a little above)
    float tilt = -0.06f;                              // radians, as in the picture
    std::array<float, 3> face{0.90f, 0.40f, 0.09f};   // orange
    std::array<float, 3> shadow{0.40f, 0.12f, 0.03f};
    std::array<float, 3> light{1.00f, 0.74f, 0.36f};
    std::array<float, 3> inside_centre{0.30f, 0.20f, 0.46f};  // the opening: dusky purple
    std::array<float, 3> inside_edge{0.07f, 0.06f, 0.14f};
};

constexpr std::uint32_t display_list_pointer = 0x800F22B4u;  // the game's next free command
constexpr std::uint32_t ring_memory = 0x807E0000u;           // matrices, then vertices (unused by the game)

void put32(std::uint8_t* rdram, std::uint32_t address, std::uint32_t v) {
    std::memcpy(rdram + (address & 0x00FFFFFFu), &v, 4);
}
// Bytes and halves in guest order (words are kept in host order).
void put16(std::uint8_t* rdram, std::uint32_t address, std::uint16_t v) {
    const std::uint32_t o = (address & 0x00FFFFFFu) ^ 2u;
    rdram[o] = static_cast<std::uint8_t>(v & 0xFF);
    rdram[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put8(std::uint8_t* rdram, std::uint32_t address, std::uint8_t v) { rdram[(address & 0x00FFFFFFu) ^ 3u] = v; }

// An N64 fixed-point matrix from a row-vector 4 x 4 (v * M).
void put_matrix(std::uint8_t* rdram, std::uint32_t address, const float (&m)[4][4]) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const auto fixed = static_cast<std::int32_t>(std::lround(m[i][j] * 65536.0f));
            put16(rdram, address + 2u * std::uint32_t(4 * i + j), static_cast<std::uint16_t>((fixed >> 16) & 0xFFFF));
            put16(rdram, address + 32u + 2u * std::uint32_t(4 * i + j), static_cast<std::uint16_t>(fixed & 0xFFFF));
        }
}

struct RingVertex { float x, y, z; std::array<float, 3> colour; };

std::array<float, 3> mix(const std::array<float, 3>& a, const std::array<float, 3>& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

// The ring as quad strips: each strip is pairs of vertices around the oval.
std::vector<std::vector<RingVertex>> ring_strips(const RingStyle& st, int segments) {
    // Drawn in order (no depth test): the opening, the sides, then the face.
    std::vector<std::vector<RingVertex>> strips(4);
    const float ix = st.outer_x - st.rim, iy = st.outer_y - st.rim * st.rim_vertical;
    const float c = std::cos(st.tilt), sn = std::sin(st.tilt);
    const auto place = [&](float x, float y, float z) {
        // Centred, y up (the frame's y runs down), tilted.
        return std::array<float, 3>{x * c - y * sn, x * sn + y * c, z};
    };
    for (int k = 0; k <= segments; ++k) {
        const float a = 6.2831853f * float(k) / float(segments);
        const float ca = std::cos(a), sa = std::sin(a);
        // Light from the upper left: how much each part of the rim faces it.
        const float facing = 0.5f + 0.5f * (-ca * 0.6f + sa * 0.8f);
        const auto o = place(st.outer_x * ca, st.outer_y * sa, 0.0f);
        const auto i = place(ix * ca, iy * sa, 0.0f);
        const auto ob = place(st.outer_x * ca + st.back_dx, st.outer_y * sa + st.back_dy, -st.depth);
        const auto ib = place(ix * ca + st.back_dx, iy * sa + st.back_dy, -st.depth);
        const auto centre = place(st.back_dx, st.back_dy, -st.depth);
        const auto front_outer = mix(st.face, st.light, facing * 0.8f);
        const auto front_inner = mix(st.face, st.shadow, (1.0f - facing) * 0.5f);
        // The opening: from the back edge to the centre, darker at the edge.
        strips[0].push_back({ib[0], ib[1], ib[2], st.inside_edge});
        strips[0].push_back({centre[0], centre[1], centre[2], st.inside_centre});
        // Outer side: lit on the top, dark below.
        const auto outer_side = mix(st.shadow, st.face, 0.2f + 0.8f * (0.5f + 0.5f * sa));
        strips[1].push_back({o[0], o[1], o[2], outer_side});
        strips[1].push_back({ob[0], ob[1], ob[2], mix(outer_side, st.shadow, 0.7f)});
        // Inner side (the wall inside the opening): lit at the bottom, where it faces up.
        const auto inner_side = mix(st.shadow, st.face, 0.15f + 0.85f * (0.5f - 0.5f * sa));
        strips[2].push_back({i[0], i[1], i[2], inner_side});
        strips[2].push_back({ib[0], ib[1], ib[2], mix(inner_side, st.shadow, 0.6f)});
        // Front face: outer then inner edge.
        strips[3].push_back({o[0], o[1], o[2], front_outer});
        strips[3].push_back({i[0], i[1], i[2], front_inner});
    }
    return strips;
}

void draw_title_ring(std::uint8_t* rdram) {
    static const RingStyle style;
    static const auto strips = ring_strips(style, 56);
    std::uint32_t dl = load32(rdram, display_list_pointer);
    if (dl < 0x80000000u || dl >= 0x80800000u) return;
    // The projection in use, put back afterwards (the last projection load
    // in this frame's list, looked for a little way back).
    std::uint32_t projection = 0;
    for (std::uint32_t at = dl - 8u, n = 0; n < 4096 && at >= 0x80000000u; at -= 8u, ++n) {
        const std::uint32_t w0 = load32(rdram, at);
        if ((w0 >> 24) == 0x01u && (w0 >> 16 & 0x01u) != 0) {
            projection = load32(rdram, at + 4u);
            break;
        }
    }
    if (projection == 0) return;
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
        put32(rdram, dl, w0);
        put32(rdram, dl + 4u, w1);
        dl += 8u;
    };
    // Our projection: the frame's centred units straight to clip space.
    const std::uint32_t proj = ring_memory, model = ring_memory + 0x40u;
    const float p[4][4] = {{1.0f / 160.0f, 0, 0, 0}, {0, 1.0f / 120.0f, 0, 0}, {0, 0, 1.0f / 512.0f, 0}, {0, 0, 0, 1}};
    const float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    put_matrix(rdram, proj, p);
    put_matrix(rdram, model, id);
    cmd(0xE7000000u, 0);                       // pipe sync
    cmd(0xBA001402u, 0x00000000u);             // one-cycle
    cmd(0xB900031Du, 0x0F0A4000u);             // opaque surface, no z
    cmd(0xFCFFFFFFu, 0xFFFE793Cu);             // combine: shade
    cmd(0xBB000000u, 0x00000000u);             // texture off
    cmd(0xB6000000u, 0xFFFFFFFFu);             // clear geometry modes
    cmd(0xB7000000u, 0x00000204u);             // shade, smooth
    cmd(0x01030040u, proj);                    // projection: load
    cmd(0x01060040u, model);                   // modelview: load, push
    // Vertices: strips in chunks of 8 pairs (16 vertices), 14 triangles each.
    std::uint32_t vtx = ring_memory + 0x100u;
    for (const auto& strip : strips) {
        for (std::size_t start = 0; start + 2 < strip.size(); start += 14) {
            const std::size_t count = std::min<std::size_t>(16, strip.size() - start);
            const std::uint32_t base = vtx;
            for (std::size_t k = 0; k < count; ++k) {
                const auto& v = strip[start + k];
                const std::uint32_t a = vtx + 16u * std::uint32_t(k);
                put16(rdram, a + 0, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v.x))));
                put16(rdram, a + 2, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v.y))));
                put16(rdram, a + 4, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v.z))));
                put16(rdram, a + 6, 0);
                put16(rdram, a + 8, 0);
                put16(rdram, a + 10, 0);
                for (int ch = 0; ch < 3; ++ch)
                    put8(rdram, a + 12u + std::uint32_t(ch), static_cast<std::uint8_t>(std::clamp(v.colour[std::size_t(ch)], 0.0f, 1.0f) * 255.0f));
                put8(rdram, a + 15u, 0xFF);
            }
            vtx += 16u * std::uint32_t(count);
            // G_VTX: count - 1 in bits 20.., slot 0, length.
            cmd(0x04000000u | (std::uint32_t(count - 1) << 20) | std::uint32_t(count * 16), base);
            for (std::size_t q = 0; q + 3 < count; q += 2) {  // one quad per two pairs
                const std::uint32_t a0 = std::uint32_t(q), a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
                cmd(0xBF000000u, (a0 * 10u) << 16 | (a1 * 10u) << 8 | (b0 * 10u));
                cmd(0xBF000000u, (a1 * 10u) << 16 | (b1 * 10u) << 8 | (b0 * 10u));
            }
        }
    }
    cmd(0xBD000000u, 0x00000000u);             // pop the modelview
    cmd(0x01030040u, projection);              // the game's projection again
    cmd(0xE7000000u, 0);
    put32(rdram, display_list_pointer, dl);
}
} // namespace

// With the letters held (BLOCKPLANET_TITLE_FREEZE), the title picture is drawn
// fully opaque (its alpha argument, on the stack at +0x1C).
extern "C" void blockplanet_title_picture(std::uint8_t* rdram, recomp_context* ctx) {
    if (title_freeze_frame() < 0) return;
    static const bool ring = std::getenv("BLOCKPLANET_TITLE_RING") == nullptr || std::atoi(std::getenv("BLOCKPLANET_TITLE_RING")) != 0;
    if (ring) draw_title_ring(rdram);
    const std::uint32_t alpha = 0xFF;
    std::memcpy(rdram + ((static_cast<std::uint32_t>(ctx->r29) + 0x1C) & 0x00FFFFFFu), &alpha, 4);
}
