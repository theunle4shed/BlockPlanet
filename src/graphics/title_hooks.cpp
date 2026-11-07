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

// With the letters held (BLOCKPLANET_TITLE_FREEZE), the title picture is drawn
// fully opaque (its alpha argument, on the stack at +0x1C).
extern "C" void blockplanet_title_picture(std::uint8_t* rdram, recomp_context* ctx) {
    if (title_freeze_frame() < 0) return;
    const std::uint32_t alpha = 0xFF;
    std::memcpy(rdram + ((static_cast<std::uint32_t>(ctx->r29) + 0x1C) & 0x00FFFFFFu), &alpha, 4);
}
