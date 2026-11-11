// The N64 logo opens with "NINTENDO64" already in place
// (docs/research/blockplanet-game-state.md, "The N64 logo's animation").
//
// The logo is one LightWave scene (pointer at 0x800E166C, built by the intro
// func_80067798 and played by func_8002D93C): object 0 the N, 1 the red "64",
// 2 the NINTENDO wordmark, 3 the shadow. Its keyframes are stepped one frame
// at a time for the whole scene (func_8002F55C), so one object cannot simply
// be started later in its animation. Instead, at the logo's first frame a hook
// (func_8002D93C at 0x8002DD80, after the step and before the objects'
// matrices are built) saves the scene's memory, steps it on to the frame where
// the "64" and the wordmark have settled, keeps those two objects' state and
// puts the scene back. Until the live animation reaches that frame, their
// settled state is written over the live one each frame; from there on
// everything plays as the original. BLOCKPLANET_INTRO_LOGO=1 keeps the original
// animation; BLOCKPLANET_INTRO_LOGO_HOLD=frame changes the frame.

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

extern "C" void func_8002F55C(std::uint8_t* rdram, recomp_context* ctx);

namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t logo_scene_pointer = 0x800E166Cu;
constexpr std::uint32_t intro_scene_pointer = 0x800E1670u;  // built right after the logo's
constexpr std::uint32_t object_size = 0x120u;
// Each record starts with its two matrices (double-buffered: the renderer may
// still be reading one), then the keyframe state, which is what is held.
constexpr std::uint32_t state_offset = 0x80u;
constexpr std::uint32_t state_size = object_size - state_offset;
// The "64" and the wordmark.
const std::vector<int>& held_list() {
    static const std::vector<int> list{1, 2};
    return list;
}

int hold_frame() {
    static const int frame = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_LOGO_HOLD");
        return v ? std::atoi(v) : 610;  // the "64" settles by 600; the wordmark has faded in
    }();
    return frame;
}

// Where the logo starts (the owner's request, 2026-11-10): frame 540 (60 a
// second), with the N already centred and spinning; its walk-in (frames 64 to
// about 190) and most of its spin are skipped. The whole logo then shows for
// 1.75 s, until the wordmark fades and the camera pulls back (640 to 665),
// and the intro's scenes take over at 690: 2.5 s in all, against about 10.5.
// BLOCKPLANET_INTRO_LOGO_START=frame changes it (0: the full length).
int start_frame() {
    static const int frame = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_LOGO_START");
        return v ? std::atoi(v) : 540;
    }();
    return frame;
}

bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_LOGO");
        return v == nullptr || v[0] != '1';
    }();
    return on;
}

std::uint32_t prepared_scene = 0;
int last_frame = 0;  // a frame lower than the last: the logo started again
std::vector<std::uint8_t> held;  // the held objects' records, one after the other

void step(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t scene) {
    const recomp_context saved = *ctx;
    ctx->r4 = static_cast<gpr>(static_cast<std::int64_t>(static_cast<std::int32_t>(scene)));
    func_8002F55C(rdram, ctx);
    *ctx = saved;
}

} // namespace

namespace {
void logo_scene(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t scene, bool hold);
}

extern "C" void blockplanet_logo_scene_stepped(std::uint8_t* rdram, recomp_context* ctx) {
    logo_scene(rdram, ctx, static_cast<std::uint32_t>(ctx->r16), true);
}

// Before each of the logo's draws (the draw filter): the set-up and the short
// start only. The hook after the step is not reached on the scene's first
// draws, which showed the N alone; the held state is written only after the
// step (written before it, the step adds its own increments to it).
extern "C" void blockplanet_logo_scene_before(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t scene) {
    logo_scene(rdram, ctx, scene, false);
}

namespace {
void logo_scene(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t scene, bool hold) {
    if (!enabled()) return;
    if (scene == 0 || scene != load32(rdram, logo_scene_pointer) || load16(rdram, 0x800E1648u) != 0) return;
    const std::uint32_t frame_address = scene + 0x24u;
    const int frame = static_cast<int>(load32(rdram, frame_address));
    const std::uint32_t objects = load32(rdram, scene + 0x14u);
    const bool restarted = frame < last_frame;
    last_frame = frame;
    if (prepared_scene != scene || restarted) {
        prepared_scene = 0;
        held.clear();
        const std::uint32_t end = load32(rdram, intro_scene_pointer);
        if (end <= scene || end - scene > 0x100000u || objects < scene || objects + 6 * object_size > end) return;
        if (frame >= hold_frame()) return;
        // Save the scene, step it on to the hold frame, keep the two objects.
        std::vector<std::uint8_t> saved(host(rdram, scene), host(rdram, end));
        while (static_cast<int>(load32(rdram, frame_address)) < hold_frame()) {
            step(rdram, ctx, scene);
            store32(rdram, frame_address, load32(rdram, frame_address) + 1u);
        }
        for (const int object : held_list()) {
            const std::uint8_t* record = host(rdram, objects + std::uint32_t(object) * object_size + state_offset);
            held.insert(held.end(), record, record + state_size);
        }
        std::memcpy(host(rdram, scene), saved.data(), saved.size());
        prepared_scene = scene;
        // Shorter: carry the live scene on to the start frame.
        while (static_cast<int>(load32(rdram, frame_address)) < start_frame()) {
            step(rdram, ctx, scene);
            store32(rdram, frame_address, load32(rdram, frame_address) + 1u);
        }
        std::fprintf(stdout, "{\"event\":\"intro_logo_held\",\"scene\":\"0x%08X\",\"from_frame\":%d,\"start_frame\":%d,\"hold_frame\":%d,\"end_frame\":%d}\n",
                     scene, frame, start_frame(), hold_frame(), static_cast<int>(load32(rdram, load32(rdram, scene + 0x1Cu) + 0xCu)));
        std::fflush(stdout);
    }
    if (!hold || frame >= hold_frame() || held.size() != held_list().size() * state_size) return;
    for (std::size_t i = 0; i < held_list().size(); ++i)
        std::memcpy(host(rdram, objects + std::uint32_t(held_list()[i]) * object_size + state_offset), held.data() + i * state_size, state_size);
}
} // namespace

// Straight on (the owner's request, 2026-11-10): the settled wordmark is
// pitched 11 degrees to the camera and the "64" turned about 14 degrees, so
// the edges of their extruded sides show. When the per-object draw
// (func_800321D4) reaches either of them, their matrix (drawn after the
// camera's, scene +0xF8, both chosen by the buffer at scene +0x3D) is turned
// upright to the camera, the wordmark parallel to the screen and the "64"
// with its front (local +Z) straight at the eye (see below).
// Position, size, colour and fade are unchanged (thinning the depth as well
// was tried and darkens the "64": it bends the lighting normals).
// BLOCKPLANET_INTRO_LOGO_FACE=0 keeps their own rotation.
namespace {

struct Matrix { double m[4][4]; };
struct Vec { double x, y, z; };

Vec cross(const Vec& a, const Vec& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec normalised(const Vec& v) {
    const double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0 ? Vec{v.x / l, v.y / l, v.z / l} : v;
}

Matrix read_matrix(const std::uint8_t* rdram, std::uint32_t at) {
    Matrix r{};
    for (int k = 0; k < 16; ++k) {
        const std::int32_t fixed = static_cast<std::int32_t>(
            (std::uint32_t(std::uint16_t(load16(rdram, at + 2u * k))) << 16) | std::uint16_t(load16(rdram, at + 32u + 2u * k)));
        r.m[k / 4][k % 4] = fixed / 65536.0;
    }
    return r;
}

void write_matrix(std::uint8_t* rdram, std::uint32_t at, const Matrix& r) {
    for (int k = 0; k < 16; ++k) {
        const std::int32_t fixed = static_cast<std::int32_t>(std::llround(r.m[k / 4][k % 4] * 65536.0));
        store16(rdram, at + 2u * k, static_cast<std::int16_t>(std::uint16_t(std::uint32_t(fixed) >> 16)));
        store16(rdram, at + 32u + 2u * k, static_cast<std::int16_t>(std::uint16_t(std::uint32_t(fixed) & 0xFFFFu)));
    }
}

bool face_enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_LOGO_FACE");
        return v == nullptr || v[0] != '0';
    }();
    return on;
}


// The N keeps turning (the owner's notes, 2026-11-10): its spin about its
// own vertical axis (about 1.3 degrees a frame at the end) stops dead at
// frame 657, as the wordmark fades, which looks abrupt. From the frame its
// turn stops, it goes on turning at its last rate until the logo leaves.
// The stop is found from the N's own matrix (its first row's angle about
// the vertical), so it holds wherever the logo starts.
// BLOCKPLANET_INTRO_LOGO_SPIN=0 keeps the original stop.
void keep_spinning(std::uint8_t* rdram, std::uint32_t scene) {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_LOGO_SPIN");
        return v == nullptr || v[0] != '0';
    }();
    if (!enabled() || !on) return;
    if (scene == 0 || scene != load32(rdram, logo_scene_pointer) || load16(rdram, 0x800E1648u) != 0) return;
    static std::uint32_t seen_scene = 0;
    static int last_frame = -1, end_frame = -1;
    static double last_angle = 0, rate = 0, end_angle = 0;
    const int frame = static_cast<int>(load32(rdram, scene + 0x24u));
    if (scene != seen_scene || frame < last_frame) {  // a new logo
        seen_scene = scene, last_frame = -1, end_frame = -1, rate = 0;
    }
    const std::uint32_t buffer = load8(rdram, scene + 0x3Du) & 1u;
    const std::uint32_t at = load32(rdram, scene + 0x14u) + buffer * 0x40u;
    Matrix m = read_matrix(rdram, at);
    const double angle = std::atan2(m.m[0][2], m.m[0][0]);
    if (end_frame < 0) {
        if (last_frame >= 0 && frame > last_frame) {
            double step = angle - last_angle;
            constexpr double pi = 3.14159265358979323846;  // (MSVC has no M_PI by default)
            if (step > pi) step -= 2 * pi;
            if (step < -pi) step += 2 * pi;
            step /= frame - last_frame;
            if (std::fabs(step) < 1e-4 && std::fabs(rate) > 1e-3)
                end_frame = last_frame, end_angle = last_angle;  // the turn has stopped
            else
                rate = step;
        }
        if (frame != last_frame) last_frame = frame, last_angle = angle;
        if (end_frame < 0) return;
    }
    last_frame = frame;
    // Turned on about its own vertical (row vectors: rows 0 and 2 mixed).
    const double turn = end_angle + rate * (frame - end_frame) - angle;
    const double c = std::cos(turn), s = std::sin(turn);
    Matrix r = m;
    for (int j = 0; j < 3; ++j) {
        r.m[0][j] = c * m.m[0][j] + s * m.m[2][j];
        r.m[2][j] = -s * m.m[0][j] + c * m.m[2][j];
    }
    write_matrix(rdram, at, r);
}
} // namespace

extern "C" void blockplanet_logo_object_draw(std::uint8_t* rdram, std::uint32_t scene, int object) {
    if (object == 0) {
        keep_spinning(rdram, scene);
        return;
    }
    if (!enabled() || !face_enabled() || (object != 1 && object != 2)) return;
    if (scene == 0 || scene != load32(rdram, logo_scene_pointer) || load16(rdram, 0x800E1648u) != 0) return;
    const std::uint32_t buffer = load8(rdram, scene + 0x3Du) & 1u;
    const Matrix c = read_matrix(rdram, scene + 0xF8u + buffer * 0x40u);
    const std::uint32_t at = load32(rdram, scene + 0x14u) + std::uint32_t(object) * object_size + buffer * 0x40u;
    Matrix m = read_matrix(rdram, at);
    // Row vectors: drawn = local * m * c. The object's place seen from the
    // eye (at the origin of the camera's space, looking down -Z):
    const Vec p{m.m[3][0] * c.m[0][0] + m.m[3][1] * c.m[1][0] + m.m[3][2] * c.m[2][0] + c.m[3][0],
                m.m[3][0] * c.m[0][1] + m.m[3][1] * c.m[1][1] + m.m[3][2] * c.m[2][1] + c.m[3][1],
                m.m[3][0] * c.m[0][2] + m.m[3][1] * c.m[1][2] + m.m[3][2] * c.m[2][2] + c.m[3][2]};
    // Its new axes in the camera's space, upright. The wordmark (wide) lies
    // parallel to the screen: facing the eye would keystone it (its first N
    // leans), and its depth is too thin for its sides to show. The "64"
    // (small, deeper) faces the eye, so perspective shows none of its sides.
    const Vec z = object == 2 ? Vec{0.0, 0.0, 1.0} : normalised({-p.x, -p.y, -p.z});
    const Vec x = normalised(cross({0.0, 1.0, 0.0}, z));
    const Vec y = cross(z, x);
    const Vec axes[3] = {x, y, z};
    // Back into the scene's space (the camera's rotation is orthonormal, so
    // its inverse is its transpose), keeping each axis's length.
    for (int i = 0; i < 3; ++i) {
        const double length = std::sqrt(m.m[i][0] * m.m[i][0] + m.m[i][1] * m.m[i][1] + m.m[i][2] * m.m[i][2]);
        const Vec& a = axes[i];
        for (int j = 0; j < 3; ++j) m.m[i][j] = length * (a.x * c.m[j][0] + a.y * c.m[j][1] + a.z * c.m[j][2]);
    }
    write_matrix(rdram, at, m);
}

// Research: BLOCKPLANET_MUSIC_LOG=1 logs each track the game starts
// (func_8007F720: a0 track, a1 player) with the time, the intro's and title's
// state and the logo's and intro scenes' frames.
extern "C" void blockplanet_music_log(std::uint8_t* rdram, recomp_context* ctx) {
    static const bool on = std::getenv("BLOCKPLANET_MUSIC_LOG") != nullptr;
    if (!on) return;
    const auto frame_of = [&](std::uint32_t pointer) {
        const std::uint32_t scene = load32(rdram, pointer);
        return (scene >> 24) == 0x80u ? static_cast<int>(load32(rdram, scene + 0x24u)) : -1;
    };
    std::fprintf(stdout,
                 "{\"event\":\"music_start\",\"track\":%u,\"player\":\"0x%08X\",\"intro_done\":%d,\"title_done\":%d,"
                 "\"logo_frame\":%d,\"intro_frame\":%d,\"timer\":%d}\n",
                 static_cast<unsigned>(ctx->r4), static_cast<std::uint32_t>(ctx->r5), load16(rdram, 0x800E1648u),
                 load16(rdram, 0x800E1646u), frame_of(logo_scene_pointer), frame_of(intro_scene_pointer),
                 static_cast<int>(load32(rdram, 0x800E1208u)));
    std::fflush(stdout);
}

// The intro's sphere close-up (the owner's report, 2026-11-11): the intro
// scene (pointer at 0x800E1670) is made of separate brick objects, and as the
// camera closes in, bricks a 4:3 view would not see are faded out by the
// scene itself (their opacity byte, record +0x80, keyed to 0), so wider or
// taller windows show holes in the sphere. Through the close-up, an object
// of the intro scene that has been seen fully opaque is kept opaque, so the
// sphere stays whole at any window shape. BLOCKPLANET_INTRO_BRICKS=0 keeps
// the original fades.
extern "C" void blockplanet_intro_bricks(std::uint8_t* rdram, recomp_context* ctx) {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_BRICKS");
        return v == nullptr || v[0] != '0';
    }();
    if (!on) return;
    const std::uint32_t scene = static_cast<std::uint32_t>(ctx->r4);
    if (scene == 0 || scene != load32(rdram, intro_scene_pointer) || load16(rdram, 0x800E1648u) != 0) return;
    static std::uint32_t seen_scene = 0;
    static std::vector<bool> opaque;
    const int count = static_cast<int>(load32(rdram, scene + 0x10u));
    if (scene != seen_scene || static_cast<int>(opaque.size()) != count) {
        seen_scene = scene;
        opaque.assign(static_cast<std::size_t>(std::max(count, 0)), false);
    }
    // Only through the sphere's close-up (scene frames 120 to 300, where the
    // bricks that drop out are the sphere's own; later fades are the intro's
    // own effects, which must still happen). BLOCKPLANET_INTRO_BRICKS_TO=frame.
    static const int last_frame = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_BRICKS_TO");
        return v ? std::atoi(v) : 300;
    }();
    const int frame = static_cast<int>(load32(rdram, scene + 0x24u));
    if (frame > last_frame) return;
    const std::uint32_t objects = load32(rdram, scene + 0x14u);
    for (int k = 0; k < count && k < 512; ++k) {
        const std::uint32_t at = objects + static_cast<std::uint32_t>(k) * object_size + 0x80u;
        const std::uint8_t alpha = load8(rdram, at);
        if (alpha == 0xFF) opaque[static_cast<std::size_t>(k)] = true;
        else if (alpha == 0 && opaque[static_cast<std::size_t>(k)]) store8(rdram, at, 0xFF);
    }
}
