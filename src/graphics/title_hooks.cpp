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
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t letters_pointer = 0x800E1670u;
std::uint32_t letters_scene = 0;     // remembered from the intro
std::atomic<bool> drawing{false};    // set while BlockPlanet draws the letters
// When the title picture last came up to be drawn (steady clock, ms) and its
// address, for BlockPlanet's title overlay (the copyright lines).
std::atomic<long long> title_seen_ms{0};
std::atomic<std::uint32_t> title_seen_image{0};
std::atomic<long long> title_routine_ms{0};  // when the title routine last drew (its letters hook)
long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

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
    // Only the title routine (the intro's logo screens draw the same image).
    if (now - title_seen_ms.load() > 250 || now - title_routine_ms.load() > 250) return false;
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

namespace { std::uint32_t letters_dl_start = 0; }

// The letters a little brighter, to stand apart from the ring: the lights the
// letters scene loaded this frame are copied (into BlockPlanet's memory, so
// nothing compounds from frame to frame), brightened, and pointed to.
extern "C" void blockplanet_title_letters_end(std::uint8_t* rdram, recomp_context*) {
    drawing.store(false);
    if (title_freeze_frame() < 0 || letters_dl_start == 0) return;
    const std::uint32_t to = load32(rdram, 0x800F22B4u);
    static bool logged = std::getenv("BLOCKPLANET_TITLE_DL_LOG") == nullptr;
    if (!logged) {
        logged = true;
        for (std::uint32_t a = letters_dl_start; a < to && a < letters_dl_start + 8u * 300u; a += 8u)
            std::fprintf(stdout, "LDL %08X %08X\n", load32(rdram, a), load32(rdram, a + 4u));
        std::fflush(stdout);
    }
    // Their lights are already at full strength (texture x shade), so the
    // combiner adds a small warm lift instead: its second cycle's added term
    // becomes the environment colour, set just before the letters (in the
    // freeze hook) and put back to 0 after them.
    for (std::uint32_t a = letters_dl_start; a + 8u <= to && a < letters_dl_start + 8u * 16384u; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a), w1 = load32(rdram, a + 4u);
        if (w0 == 0xFC26A004u && w1 == 0x1F0C93FFu) {
            const std::uint32_t lifted = (w1 & ~(7u << 6)) | (5u << 6);
            std::memcpy(rdram + ((a + 4u) & 0x00FFFFFFu), &lifted, 4);
        }
    }
    {
        const std::uint32_t env0 = 0xFB000000u, env1 = 0;
        std::memcpy(rdram + (to & 0x00FFFFFFu), &env0, 4);
        std::memcpy(rdram + ((to + 4u) & 0x00FFFFFFu), &env1, 4);
        const std::uint32_t next = to + 8u;
        std::memcpy(rdram + (0x800F22B4u & 0x00FFFFFFu), &next, 4);
    }
    letters_dl_start = 0;
}

// BLOCKPLANET_TITLE_FREEZE=frame: just before the title advances and draws the
// letters scene (0x80069460, a1 = frames elapsed, from 0x800E1208 - 0x800E1210),
// the scene's frame is set so that after advancing it lands on that frame, so
// the animation never ends and the game keeps drawing the letters.
extern "C" void blockplanet_title_freeze(std::uint8_t* rdram, recomp_context* ctx) {
    static const int freeze = title_freeze_frame();
    if (freeze < 0) return;
    title_routine_ms.store(now_ms());
    {
        const std::uint32_t dl = load32(rdram, 0x800F22B4u);
        const std::uint32_t env0 = 0xFB000000u, env1 = 0x2A200C00u;  // the letters' lift: (42, 32, 12)
        std::memcpy(rdram + (dl & 0x00FFFFFFu), &env0, 4);
        std::memcpy(rdram + ((dl + 4u) & 0x00FFFFFFu), &env1, 4);
        const std::uint32_t next = dl + 8u;
        std::memcpy(rdram + (0x800F22B4u & 0x00FFFFFFu), &next, 4);
    }
    letters_dl_start = load32(rdram, 0x800F22B4u);
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
    float rim = 25.0f;                                // the rim's width at the sides (the original's is thick)
    float rim_vertical = 0.62f;                       // ... and its share at the top and bottom
    float depth = 12.0f;                              // extrusion
    float back_dx = 2.0f, back_dy = -6.0f;            // where the back shows (seen from a little above)
    float tilt = 0.12f;                               // radians: lower on the left, as in the picture
    float offset_y = -4.0f;                           // a little lower than centre, as in the picture
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
        return std::array<float, 3>{x * c - y * sn, x * sn + y * c + st.offset_y, z};
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


// The light from the exposed core (the owner's reading of the original
// picture): a white core tile behind Wheels, a soft glow and a fan of rays
// coming towards the viewer, all kept inside the ring's opening. Drawn as
// translucent shapes over the opening, before the characters.
struct LightVertex { float x, y; float r, g, b, a; };

void draw_title_light(std::uint8_t* rdram, const RingStyle& st) {
    std::uint32_t dl = load32(rdram, display_list_pointer);
    std::uint32_t projection = 0;
    for (std::uint32_t at = dl - 8u, n = 0; n < 4096 && at >= 0x80000000u; at -= 8u, ++n) {
        const std::uint32_t w0 = load32(rdram, at);
        if ((w0 >> 24) == 0x01u && (w0 >> 16 & 0x01u) != 0) { projection = load32(rdram, at + 4u); break; }
    }
    if (projection == 0) return;
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) { put32(rdram, dl, w0); put32(rdram, dl + 4u, w1); dl += 8u; };
    // The exposed core tile, a little right of centre and up (behind Wheels),
    // in the frame's centred units, y up. Its light spreads to the left only,
    // as streaks of lavender and pale blue in a blue-purple haze; the right of
    // the opening stays clear, so the background's squares show there.
    const float lx = 11.0f, ly = -3.0f, hw = 19.0f, hh = 16.0f;  // matched to the original on a grid
    const float ix = st.outer_x - st.rim - 2.0f, iy = st.outer_y - st.rim * st.rim_vertical - 2.0f;
    const float c = std::cos(st.tilt), sn = std::sin(st.tilt);
    const auto edge = [&](float a) {  // a point on the opening's edge
        const float x = ix * std::cos(a), y = iy * std::sin(a);
        return std::array<float, 2>{x * c - y * sn, x * sn + y * c + st.offset_y};
    };
    std::vector<std::array<LightVertex, 3>> tris;
    const auto tri = [&](LightVertex a, LightVertex b, LightVertex d) { tris.push_back({a, b, d}); };
    const float pi = 3.1415927f;
    // A faint shade over the whole opening, so the background reads as behind it.
    for (int k = 0; k < 48; ++k) {
        const auto e0 = edge(2 * pi * k / 48), e1 = edge(2 * pi * (k + 1) / 48);
        // Purple, as the original's blurred squares behind the light.
        tri({0, 0, 0.22f, 0.08f, 0.30f, 0.42f}, {e0[0], e0[1], 0.16f, 0.05f, 0.24f, 0.42f}, {e1[0], e1[1], 0.16f, 0.05f, 0.24f, 0.42f});
    }
    // The tile's corners (tilted a little).
    const float tilt = 0.0f;  // level, as in the original
    const auto corner = [&](float x, float y) {
        return std::array<float, 2>{lx + x * std::cos(tilt) - y * std::sin(tilt), ly + x * std::sin(tilt) + y * std::cos(tilt)};
    };
    const auto tl = corner(-hw, hh), tr = corner(hw, hh), br = corner(hw, -hh), bl = corner(-hw, -hh);
    // Where on the tile a ray to edge angle a starts (the owner's sketch): the
    // upper rays from the top edge, out to the top-right corner; the lower
    // rays from the bottom edge, out to the bottom-right corner; the middle
    // ones from the left edge. The light shines to the left only.
    const auto source = [&](float a) {
        const float s = std::sin(a);  // +1 up, -1 down
        if (s > 0.25f) { const float t = std::min(1.0f, (s - 0.25f) / 0.6f); return std::array<float, 2>{tl[0] + (tr[0] - tl[0]) * t, tl[1] + (tr[1] - tl[1]) * t}; }
        if (s < -0.25f) { const float t = std::min(1.0f, (-s - 0.25f) / 0.6f); return std::array<float, 2>{bl[0] + (br[0] - bl[0]) * t, bl[1] + (br[1] - bl[1]) * t}; }
        const float t = (s + 0.25f) / 0.5f;
        return std::array<float, 2>{bl[0] + (tl[0] - bl[0]) * t, bl[1] + (tl[1] - bl[1]) * t};
    };
    // The haze: quads from the tile's outline to the left half of the opening.
    const int n = 40;
    for (int k = 0; k < n; ++k) {
        const float a0 = pi * 0.42f + pi * 1.16f * k / n, a1 = pi * 0.42f + pi * 1.16f * (k + 1) / n;
        const auto e0 = edge(a0), e1 = edge(a1), s0 = source(a0), s1 = source(a1);
        const float f0 = std::max(0.0f, -std::cos(a0)) * 0.7f + 0.3f, f1 = std::max(0.0f, -std::cos(a1)) * 0.7f + 0.3f;
        // Bluish white near the tile, pale blue further out (the original's left side).
        const LightVertex q0{s0[0], s0[1], 0.78f, 0.86f, 1.0f, 0.60f * f0}, q1{s1[0], s1[1], 0.78f, 0.86f, 1.0f, 0.60f * f1};
        const LightVertex r0{e0[0], e0[1], 0.40f, 0.55f, 0.90f, 0.18f * f0}, r1{e1[0], e1[1], 0.40f, 0.55f, 0.90f, 0.18f * f1};
        tri(q0, r0, r1);
        tri(q0, r1, q1);
    }
    // The streaks: lavender and pale blue, from the tile's outline outwards.
    const float streaks[][4] = {  // angle (degrees, 180 = left), width, which colour, length
        {112, 3.5f, 0, 0.85f}, {126, 3.0f, 1, 0.80f}, {140, 4.0f, 0, 0.95f}, {156, 3.0f, 1, 0.85f}, {172, 4.5f, 0, 1.0f},
        {188, 3.0f, 1, 0.85f}, {204, 4.5f, 0, 0.95f}, {219, 3.0f, 1, 0.80f}, {233, 4.0f, 0, 0.90f}, {248, 3.5f, 1, 0.85f},
    };
    for (const auto& r : streaks) {
        const float a = r[0] * pi / 180.0f, w = r[1] * pi / 180.0f;
        const auto e0 = edge(a - w), e1 = edge(a + w), sa = source(a - w * 0.5f), sb = source(a + w * 0.5f);
        const float len = r[3];
        const float cr = r[2] == 0 ? 0.80f : 0.70f, cg = r[2] == 0 ? 0.58f : 0.86f, cb = 1.0f;
        const LightVertex p0{sa[0], sa[1], 1.0f, 1.0f, 1.0f, 0.75f}, p1{sb[0], sb[1], 1.0f, 1.0f, 1.0f, 0.75f};
        const LightVertex o0{sa[0] + (e0[0] - sa[0]) * len, sa[1] + (e0[1] - sa[1]) * len, cr, cg, cb, 0.0f};
        const LightVertex o1{sb[0] + (e1[0] - sb[0]) * len, sb[1] + (e1[1] - sb[1]) * len, cr, cg, cb, 0.0f};
        tri(p0, o0, o1);
        tri(p0, o1, p1);
    }
    // The white oval glow around the tile (the original's soft round light).
    for (int k = 0; k < 40; ++k) {
        const float a0 = 2 * pi * k / 40, a1 = 2 * pi * (k + 1) / 40;
        const float rx = 34.0f, ry = 29.0f;
        tri({lx, ly, 1, 1, 1, 0.92f}, {lx + rx * std::cos(a0), ly + ry * std::sin(a0), 1, 1, 1, 0.0f},
            {lx + rx * std::cos(a1), ly + ry * std::sin(a1), 1, 1, 1, 0.0f});
        tri({lx, ly, 1, 1, 1, 0.55f}, {lx + 0.6f * rx * std::cos(a0), ly + 0.6f * ry * std::sin(a0), 1, 1, 1, 0.35f},
            {lx + 0.6f * rx * std::cos(a1), ly + 0.6f * ry * std::sin(a1), 1, 1, 1, 0.35f});
    }
    // The tile, blurred: soft bands around it fading out, then the white square.
    for (int ring = 6; ring >= 1; --ring) {
        const float g = 4.5f * ring;
        const auto o_tl = corner(-hw - g, hh + g), o_tr = corner(hw + g, hh + g), o_br = corner(hw + g, -hh - g), o_bl = corner(-hw - g, -hh - g);
        const float al = 0.24f;
        const auto q = [&](std::array<float, 2> a, std::array<float, 2> b, std::array<float, 2> c2, std::array<float, 2> d) {
            tri({a[0], a[1], 0.95f, 0.93f, 1.0f, 0.0f}, {b[0], b[1], 0.95f, 0.93f, 1.0f, 0.0f}, {c2[0], c2[1], 1, 1, 1, al});
            tri({a[0], a[1], 0.95f, 0.93f, 1.0f, 0.0f}, {c2[0], c2[1], 1, 1, 1, al}, {d[0], d[1], 1, 1, 1, al});
        };
        q(o_tl, o_tr, tr, tl);
        q(o_tr, o_br, br, tr);
        q(o_br, o_bl, bl, br);
        q(o_bl, o_tl, tl, bl);
    }
    // The tile itself: bright at the centre, softer towards its edges.
    const LightVertex mid{lx, ly, 1, 1, 1, 1.0f};  // pure white, the glow around it does the softening
    tri(mid, {tl[0], tl[1], 1, 1, 1, 1.0f}, {tr[0], tr[1], 1, 1, 1, 1.0f});
    tri(mid, {tr[0], tr[1], 1, 1, 1, 1.0f}, {br[0], br[1], 1, 1, 1, 1.0f});
    tri(mid, {br[0], br[1], 1, 1, 1, 1.0f}, {bl[0], bl[1], 1, 1, 1, 1.0f});
    tri(mid, {bl[0], bl[1], 1, 1, 1, 1.0f}, {tl[0], tl[1], 1, 1, 1, 1.0f});
    const std::uint32_t proj = 0x807EFE00u, model = 0x807EFE40u;
    const float p[4][4] = {{1.0f / 160.0f, 0, 0, 0}, {0, 1.0f / 120.0f, 0, 0}, {0, 0, 1.0f / 512.0f, 0}, {0, 0, 0, 1}};
    const float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    put_matrix(rdram, proj, p);
    put_matrix(rdram, model, id);
    cmd(0xE7000000u, 0);
    cmd(0xBA001402u, 0x00000000u);
    cmd(0xB900031Du, 0x00504A40u);             // translucent surface, no z
    cmd(0xFCFFFFFFu, 0xFFFE793Cu);             // shade colour and alpha
    cmd(0xBB000000u, 0x00000000u);
    cmd(0xB6000000u, 0xFFFFFFFFu);
    cmd(0xB7000000u, 0x00000204u);
    cmd(0x01030040u, proj);
    cmd(0x01060040u, model);
    std::uint32_t vtx = 0x807E4000u;
    for (std::size_t i = 0; i < tris.size(); i += 5) {
        const std::size_t count = std::min<std::size_t>(5, tris.size() - i);
        const std::uint32_t base = vtx;
        for (std::size_t f = 0; f < count; ++f)
            for (const auto& v : tris[i + f]) {
                put16(rdram, vtx + 0, std::uint16_t(std::int16_t(std::lround(v.x))));
                put16(rdram, vtx + 2, std::uint16_t(std::int16_t(std::lround(v.y))));
                put16(rdram, vtx + 4, 0), put16(rdram, vtx + 6, 0), put16(rdram, vtx + 8, 0), put16(rdram, vtx + 10, 0);
                put8(rdram, vtx + 12, std::uint8_t(std::clamp(v.r, 0.0f, 1.0f) * 255));
                put8(rdram, vtx + 13, std::uint8_t(std::clamp(v.g, 0.0f, 1.0f) * 255));
                put8(rdram, vtx + 14, std::uint8_t(std::clamp(v.b, 0.0f, 1.0f) * 255));
                put8(rdram, vtx + 15, std::uint8_t(std::clamp(v.a, 0.0f, 1.0f) * 255));
                vtx += 16u;
            }
        const std::uint32_t verts = std::uint32_t(count * 3);
        cmd(0x04000000u | ((verts - 1) << 20) | (verts * 16u), base);
        for (std::uint32_t f = 0; f < count; ++f) cmd(0xBF000000u, ((3 * f) * 10u) << 16 | ((3 * f + 1) * 10u) << 8 | ((3 * f + 2) * 10u));
    }
    cmd(0xBD000000u, 0x00000000u);
    cmd(0x01030040u, projection);
    cmd(0xE7000000u, 0);
    put32(rdram, display_list_pointer, dl);
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
    for (std::size_t si = 1; si < strips.size(); ++si) {  // not strip 0, the opening: the background shows through
        const auto& strip = strips[si];
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
    draw_title_light(rdram, style);
}
} // namespace

namespace {
// The title's characters (Turbine, Wheels and the Bomb magic), drawn with the
// game's own scene system: once, the robots' 3D objects and a scene are
// loaded with the game's loaders into memory the game does not use (its
// memory pointer and end, 0x800F22C0 and 0x800FC5AC, are pointed there for
// the duration and put back), the scene is built (func_8002CF70) and then it
// is drawn each title frame (func_8002D93C) before the letters.
constexpr std::uint32_t heap_pointer = 0x800F22C0u, heap_end = 0x800FC5ACu;
constexpr std::uint32_t object_array = 0x8073F000u, size_out = 0x8073F100u;
constexpr std::uint32_t scene_memory = 0x80740000u, scene_memory_end = 0x807DF000u;
std::uint32_t title_scene = 0;

void call4(std::uint8_t* rdram, recomp_context* ctx, void (*fn)(std::uint8_t*, recomp_context*),
           std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3, std::uint32_t stack0 = 0) {
    const recomp_context saved = *ctx;
    ctx->r29 = static_cast<gpr>(static_cast<std::int32_t>(saved.r29) - 0x80);
    std::memcpy(rdram + ((static_cast<std::uint32_t>(ctx->r29) + 0x10u) & 0x00FFFFFFu), &stack0, 4);
    ctx->r4 = static_cast<gpr>(static_cast<std::int32_t>(a0));
    ctx->r5 = static_cast<gpr>(static_cast<std::int32_t>(a1));
    ctx->r6 = static_cast<gpr>(static_cast<std::int32_t>(a2));
    ctx->r7 = static_cast<gpr>(static_cast<std::int32_t>(a3));
    fn(rdram, ctx);
    const auto result = ctx->r2;
    *ctx = saved;
    ctx->r2 = result;
}

// Research: BLOCKPLANET_TITLE_SCENE=scene:object[,scene:object...] builds those
// s3 scenes, each with one s2 object (hex or decimal), and draws them.
struct TitleSceneSpec { int scene, object, frame; float dx = 0, dy = 0, scale = 1; };
std::vector<TitleSceneSpec> title_scene_list() {
    std::vector<TitleSceneSpec> list;
    // The title's robots by default (Turbine on the left, Wheels in the
    // middle, held on a frame where they are unfolded);
    // BLOCKPLANET_TITLE_CHARACTERS=0 leaves them out.
    const char* off = std::getenv("BLOCKPLANET_TITLE_CHARACTERS");
    if (off != nullptr && std::atoi(off) == 0) return list;
    const char* v = std::getenv("BLOCKPLANET_TITLE_SCENE");
    if (v == nullptr) v = "0x1b:0x28@0/-0.345/-0.141/0.92,0xc9:0x27@0/0.508/0.115/0.72";
    std::string text(v);
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t comma = text.find(',', at);
        const std::string item = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        // scene:object[@frame[/dx/dy/scale]]: dx and dy move it on screen
        // (fractions of the half-width and half-height), scale sizes it.
        const std::size_t colon = item.find(':'), at_sign = item.find('@');
        if (colon != std::string::npos) {
            TitleSceneSpec spec{int(std::strtol(item.substr(0, colon).c_str(), nullptr, 0)),
                                int(std::strtol(item.substr(colon + 1, at_sign == std::string::npos ? std::string::npos : at_sign - colon - 1).c_str(), nullptr, 0)),
                                -1};
            if (at_sign != std::string::npos) {
                const char* q = item.c_str() + at_sign + 1;
                char* end = nullptr;
                spec.frame = int(std::strtol(q, &end, 0));
                if (end && *end == '/') spec.dx = std::strtof(end + 1, &end);
                if (end && *end == '/') spec.dy = std::strtof(end + 1, &end);
                if (end && *end == '/') spec.scale = std::strtof(end + 1, &end);
            }
            list.push_back(spec);
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return list;
}
std::vector<std::uint32_t> title_scenes;
std::vector<int> title_scene_frames;  // -1: animate
std::vector<TitleSceneSpec> title_scene_specs;
// The robots' shared texture (main archive entry 0xB1, 44 x 44), which the
// credits put in the global at 0x800E0000 before drawing robot scenes.
std::uint32_t robot_texture = 0;
constexpr std::uint32_t robot_texture_global = 0x800E0000u;

// Each part's colour (material RGB, the first bytes of its record in section
// B of the loaded object). Research: BLOCKPLANET_TITLE_TINT=1 gives every part
// a distinct colour and logs them, to tell the parts apart.

// The robots are drawn without the game's lighting (their colour is the
// part's colour times the vertex colour, white throughout), so they look
// flat. Shading is baked into the vertex colours instead: a normal for each
// vertex from the geometry (the average of its triangles' normals), lit by a
// key light from the upper left and front, in the model's own space.
void compute_normals(std::uint8_t* rdram, std::uint32_t o) {
    const auto u8 = [&](std::uint32_t a) { return std::uint32_t(rdram[(a & 0x00FFFFFFu) ^ 3u]); };
    const auto be16 = [&](std::uint32_t a) { return std::int16_t(u8(a) << 8 | u8(a + 1)); };
    const auto be32 = [&](std::uint32_t a) { return u8(a) << 24 | u8(a + 1) << 16 | u8(a + 2) << 8 | u8(a + 3); };
    if (o < 0x80000000u || be32(o) != 0x18u) return;
    const std::uint32_t b = o + be32(o + 4), c = o + be32(o + 8), v = o + be32(o + 12), e = o + be32(o + 16);
    const std::uint32_t parts = be32(b), count = (e - v) / 16u;
    if (count == 0 || count > 8192) return;
    std::vector<std::array<float, 3>> pos(count), nrm(count, {0, 0, 0});
    for (std::uint32_t i = 0; i < count; ++i)
        pos[i] = {float(be16(v + 16 * i)), float(be16(v + 16 * i + 2)), float(be16(v + 16 * i + 4))};
    for (std::uint32_t part = 0; part < parts && part < 512; ++part) {
        const std::uint32_t blk = std::uint32_t(std::int32_t(c) + be16(c + 2 * part));
        const int batches = be16(blk);
        const std::uint32_t tri = std::uint32_t(std::int32_t(blk) + be16(blk + 2));
        std::array<int, 32> cache{};
        std::uint32_t nibble = 0;
        const auto next = [&]() {
            const std::uint32_t byte = u8(tri + nibble / 2);
            const int n = (nibble % 2 == 0) ? int(byte >> 4) : int(byte & 15);
            ++nibble;
            return n;
        };
        for (int k = 0; k < batches && k < 512; ++k) {
            const std::uint32_t q = blk + 4 + 6u * std::uint32_t(k);
            const int first = be16(q), slot = int(u8(q + 2)), n = int(u8(q + 3)), triangles = int(u8(q + 4));
            for (int i = 0; i < n && slot + i < 32; ++i) cache[std::size_t(slot + i)] = first + i;
            for (int t = 0; t < triangles; ++t) {
                const int i0 = cache[std::size_t(next())], i1 = cache[std::size_t(next())], i2 = cache[std::size_t(next())];
                if (i0 < 0 || i1 < 0 || i2 < 0 || std::uint32_t(std::max({i0, i1, i2})) >= count) continue;
                const auto& a = pos[std::size_t(i0)]; const auto& bb = pos[std::size_t(i1)]; const auto& cc = pos[std::size_t(i2)];
                const float ux = bb[0] - a[0], uy = bb[1] - a[1], uz = bb[2] - a[2], vx = cc[0] - a[0], vy = cc[1] - a[1], vz = cc[2] - a[2];
                const std::array<float, 3> f{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
                for (int idx : {i0, i1, i2})
                    for (int ch = 0; ch < 3; ++ch) nrm[std::size_t(idx)][std::size_t(ch)] += f[std::size_t(ch)];
            }
        }
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& n = nrm[i];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len <= 0.0f) continue;
        const float nx = n[0] / len, ny = n[1] / len, nz = n[2] / len;
        const float key = std::max(0.0f, -0.45f * nx + 0.62f * ny + 0.64f * nz);
        const float rim = std::pow(1.0f - std::fabs(nz), 3.0f) * 0.25f;
        const float shade = std::clamp(0.42f + 0.68f * key + rim, 0.0f, 1.0f);
        for (int ch = 0; ch < 3; ++ch)
            rdram[((v + 16 * i + 12u + std::uint32_t(ch)) & 0x00FFFFFFu) ^ 3u] = std::uint8_t(std::lround(shade * 255.0f));
    }
}

// The robots' colours, chosen from their portraits (main archive DEMO
// pictures): the objects store black for every part, and the game's own
// source of their colours is not found yet. Part numbers found by tinting.
struct PartColour { int first, last; std::uint8_t r, g, b; };
constexpr PartColour turbine_colours[] = {  // s2 0x28 (teal, tentacles)
    {0, 122, 45, 128, 118},     // body
    {35, 66, 34, 100, 96},      // dome
    {37, 42, 196, 206, 202},    // the white band across the face
    {60, 60, 18, 18, 22}, {70, 70, 18, 18, 22},    // pupils
    {71, 72, 240, 240, 234},    // eyes
    {121, 122, 20, 52, 54},     // brow
    {67, 68, 92, 34, 118},      // the glow inside
    {32, 32, 150, 160, 160},    // arm
    {73, 112, 64, 166, 150},    // tentacles
};
constexpr PartColour wheels_colours[] = {  // s2 0x27 (cream and gold, big wheel)
    {0, 80, 226, 186, 136},     // body
    {8, 24, 236, 196, 146},     // face
    {10, 11, 240, 182, 120},
    {19, 19, 244, 244, 238}, {24, 24, 244, 244, 238},  // eyes
    {12, 12, 20, 20, 24}, {14, 14, 20, 20, 24},        // pupils
    {15, 15, 200, 150, 90},     // knob
    {25, 28, 226, 226, 236},    // big wheel
    {26, 26, 122, 104, 184},    // its purple ring
    {42, 43, 150, 160, 190},    // axle
    {44, 58, 214, 150, 82},     // small wheel
    {60, 76, 214, 166, 112},    // shell
};

void tint_parts(std::uint8_t* rdram, std::uint32_t object, int entry) {
    static const bool research = std::getenv("BLOCKPLANET_TITLE_TINT") != nullptr;
    if (object < 0x80000000u) return;
    const PartColour* table = nullptr;
    std::size_t count = 0;
    if (entry == 0x28) table = turbine_colours, count = std::size(turbine_colours);
    if (entry == 0x27) table = wheels_colours, count = std::size(wheels_colours);
    if (table == nullptr && !research) return;
    if (research && entry != 0x28 && entry != 0x27 && table == nullptr) return;
    const auto byte = [&](std::uint32_t a) -> std::uint8_t& { return rdram[(a & 0x00FFFFFFu) ^ 3u]; };
    const auto be32 = [&](std::uint32_t a) {
        return std::uint32_t(byte(a)) << 24 | std::uint32_t(byte(a + 1)) << 16 | std::uint32_t(byte(a + 2)) << 8 | byte(a + 3);
    };
    const std::uint32_t b = object + be32(object + 4);
    const std::uint32_t parts = be32(b);
    // Parts to leave out (flat backdrop cards in the robots' models): their
    // batch count set to 0. BLOCKPLANET_TITLE_HIDE=part,part tries others.
    {
        std::vector<int> hide;
        (void)entry;
        if (const char* h = std::getenv("BLOCKPLANET_TITLE_HIDE")) {
            hide.clear();
            for (const char* q = h; *q;) { char* e = nullptr; hide.push_back(int(std::strtol(q, &e, 0))); q = (*e == ',') ? e + 1 : e; if (e == q && *q != 0) break; }
        }
        const std::uint32_t c = object + be32(object + 8);
        for (int part : hide) {
            if (part < 0 || std::uint32_t(part) >= parts) continue;
            const std::int16_t off = std::int16_t(byte(c + 2u * std::uint32_t(part)) << 8 | byte(c + 2u * std::uint32_t(part) + 1));
            const std::uint32_t blk = std::uint32_t(std::int32_t(c) + off);
            byte(blk) = 0, byte(blk + 1) = 0;
        }
    }
    for (std::uint32_t p = 0; p < parts && p < 512; ++p) {
        const std::uint32_t rec = b + be32(b + 4 + 4 * p);
        if (!research) {
            for (std::size_t k = 0; k < count; ++k)
                if (int(p) >= table[k].first && int(p) <= table[k].last)
                    byte(rec) = table[k].r, byte(rec + 1) = table[k].g, byte(rec + 2) = table[k].b;
            if (entry == 0x27) {
                // Wheels stands in the core's light: his colours washed
                // towards a warm white, as if lit from inside.
                // Golden, as in the original picture (the light passing through him).
                const std::uint8_t gold[3] = {255, 214, 112};
                for (int ch = 0; ch < 3; ++ch) byte(rec + std::uint32_t(ch)) = std::uint8_t((byte(rec + std::uint32_t(ch)) * 35 + gold[ch] * 65) / 100);
            }
            continue;
        }
        // A colour per part, its hue coding its number (2.5 degrees apart), so a
        // screenshot can be read back to tell the parts apart.
        const float h = std::fmod(float(p) * 2.5f, 360.0f) / 60.0f, v = 1.0f;
        const float x = v * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
        float r = 0, g = 0, bl = 0;
        switch (int(h)) { case 0: r = v, g = x; break; case 1: r = x, g = v; break; case 2: g = v, bl = x; break;
                          case 3: g = x, bl = v; break; case 4: r = x, bl = v; break; default: r = v, bl = x; }
        byte(rec) = std::uint8_t(r * 255), byte(rec + 1) = std::uint8_t(g * 255), byte(rec + 2) = std::uint8_t(bl * 255);
    }
}

// Moves and sizes a drawn scene on screen: each projection the scene loaded
// (G_MTX, projection) is replaced by a copy times a clip-space shift and
// scale (x' = s x + dx w, y' = s y + dy w), kept in BlockPlanet's memory.
float matrix_get(std::uint8_t* rdram, std::uint32_t m, int i, int j) {
    const auto half = [&](std::uint32_t a) {
        const std::uint32_t o = (a & 0x00FFFFFFu) ^ 2u;
        return std::uint16_t(rdram[o] | (rdram[o + 1] << 8));
    };
    const int k = 4 * i + j;
    const std::int32_t fixed = std::int32_t(std::uint32_t(half(m + 2u * std::uint32_t(k))) << 16 | half(m + 32u + 2u * std::uint32_t(k)));
    return float(fixed) / 65536.0f;
}

void place_scene(std::uint8_t* rdram, std::uint32_t from, std::uint32_t to, const TitleSceneSpec& spec, std::size_t slot) {
    if (spec.dx == 0 && spec.dy == 0 && spec.scale == 1) return;
    int n = 0;
    for (std::uint32_t a = from; a + 8u <= to && a < from + 8u * 8192u; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a);
        if ((w0 >> 24) != 0x01u || ((w0 >> 16) & 0x01u) == 0) continue;  // a projection matrix
        const std::uint32_t m = load32(rdram, a + 4u) | 0x80000000u;
        float p[4][4], q[4][4];
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) p[i][j] = matrix_get(rdram, m, i, j);
        for (int i = 0; i < 4; ++i) {
            q[i][0] = p[i][0] * spec.scale + p[i][3] * spec.dx;
            q[i][1] = p[i][1] * spec.scale + p[i][3] * spec.dy;
            q[i][2] = p[i][2];
            q[i][3] = p[i][3];
        }
        const std::uint32_t out = 0x807EC000u + 0x40u * std::uint32_t(slot * 4 + std::size_t(n % 4));
        put_matrix(rdram, out, q);
        put32(rdram, a + 4u, out);
        ++n;
    }
}


// The title's Bombs (the Bomb magic's missile, s2 0x5E): its scenes place it
// only in play, so BlockPlanet draws the game's own mesh itself, like the
// ring: decoded from the loaded object (vertices, parts, material colours,
// nibble-packed triangles; docs/research/blockplanet-assets.md), lit with one
// light from the upper left, sorted back to front, posed as in the original
// picture (three missiles on the right, pointing up and to the left).
std::uint32_t bomb_object = 0;

struct MeshTriangle { std::array<std::array<float, 3>, 3> p; std::array<float, 3> colour; };

std::vector<MeshTriangle> decode_object(std::uint8_t* rdram, std::uint32_t o) {
    std::vector<MeshTriangle> out;
    const auto u8 = [&](std::uint32_t a) { return std::uint32_t(rdram[(a & 0x00FFFFFFu) ^ 3u]); };
    const auto be16 = [&](std::uint32_t a) { return std::int16_t(u8(a) << 8 | u8(a + 1)); };
    const auto be32 = [&](std::uint32_t a) { return u8(a) << 24 | u8(a + 1) << 16 | u8(a + 2) << 8 | u8(a + 3); };
    if (o < 0x80000000u || be32(o) != 0x18u) return out;
    const std::uint32_t b = o + be32(o + 4), c = o + be32(o + 8), v = o + be32(o + 12), e = o + be32(o + 16);
    const std::uint32_t parts = be32(b);
    const std::uint32_t vertex_count = (e - v) / 16u;
    for (std::uint32_t part = 0; part < parts && part < 256; ++part) {
        const std::uint32_t rec = b + be32(b + 4 + 4 * part);
        std::array<float, 3> colour{u8(rec) / 255.0f, u8(rec + 1) / 255.0f, u8(rec + 2) / 255.0f};
        // The missile as in the original picture: no flat cards or flame (parts
        // 0 to 2), and its front disc silver, not red (part 6).
        if (part <= 2) continue;
        if (part == 6) colour = {0.80f, 0.80f, 0.82f};
        const std::uint32_t blk = std::uint32_t(std::int32_t(c) + be16(c + 2 * part));
        const int batches = be16(blk);
        const std::uint32_t tri = std::uint32_t(std::int32_t(blk) + be16(blk + 2));
        std::array<int, 32> cache{};
        std::uint32_t nibble = 0;
        const auto next = [&]() {
            const std::uint32_t byte = u8(tri + nibble / 2);
            const int n = (nibble % 2 == 0) ? int(byte >> 4) : int(byte & 15);
            ++nibble;
            return n;
        };
        for (int k = 0; k < batches && k < 512; ++k) {
            const std::uint32_t q = blk + 4 + 6u * std::uint32_t(k);
            const int first = be16(q), slot = int(u8(q + 2)), count = int(u8(q + 3)), triangles = int(u8(q + 4));
            for (int i = 0; i < count && slot + i < 32; ++i) cache[std::size_t(slot + i)] = first + i;
            for (int t = 0; t < triangles; ++t) {
                MeshTriangle m;
                bool ok = true;
                for (int corner = 0; corner < 3; ++corner) {
                    const int index = cache[std::size_t(next())];
                    if (index < 0 || std::uint32_t(index) >= vertex_count) { ok = false; continue; }
                    const std::uint32_t a = v + 16u * std::uint32_t(index);
                    m.p[std::size_t(corner)] = {float(be16(a)), float(be16(a + 2)), float(be16(a + 4))};
                }
                m.colour = colour;
                if (ok) out.push_back(m);
            }
        }
    }
    return out;
}

struct BombPose { float x, y, size, yaw, pitch, roll; };
// Positions in the 320 x 240 frame's centred units (y down), matched to the
// original picture on a grid: noses to the left, fins on the right.
constexpr BombPose bomb_poses[] = {
    {62.0f, -12.0f, 0.0055f, 5.04f, -0.25f, 0.3f},
    {68.0f, 19.0f, 0.0085f, 5.04f, -0.25f, 0.3f},
    {40.0f, 54.0f, 0.0125f, 5.04f, -0.25f, 0.3f},
};

void draw_title_bombs(std::uint8_t* rdram) {
    static std::vector<MeshTriangle> mesh;
    static std::uint32_t decoded_from = 0;
    if (bomb_object == 0) return;
    if (decoded_from != bomb_object) mesh = decode_object(rdram, bomb_object), decoded_from = bomb_object;
    if (mesh.empty()) return;
    struct Face { float depth; std::array<std::array<float, 3>, 3> p; std::array<float, 3> colour; };
    std::vector<Face> faces;
    for (const auto& pose : bomb_poses) {
        const float cy = std::cos(pose.yaw), sy = std::sin(pose.yaw), cp = std::cos(pose.pitch), sp = std::sin(pose.pitch),
                    cr = std::cos(pose.roll), sr = std::sin(pose.roll);
        for (const auto& t : mesh) {
            Face f;
            for (int k = 0; k < 3; ++k) {
                auto [x, y, z] = t.p[std::size_t(k)];
                float x1 = x * cy + z * sy, z1 = -x * sy + z * cy;           // yaw
                float y1 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;          // pitch
                float x2 = x1 * cr - y1 * sr, y2 = x1 * sr + y1 * cr;        // roll
                f.p[std::size_t(k)] = {pose.x + x2 * pose.size, pose.y + y2 * pose.size, z2 * pose.size};
            }
            const auto& a = f.p[0]; const auto& b = f.p[1]; const auto& c = f.p[2];
            const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
            float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 0.0f) continue;
            nx /= len, ny /= len, nz /= len;
            const float light = std::fabs(-0.5f * nx + 0.6f * ny + 0.62f * nz);
            const float shade = 0.38f + 0.62f * light;
            f.colour = {t.colour[0] * shade, t.colour[1] * shade, t.colour[2] * shade};
            f.depth = (a[2] + b[2] + c[2]) / 3.0f;
            faces.push_back(f);
        }
    }
    std::sort(faces.begin(), faces.end(), [](const Face& l, const Face& r) { return l.depth < r.depth; });
    std::uint32_t dl = load32(rdram, display_list_pointer);
    std::uint32_t projection = 0;
    for (std::uint32_t at = dl - 8u, n = 0; n < 8192 && at >= 0x80000000u; at -= 8u, ++n) {
        const std::uint32_t w0 = load32(rdram, at);
        if ((w0 >> 24) == 0x01u && (w0 >> 16 & 0x01u) != 0) { projection = load32(rdram, at + 4u); break; }
    }
    if (projection == 0) return;
    const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) { put32(rdram, dl, w0); put32(rdram, dl + 4u, w1); dl += 8u; };
    const std::uint32_t proj = 0x807EFF00u, model = 0x807EFF40u;
    const float p[4][4] = {{1.0f / 160.0f, 0, 0, 0}, {0, 1.0f / 120.0f, 0, 0}, {0, 0, 1.0f / 512.0f, 0}, {0, 0, 0, 1}};
    const float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    put_matrix(rdram, proj, p);
    put_matrix(rdram, model, id);
    cmd(0xE7000000u, 0);
    cmd(0xBA001402u, 0x00000000u);
    cmd(0xB900031Du, 0x0F0A4000u);
    cmd(0xFCFFFFFFu, 0xFFFE793Cu);
    cmd(0xBB000000u, 0x00000000u);
    cmd(0xB6000000u, 0xFFFFFFFFu);
    cmd(0xB7000000u, 0x00000004u);  // shade, flat look (each face its own colour)
    cmd(0x01030040u, proj);
    cmd(0x01060040u, model);
    std::uint32_t vtx = 0x807F0000u;
    const std::uint32_t vtx_end = 0x80800000u;
    for (std::size_t i = 0; i < faces.size(); i += 5) {
        const std::size_t n = std::min<std::size_t>(5, faces.size() - i);
        if (vtx + 48u * std::uint32_t(n) > vtx_end) break;
        const std::uint32_t base = vtx;
        for (std::size_t f = 0; f < n; ++f)
            for (int k = 0; k < 3; ++k) {
                const auto& q = faces[i + f].p[std::size_t(k)];
                const std::uint32_t a = vtx;
                put16(rdram, a + 0, std::uint16_t(std::int16_t(std::lround(q[0]))));
                put16(rdram, a + 2, std::uint16_t(std::int16_t(std::lround(-q[1]))));
                put16(rdram, a + 4, std::uint16_t(std::int16_t(std::lround(q[2]))));
                put16(rdram, a + 6, 0), put16(rdram, a + 8, 0), put16(rdram, a + 10, 0);
                for (int ch = 0; ch < 3; ++ch)
                    put8(rdram, a + 12u + std::uint32_t(ch), std::uint8_t(std::clamp(faces[i + f].colour[std::size_t(ch)], 0.0f, 1.0f) * 255.0f));
                put8(rdram, a + 15u, 0xFF);
                vtx += 16u;
            }
        const std::uint32_t count = std::uint32_t(n * 3);
        cmd(0x04000000u | ((count - 1) << 20) | (count * 16u), base);
        for (std::uint32_t f = 0; f < n; ++f)
            cmd(0xBF000000u, ((3 * f) * 10u) << 16 | ((3 * f + 1) * 10u) << 8 | ((3 * f + 2) * 10u));
    }
    cmd(0xBD000000u, 0x00000000u);
    cmd(0x01030040u, projection);
    cmd(0xE7000000u, 0);
    put32(rdram, display_list_pointer, dl);
}

// The robots' lighting on the title: the scenes' own lights are mostly
// ambient, which looks flat, so the lights they load (G_MOVEMEM light 1 and
// the ambient after it) are rewritten: a strong warm-white key light from the
// upper left and front, and a low ambient. Takes effect from the next frame.
void relight_scene(std::uint8_t* rdram, std::uint32_t from, std::uint32_t to) {
    const auto byte = [&](std::uint32_t a, std::uint8_t v) { rdram[(a & 0x00FFFFFFu) ^ 3u] = v; };
    // The number of directional lights (G_MOVEWORD numlights: 0x80000000 +
    // 32 x (lights + 1)); the ambient follows the last.
    std::uint32_t lights = 1;
    for (std::uint32_t a = from; a + 8u <= to && a < from + 8u * 8192u; a += 8u)
        if (load32(rdram, a) == 0xBC000002u) lights = std::max(1u, ((load32(rdram, a + 4u) & 0x7FFFFFFFu) / 32u) - 1u);
    const std::uint32_t ambient_slot = 0x86u + 2u * lights;
    for (std::uint32_t a = from; a + 8u <= to && a < from + 8u * 8192u; a += 8u) {
        const std::uint32_t w0 = load32(rdram, a);
        if ((w0 >> 24) != 0x03u) continue;
        const std::uint32_t which = (w0 >> 16) & 0xFFu, at = load32(rdram, a + 4u) | 0x80000000u;
        if (which > 0x86u && which < ambient_slot) {  // the other lights: a faint cool fill from below right
            const std::uint8_t c[3] = {40, 46, 70};
            for (int k = 0; k < 3; ++k) byte(at + std::uint32_t(k), c[k]), byte(at + 4u + std::uint32_t(k), c[k]);
            byte(at + 8, std::uint8_t(std::int8_t(70))), byte(at + 9, std::uint8_t(std::int8_t(-60))), byte(at + 10, std::uint8_t(std::int8_t(60)));
            continue;
        }
        if (which == 0x86u) {  // light 1: colour, copy, direction (towards the light)
            const std::uint8_t c[3] = {236, 232, 220};
            for (int k = 0; k < 3; ++k) byte(at + std::uint32_t(k), c[k]), byte(at + 4u + std::uint32_t(k), c[k]);
            byte(at + 8, std::uint8_t(std::int8_t(-58))), byte(at + 9, std::uint8_t(std::int8_t(72))), byte(at + 10, std::uint8_t(std::int8_t(84)));
        } else if (which == ambient_slot) {  // the ambient
            const std::uint8_t c[3] = {64, 64, 72};
            for (int k = 0; k < 3; ++k) byte(at + std::uint32_t(k), c[k]), byte(at + 4u + std::uint32_t(k), c[k]);
        }
    }
}

void build_title_scene(std::uint8_t* rdram, recomp_context* ctx) {
    // Built in BlockPlanet's own memory above 4 MB (the scene code's 4 MB
    // checks are raised to 8 MB in the recompiler config), with the game's
    // memory pointer and end pointed there for the duration. Built again
    // whenever the title comes back (not drawn for a moment), as before.
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const bool returned = now - last > std::chrono::milliseconds(500);
    last = now;
    if (!returned) return;
    title_scenes.clear();
    robot_texture = 0;
    bomb_object = 0;
    const auto list = title_scene_list();
    if (list.empty()) return;
    const std::uint32_t saved_pointer = load32(rdram, heap_pointer), saved_end = load32(rdram, heap_end);
    put32(rdram, heap_pointer, scene_memory);
    put32(rdram, heap_end, scene_memory_end);
    call4(rdram, ctx, func_8002A100, 0x5Eu, 0, 0, 0);  // the Bomb's missile
    bomb_object = static_cast<std::uint32_t>(ctx->r2);
    call4(rdram, ctx, func_8002A8B4, 0x736C50u, 0xB1u, 8u, 0);
    robot_texture = static_cast<std::uint32_t>(ctx->r2);
    std::uint32_t slot = 0;
    title_scene_frames.clear();
    title_scene_specs.clear();
    for (const auto& spec : list) {
        const int scene_entry = spec.scene, object_entry = spec.object;
        call4(rdram, ctx, func_8002A100, std::uint32_t(object_entry), 0, 0, 0);
        const std::uint32_t object = static_cast<std::uint32_t>(ctx->r2);
        tint_parts(rdram, object, object_entry);
        compute_normals(rdram, object);
        const std::uint32_t object_slot = object_array + 4u * slot++;
        put32(rdram, object_slot, object);
        call4(rdram, ctx, func_8002A0C0, std::uint32_t(scene_entry), 0, 0, 0);
        const std::uint32_t scene_data = static_cast<std::uint32_t>(ctx->r2);
        const std::uint32_t at = (load32(rdram, heap_pointer) + 15u) & ~15u;
        if (object == 0 || scene_data == 0 || load32(rdram, heap_end) < at + 0x10000u) break;  // not enough room
        put32(rdram, heap_pointer, at);
        put32(rdram, size_out, 0);
        call4(rdram, ctx, func_8002CF70, at, scene_data, object_slot, 1, size_out);
        const std::uint32_t built = static_cast<std::uint32_t>(ctx->r2);
        put32(rdram, heap_pointer, at + load32(rdram, size_out));
        title_scenes.push_back(built);
        title_scene_frames.push_back(spec.frame);
        title_scene_specs.push_back(spec);
        std::fprintf(stdout, "{\"event\":\"title_scene\",\"scene\":%d,\"object\":%d,\"built\":\"0x%08X\",\"size\":%u}\n",
                     scene_entry, object_entry, built, load32(rdram, size_out));
        std::fflush(stdout);
    }
    put32(rdram, heap_pointer, saved_pointer);
    put32(rdram, heap_end, saved_end);
}

void draw_title_scene(std::uint8_t* rdram, recomp_context* ctx) {
    const std::uint32_t elapsed = load32(rdram, 0x800E1208u) - load32(rdram, 0x800E1210u);
    if (title_scenes.empty()) return;
    // Lights for the scene (the credits set their own; the title has none):
    // one white key light from the upper left and front, and a soft ambient.
    {
        constexpr std::uint32_t lights = 0x807E8000u;  // clear of the ring's vertices (0x807E0000 on)
        const auto byte = [&](std::uint32_t a, std::uint8_t v) { rdram[(a & 0x00FFFFFFu) ^ 3u] = v; };
        for (int k = 0; k < 3; ++k) {
            byte(lights + std::uint32_t(k), 0xE0);        // light colour
            byte(lights + 4u + std::uint32_t(k), 0xE0);   // (copy)
            byte(lights + 16u + std::uint32_t(k), 0x50);  // ambient
            byte(lights + 20u + std::uint32_t(k), 0x50);
        }
        byte(lights + 8, 0xC4);   // direction (x, y, z), signed
        byte(lights + 9, 0x40);
        byte(lights + 10, 0x60);
        std::uint32_t dl = load32(rdram, display_list_pointer);
        const auto cmd = [&](std::uint32_t w0, std::uint32_t w1) {
            put32(rdram, dl, w0);
            put32(rdram, dl + 4u, w1);
            dl += 8u;
        };
        cmd(0xBC000002u, 0x80000040u);  // one light
        cmd(0x03860010u, lights);       // light 1
        cmd(0x03880010u, lights + 16u); // ambient
        put32(rdram, display_list_pointer, dl);
    }
    const std::uint32_t saved = load32(rdram, robot_texture_global);
    if (robot_texture != 0) put32(rdram, robot_texture_global, robot_texture);
    {
        // Only inside the ring's area: parts of a scene that sit elsewhere in
        // its first frame are cut off (G_SETSCISSOR, in quarter pixels).
        std::uint32_t dl = load32(rdram, display_list_pointer);
        put32(rdram, dl, 0xED000000u | (0u << 12) | (4u * 40u));
        put32(rdram, dl + 4u, (4u * 320u) << 12 | (4u * 222u));
        put32(rdram, display_list_pointer, dl + 8u);
    }
    const std::uint32_t dl_before = load32(rdram, display_list_pointer);
    for (std::size_t k = 0; k < title_scenes.size(); ++k) {
        const std::uint32_t scene = title_scenes[k];
        if (scene < 0x80000000u || scene >= 0x80800000u) continue;
        const int frame = k < title_scene_frames.size() ? title_scene_frames[k] : -1;
        if (frame >= 0) put32(rdram, scene + 0x24u, std::uint32_t(frame));  // held on one frame
        const std::uint32_t from = load32(rdram, display_list_pointer);
        call4(rdram, ctx, func_8002D93C, scene, frame >= 0 ? 0u : elapsed, 0, 0);
        if (k < title_scene_specs.size()) place_scene(rdram, from, load32(rdram, display_list_pointer), title_scene_specs[k], k);
        relight_scene(rdram, from, load32(rdram, display_list_pointer));
    }
    // Research: BLOCKPLANET_TITLE_DL_LOG=1 logs the scene's commands once.
    static bool logged = std::getenv("BLOCKPLANET_TITLE_DL_LOG") == nullptr;
    if (!logged) {
        logged = true;
        const std::uint32_t dl_after = load32(rdram, display_list_pointer);
        std::fprintf(stdout, "{\"event\":\"title_dl\",\"commands\":%u}\n", (dl_after - dl_before) / 8u);
        for (const std::uint32_t scene : title_scenes) {
            const std::uint32_t objects = load32(rdram, scene + 0x14u);
            const int count = static_cast<int>(load32(rdram, scene + 0x10u));
            std::fprintf(stdout, "SCENE %08X objects %08X count %d:", scene, objects, count);
            for (int k = 0; k < 40; ++k)
                std::fprintf(stdout, " %02X", rdram[((objects + 0x120u * std::uint32_t(k) + 0x80u) & 0x00FFFFFFu) ^ 3u]);
            std::fprintf(stdout, "\n");
        }
        for (std::uint32_t a = dl_before; a < dl_after && a < dl_before + 8u * 400u; a += 8u)
            std::fprintf(stdout, "DL %08X %08X\n", load32(rdram, a), load32(rdram, a + 4u));
        std::fflush(stdout);
    }
    put32(rdram, robot_texture_global, saved);
    {
        std::uint32_t dl = load32(rdram, display_list_pointer);
        put32(rdram, dl, 0xED000000u);
        put32(rdram, dl + 4u, (4u * 320u) << 12 | (4u * 240u));
        put32(rdram, display_list_pointer, dl + 8u);
    }
}
} // namespace

// With the letters held (BLOCKPLANET_TITLE_FREEZE), the title picture is drawn
// fully opaque (its alpha argument, on the stack at +0x1C).
extern "C" void blockplanet_title_picture(std::uint8_t* rdram, recomp_context* ctx) {
    if (title_freeze_frame() < 0) return;
    static const bool ring = std::getenv("BLOCKPLANET_TITLE_RING") == nullptr || std::atoi(std::getenv("BLOCKPLANET_TITLE_RING")) != 0;
    if (ring) draw_title_ring(rdram);
    build_title_scene(rdram, ctx);
    draw_title_scene(rdram, ctx);
    draw_title_bombs(rdram);
    const std::uint32_t alpha = 0xFF;
    std::memcpy(rdram + ((static_cast<std::uint32_t>(ctx->r29) + 0x1C) & 0x00FFFFFFu), &alpha, 4);
}
