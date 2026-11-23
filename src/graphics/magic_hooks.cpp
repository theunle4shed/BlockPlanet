// The held magic's 3D model in BlockPlanet's item box (docs/HUD_DESIGN.md).
//
// func_800C78A0 draws the held magic each frame through the model helper
// 0x8002D93C with an identity matrix (0x80161540) and a translation matrix
// (0x801309A8: 0, 0, -2054). The model's own animation places it above the
// bowl, 0.38 screen heights right of the picture's centre and 0.29 below it.
// Measured by moving it (sideways and in depth): the camera is about 1,610
// units from it, so 1 screen height is about 1,133 units there, and taking the
// camera at the origin, the model's own centre is at (431, -331, 445) in its
// space (445 units nearer the camera than the matrix's origin). Just before the
// call (hook at 0x800C7ACC), BlockPlanet replaces the translation matrix so
// the model turns slowly about its own vertical axis, at a size that fits the
// item box, centred on the item box, with its animation held at frame 0.
//
// When the magic is used, the game draws the same model flying from the
// bowl's spot over the sphere (through the same matrix, from other routines).
// For a moment after use, those draws are shifted by the box-to-bowl offset,
// fading to nothing, so the flight starts from the item box.
//
// The held draw is wrapped in a left-anchored UI span (hooks in func_800CD3B8
// around 0x800CFF64) so RT64 moves it to the left edge on wide pictures with
// the HUD instead of clipping it at the original 4:3 edge.
//
// Research switches: BLOCKPLANET_MAGIC_XFORM="dx,dy,dz,scale,frame" adds to the
// game's matrix instead (frame -1 keeps the game's); BLOCKPLANET_MAGIC_ANGLE
// (radians) holds the turn; BLOCKPLANET_MAGIC_PZ, _SWEEP, _NEAR, _TX, _TY
// adjust the pivot, depth and place; BLOCKPLANET_MAGIC_LOG logs the models
// drawn through the matrix after use.

#include "tetrisphere/magic_model.h"
#include "tetrisphere/powerup_models.h"
#include "tetrisphere/barrage_scene.h"
#include "tetrisphere/recomp_support.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The six magics' centres from the turn's pivot, in model units (x right,
// y up, z towards the camera at angle 0): 0 Firecracker to 5 Ray Gun.
// Measured 2026-11-19 in Rescue 1:1 at 1280 x 720 (the turn held at 0, 90,
// 180 and 270 degrees, BLOCKPLANET_MAGIC_ANGLE; 100 units moved the model 25
// pixels, BLOCKPLANET_MAGIC_PIVOT).
constexpr std::array<std::array<float, 3>, 6> magic_model_centres{{
    {21.0f, -20.0f, -109.0f},   // Firecracker
    {39.0f, -12.0f, -37.0f},    // Bundle O' Dynamite
    {-8.0f, 24.0f, 42.0f},      // Electro Magnet
    {116.0f, -84.0f, -160.0f},  // Atom
    {-5.0f, 19.0f, 84.0f},      // Bomb
    {5.0f, 1.0f, 56.0f},        // Ray Gun
}};

namespace {

constexpr std::uint32_t translation_matrix = 0x801309A8u;
constexpr std::uint32_t held_magic = 0x80110922u;  // player 1 +0x702 (byte)

// The held model's place in the picture at scale 1, in screen heights from
// the picture's centre (x right, y down), and units per screen height at its
// depth. Measured in Rescue 1:1 (16:9).
constexpr float bowl_x = 0.381f;
constexpr float bowl_y = 0.292f;
constexpr float units_per_height = 1133.0f;

float load_float(const std::uint8_t* rdram, std::uint32_t address) {
    float value;
    std::memcpy(&value, rdram + (address & 0x00FFFFFFu), 4);
    return value;
}
void store_float(std::uint8_t* rdram, std::uint32_t address, float value) {
    std::memcpy(rdram + (address & 0x00FFFFFFu), &value, 4);
}

struct Research {
    bool set = false;
    float dx = 0, dy = 0, dz = 0, scale = 1;
    int frame = -1;
};
const Research& research() {
    static const Research value = [] {
        Research r;
        if (const char* v = std::getenv("BLOCKPLANET_MAGIC_XFORM"))
            r.set = std::sscanf(v, "%f,%f,%f,%f,%d", &r.dx, &r.dy, &r.dz, &r.scale, &r.frame) >= 4;
        return r;
    }();
    return value;
}

// Where the item box is, in screen heights from the picture's centre; set by
// the game thread (set_magic_box). NaN: not in the picture (draw nothing).
std::atomic<float> box_x{NAN}, box_y{NAN}, box_scale{0.6f};
// The box in the held draw's own frame: the draw is left-anchored, so x is
// measured from the 4:3 frame's left edge as the box is from the picture's.
std::atomic<float> anchored_x{NAN};
// How far a left-anchored span moves things on this picture (screen heights):
// half the picture's width beyond 4:3.
std::atomic<float> anchor_shift{0.0f};
std::atomic<float> box_w{0.0f}, box_h{0.0f}, shift_2d{0.0f};
std::atomic<float> charge{0.0f};  // the magic meter, 0 to 1 (set_magic_charge)
// The held model sits this much right of where the numbers put it (measured).
// Every model sat 3 pixels of 720 left of the box's middle (2026-11-19).
constexpr float box_correction = -3.0f / 720.0f;
std::atomic<bool> enabled{false};

// The flight: when the held magic is used, the offset from the bowl's spot to
// the box, fading out.
using Clock = std::chrono::steady_clock;
std::int8_t last_held = -1;
Clock::time_point used_at{};
constexpr double flight_seconds = 0.6;

std::int8_t held(const std::uint8_t* rdram) {
    return static_cast<std::int8_t>(rdram[(held_magic & 0x00FFFFFFu) ^ 3u]);
}

} // namespace

namespace blockplanet {

void set_magic_box(bool shown, float x, float y, float scale, float x_anchored, float shift, float width,
                   float height, float shift_flat) {
    shift_2d.store(shift_flat);
    box_w.store(width);
    box_h.store(height);
    enabled.store(shown);
    anchored_x.store(x_anchored - box_correction);
    anchor_shift.store(shift);
    box_x.store(x);
    box_y.store(y);
    box_scale.store(scale);
}

void set_magic_charge(float level) { charge.store(std::clamp(level, 0.0f, 1.0f)); }
namespace { std::atomic<bool> box_plain{false}, models_hidden{false}; }
void set_magic_box_plain(bool plain) { box_plain.store(plain); }
void set_magic_models_hidden(bool hidden) { models_hidden.store(hidden); }

bool magic_box_active() {
    return enabled.load() && !std::isnan(box_x.load()) && !std::isnan(box_y.load()) && !research().set;
}

} // namespace blockplanet

extern "C" bool blockplanet_magic_box_active() { return blockplanet::magic_box_active(); }

namespace {

void store_word(std::uint8_t* rdram, std::uint32_t address, std::uint32_t value) {
    std::memcpy(rdram + (address & 0x00FFFFFFu), &value, 4);  // words are kept in host order
}

// The item box's tint, drawn by the game (its rectangle routine
// func_8006FA28: x0, y0, x1, y1 in the 320 x 240 frame, then red, green, blue
// and alpha on the stack) just before the model, inside the same left-anchored
// span, so it is part of the game's picture and truly behind the model. The
// HUD's own tint would sit in front of the model and dim it.
void draw_box_tint(std::uint8_t* rdram, recomp_context* ctx, float centre_x, float centre_y) {
    // With the ring meter the panel is solid and the same colour, so the
    // rectangle is drawn a little larger than the box: no gap at its edges
    // where it fell a few pixels short (the owner saw a line round it).
    const float grow = blockplanet::box_plain.load() ? 1.08f : 1.0f;
    const float w = box_w.load() * grow, h = box_h.load() * grow;
    if (w <= 0.0f || h <= 0.0f) return;
    static const int alpha = [] {  // research: BLOCKPLANET_MAGIC_TINT (0 to 255)
        const char* v = std::getenv("BLOCKPLANET_MAGIC_TINT");
        return v ? std::atoi(v) : 0xFF;  // solid (the owner, 2026-11-18: the box was see-through); was 0xB0
    }();
    if (alpha <= 0) return;
    const int a = blockplanet::box_plain.load() ? 0xFF : alpha;  // the panel's own colour, solid as the panel, no box (the ring meter)
    // Picture heights to the 320 x 240 frame (240 units per picture height).
    const auto to_x = [](float v) { return static_cast<int>(std::lround(160.0f + v * 240.0f)); };
    const auto to_y = [](float v) { return static_cast<int>(std::lround(120.0f + v * 240.0f)); };
    const recomp_context saved = *ctx;
    ctx->r29 = static_cast<gpr>(static_cast<std::int32_t>(saved.r29) - 0x40);  // a fresh area for the arguments
    const std::uint32_t sp = static_cast<std::uint32_t>(ctx->r29);
    store_word(rdram, sp + 0x10, 0x0B);
    store_word(rdram, sp + 0x14, 0x10);
    store_word(rdram, sp + 0x18, 0x20);
    store_word(rdram, sp + 0x1C, static_cast<std::uint32_t>(std::clamp(a, 0, 255)));
    ctx->r4 = to_x(centre_x - w / 2.0f);
    ctx->r5 = to_y(centre_y - h / 2.0f);
    ctx->r6 = to_x(centre_x + w / 2.0f);
    ctx->r7 = to_y(centre_y + h / 2.0f);
    func_8006FA28(rdram, ctx);
    // The charge: the bottom of the box, the HUD's fill colour (#9c5cf0 at a
    // quarter over the box's #0b1020), so the model is drawn in front of it.
    if (const float level = charge.load(); level > 0.0f) {
        *ctx = saved;
        ctx->r29 = static_cast<gpr>(static_cast<std::int32_t>(saved.r29) - 0x40);
        store_word(rdram, sp + 0x10, 0x30);
        store_word(rdram, sp + 0x14, 0x23);
        store_word(rdram, sp + 0x18, 0x54);
        store_word(rdram, sp + 0x1C, 0xFF);
        ctx->r4 = to_x(centre_x - w / 2.0f);
        ctx->r5 = to_y(centre_y + h / 2.0f - h * level);
        ctx->r6 = to_x(centre_x + w / 2.0f);
        ctx->r7 = to_y(centre_y + h / 2.0f);
        func_8006FA28(rdram, ctx);
    }
    *ctx = saved;
}

} // namespace

// Research (BLOCKPLANET_MAGIC_DL_LOG, 2026-11-21, for BlockPlanet's own 3D
// power-up models): the display list the model helper writes for each held
// power-up, once per kind, with the lists it calls.
namespace {
// Set just before the game draws the held power-up (func_800C78A0's call, the
// hook blockplanet_held_magic_draw): the next model draw is the power-up. (The
// core's character is drawn through the same matrix while it is held, so
// "through the matrix while held" is not enough.)
bool held_draw_next = false;
std::uint32_t dl_log_start = 0;
int dl_log_kind = -1;
bool dl_logged[6] = {};
std::uint32_t load32_at(const std::uint8_t* rdram, std::uint32_t a) {
    std::uint32_t v;
    std::memcpy(&v, rdram + (a & 0x007FFFFCu), 4);
    return v;
}
// Guest bytes (memory holds the guest's words byte-swapped: a byte's address ^ 3).
std::uint8_t rdram_byte(const std::uint8_t* rdram, std::uint32_t a) { return rdram[(a & 0x007FFFFFu) ^ 3u]; }
void rdram_set_byte(std::uint8_t* rdram, std::uint32_t a, std::uint8_t v) { rdram[(a & 0x007FFFFFu) ^ 3u] = v; }
void dl_dump(const std::uint8_t* rdram, std::uint32_t from, std::uint32_t to, int depth) {
    for (std::uint32_t a = from; a + 8u <= to && a < from + 8u * 400u; a += 8u) {
        const std::uint32_t w0 = load32_at(rdram, a), w1 = load32_at(rdram, a + 4u);
        std::fprintf(stdout, "MDL %d %08X %08X %08X\n", depth, a, w0, w1);
        if ((w0 >> 24) == 0x04u) {  // G_VTX: the vertices' bounds (model units) and their normals' range
            const int n = int((w0 >> 20) & 0xFu) + 1;
            int lo[3] = {99999, 99999, 99999}, hi[3] = {-99999, -99999, -99999};
            for (int v = 0; v < n; ++v) {
                const std::uint32_t at = w1 + 16u * std::uint32_t(v);
                const std::uint32_t xy = load32_at(rdram, at), zf = load32_at(rdram, at + 4u);
                const int c[3] = {std::int16_t(xy >> 16), std::int16_t(xy & 0xFFFF), std::int16_t(zf >> 16)};
                for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], c[k]), hi[k] = std::max(hi[k], c[k]);
            }
            std::fprintf(stdout, "MVB %d %d %d %d %d %d %d\n", n, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        }
        if ((w0 >> 24) == 0x03u && ((w0 >> 16) & 0xFFu) >= 0x86u) {  // a light: its colour and direction
            const std::uint32_t c0 = load32_at(rdram, w1), d0 = load32_at(rdram, w1 + 8u);
            std::fprintf(stdout, "MLT %02X %08X %08X\n", (w0 >> 16) & 0xFFu, c0, d0);
        }
        if ((w0 >> 24) == 0x06u && depth < 2 && (w1 & 0xFF000000u) == 0x80000000u) {
            std::uint32_t end = w1;
            while (end < w1 + 8u * 600u && (load32_at(rdram, end) >> 24) != 0xB8u) end += 8u;
            dl_dump(rdram, w1, end + 8u, depth + 1);
        }
    }
}
}  // namespace

extern "C" void blockplanet_held_magic_draw(std::uint8_t* rdram, recomp_context* ctx) {
    held_draw_next = true;
    {
        static const bool log = std::getenv("BLOCKPLANET_MAGIC_DL_LOG") != nullptr;
        const int k = held(rdram);
        if (log && k >= 0 && k < 6 && !dl_logged[k]) dl_log_start = load32_at(rdram, 0x800F22B4u), dl_log_kind = k;
    }
    const auto& r = research();
    constexpr std::uint32_t m = translation_matrix;
    if (r.set) {
        store_float(rdram, m + 0x00, r.scale);
        store_float(rdram, m + 0x14, r.scale);
        store_float(rdram, m + 0x28, r.scale);
        store_float(rdram, m + 0x30, load_float(rdram, m + 0x30) + r.dx);
        store_float(rdram, m + 0x34, load_float(rdram, m + 0x34) + r.dy);
        store_float(rdram, m + 0x38, load_float(rdram, m + 0x38) + r.dz);
        if (r.frame >= 0) ctx->r5 = r.frame;
        return;
    }
    if (!enabled.load()) return;
    static const float extra_y = [] {  // research: BLOCKPLANET_MAGIC_TY (screen heights, down)
        const char* v = std::getenv("BLOCKPLANET_MAGIC_TY");
        return v ? std::strtof(v, nullptr) : 0.0f;
    }();
    static const float extra_x = [] {  // research: BLOCKPLANET_MAGIC_TX (screen heights, right)
        const char* v = std::getenv("BLOCKPLANET_MAGIC_TX");
        return v ? std::strtof(v, nullptr) : 0.0f;
    }();
    const float bx = anchored_x.load() + extra_x, by = box_y.load() + extra_y;
    if (std::isnan(bx) || std::isnan(by)) return;
    if (blockplanet::models_hidden.load()) {
        // The flat icons: the model drawn at no size (every point on one spot,
        // so nothing shows), no tint; the HUD draws the icon.
        for (int i = 0; i < 15; ++i) store_float(rdram, m + std::uint32_t(4 * i), 0.0f);
        store_float(rdram, m + 0x3C, 1.0f);
        ctx->r5 = 0;
        return;
    }
    draw_box_tint(rdram, ctx, box_x.load() + shift_2d.load(), by);
    // The model's own position (model units), the turn and the size.
    static const float pz_set = [] {  // research: the model's own depth offset
        const char* v = std::getenv("BLOCKPLANET_MAGIC_PZ");
        return v ? std::strtof(v, nullptr) : 445.0f;
    }();
    static const bool sweep = std::getenv("BLOCKPLANET_MAGIC_SWEEP") != nullptr;  // research: pz 0, 250, ... every 1.2 s
    static const auto sweep_start = Clock::now();
    const float pz = sweep ? 250.0f * float(int(std::chrono::duration<double>(Clock::now() - sweep_start).count() / 1.2) % 10)
                           : pz_set;
    static float logged_pz = -1;
    if (sweep && pz != logged_pz) {
        logged_pz = pz;
        std::fprintf(stdout, "{\"event\":\"magic_sweep\",\"pz\":%g,\"ms\":%lld}\n", pz,
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        std::fflush(stdout);
    }
    static const float fixed_angle = [] {  // research: hold the turn at this angle (radians)
        const char* v = std::getenv("BLOCKPLANET_MAGIC_ANGLE");
        return v ? std::strtof(v, nullptr) : NAN;
    }();
    // Each model's own centre (model units, from the turn's pivot), so it
    // turns about its middle in the middle of the box (the owner, 2026-11-19:
    // the Firecracker sat in the box's corner). Measured from screenshots at
    // four quarter turns (docs/HUD_DESIGN.md). BLOCKPLANET_MAGIC_PIVOT="x,y,z"
    // replaces them all, for measuring.
    static const std::array<std::array<float, 3>, 6> centres = [] {
        std::array<std::array<float, 3>, 6> c{};
        if (const char* v = std::getenv("BLOCKPLANET_MAGIC_PIVOT")) {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(v, "%f,%f,%f", &x, &y, &z) == 3)
                for (auto& e : c) e = {x, y, z};
            return c;
        }
        c = magic_model_centres;
        return c;
    }();
    const std::int8_t which = held(rdram);
    const auto& centre = centres[std::size_t(std::clamp<int>(which, 0, 5))];
    const float px = bowl_x * units_per_height + centre[0], py = -bowl_y * units_per_height + centre[1];
    // Drawn nearer the camera (a fifth of the distance) and smaller in the
    // same proportion, so it looks the same but sits in front of the tunnel,
    // whose walls near the screen's edges are nearer than the bowl's spot.
    static const float near = [] {  // research: BLOCKPLANET_MAGIC_NEAR
        const char* v = std::getenv("BLOCKPLANET_MAGIC_NEAR");
        return v ? std::strtof(v, nullptr) : 0.2f;
    }();
    constexpr float camera_distance = 1609.0f;
    const float s = box_scale.load() * near;
    const double t = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    // Research: BLOCKPLANET_MAGIC_QUARTERS steps the turn a quarter every two
    // seconds of the system clock (quarter = whole seconds / 2, mod 4).
    static const bool quarters = std::getenv("BLOCKPLANET_MAGIC_QUARTERS") != nullptr;
    const double wall = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    float angle = std::isnan(fixed_angle) ? static_cast<float>(std::fmod(t * 1.2, 6.283185307179586)) : fixed_angle;
    if (quarters) angle = static_cast<float>(int(std::fmod(wall / 2.0, 4.0))) * 1.5707963f;
    const float pzc = pz + centre[2];
    const float c = std::cos(angle) * s, sn = std::sin(angle) * s;
    // Row-vector matrix (v * M): v' = ((v - p) * S * R_y) + target.
    const float tx = bx * units_per_height * near, ty = -by * units_per_height * near;
    const float tz = -camera_distance * near;
    const float row[4][4] = {
        {c, 0, -sn, 0},
        {0, s, 0, 0},
        {sn, 0, c, 0},
        {-(px * c + pzc * sn) + tx, -py * s + ty, -(-px * sn + pzc * c) + tz, 1},
    };
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) store_float(rdram, m + std::uint32_t(4 * (4 * i + j)), row[i][j]);
    ctx->r5 = 0;  // hold the model's animation still
}

// Called from the draw filter at every model draw (0x8002D93C): after the
// held magic is used, the game draws the same model flying from the bowl's
// spot. From its first draw, for flight_seconds, it is shifted by the
// box-to-bowl offset, fading to nothing, so it starts from the item box. While
// shifted it is drawn as a left-anchored UI span (like the held model), with
// the anchor's shift added back, so its start beside the screen's edge is not
// clipped at the 4:3 edge; when the shift ends the game's own drawing takes
// over at the same place.
namespace {
bool flight_pending = false;
// The flight is its own model object (not the held one), so it is found as
// the first model drawn through the matrix after use that was not drawn
// through it while the magic was held (the core's character, for example).
std::uint32_t flight_model = 0;
std::uint32_t held_models[8] = {};
int held_model_count = 0;
bool seen_while_held(std::uint32_t model) {
    for (int i = 0; i < held_model_count; ++i)
        if (held_models[i] == model) return true;
    return false;
}
Clock::time_point flight_start{};
bool span_open = false;
} // namespace

// BlockPlanet's own 3D models (powerup_models.cpp): the power-up's draw is
// marked at the model helper's start and its geometry replaced at its end.
// Research for now: BLOCKPLANET_MODELS3D=1 (with BLOCKPLANET_MAGIC_MODELS=1 to
// see it in the box).
namespace {
std::uint32_t model3d_start = 0;
int model3d_kind = -1;
// After use: the flying model (and any draw of the same model), told by its
// first vertices' source, recorded while held, for up to 10 seconds.
std::uint32_t held_source[6] = {};
int used_kind = -1;
Clock::time_point used_time{};
bool model3d_after_use = false;
bool model3d_through = false;
// Research (BLOCKPLANET_MAGIC_FLIGHT_LOG): after a power-up is used, each model
// drawn through its matrix: the object, its first vertices' source, the time.
std::uint32_t flight_log_start = 0, flight_log_model = 0;
bool flight_log_pending = false;
Clock::time_point flight_log_used{};
}  // namespace

namespace {
// The barrage's scene (barrage_scene.cpp): game frames, and the last frame
// the launcher's flight was drawn.
int barrage_game_frame = 0, barrage_flight_seen = -1, barrage_use_frame = -1;
Clock::time_point barrage_use_time{};
}  // namespace

extern "C" void blockplanet_magic_flight(std::uint8_t* rdram, recomp_context* ctx) {
    {
        static const bool on = std::getenv("BLOCKPLANET_MODELS3D") != nullptr;
        const int k = held(rdram);
        static int last_k = -1;
        if (on && last_k >= 0 && k < 0) used_kind = last_k, used_time = Clock::now();
        last_k = k;
        const bool through = static_cast<std::uint32_t>(ctx->r7) == translation_matrix;
        if (on && k >= 0 && through && held_draw_next) model3d_start = load32_at(rdram, 0x800F22B4u), model3d_kind = k, model3d_after_use = false;
        // After use: every model draw for 10 s, through the flight's matrix
        // or not (the Magnet's last second is drawn another way, 2026-11-22);
        // only draws of the held model's own vertices are replaced, so other
        // models (the core's character) are left alone.
        else if (on && k < 0 && used_kind >= 0 && held_source[used_kind] != 0 &&
                 std::chrono::duration<double>(Clock::now() - used_time).count() < 10.0)
            model3d_start = load32_at(rdram, 0x800F22B4u), model3d_kind = used_kind, model3d_after_use = true,
            model3d_through = through;
        static const bool flog = std::getenv("BLOCKPLANET_MAGIC_FLIGHT_LOG") != nullptr;
        static int prev = -1;
        if (flog && prev >= 0 && k < 0) flight_log_used = Clock::now();
        prev = k;
        if (flog && k < 0 && flight_log_used != Clock::time_point{} &&
            std::chrono::duration<double>(Clock::now() - flight_log_used).count() < 10.0 &&
            static_cast<std::uint32_t>(ctx->r7) == translation_matrix)
            flight_log_start = load32_at(rdram, 0x800F22B4u), flight_log_model = static_cast<std::uint32_t>(ctx->r4), flight_log_pending = true;
    }
    held_draw_next = false;
    const std::int8_t now_held = held(rdram);
    const auto now = Clock::now();
    const auto model = static_cast<std::uint32_t>(ctx->r4);
    const bool through_matrix = static_cast<std::uint32_t>(ctx->r7) == translation_matrix;
    if (now_held >= 0) {
        if (last_held < 0) held_model_count = 0;
        if (through_matrix && !seen_while_held(model) && held_model_count < 8) held_models[held_model_count++] = model;
    }
    if (last_held >= 0 && now_held < 0 && blockplanet::magic_box_active()) {
        flight_model = 0;
        flight_pending = true;
        used_at = now;
        flight_start = {};
    }
    last_held = now_held;
    static const bool log_models = std::getenv("BLOCKPLANET_MAGIC_LOG") != nullptr;  // research
    if (log_models && flight_pending && static_cast<std::uint32_t>(ctx->r7) == translation_matrix) {
        static std::uint32_t seen[16] = {};
        static int count = 0;
        const auto model = static_cast<std::uint32_t>(ctx->r4);
        bool known = false;
        for (int i = 0; i < count; ++i) known = known || seen[i] == model;
        if (!known && count < 16) seen[count++] = model;
        if (!known)
            std::fprintf(stdout, "{\"event\":\"magic_flight_draw\",\"model\":\"0x%08X\",\"frame\":%d,\"expected\":\"0x%08X\"}\n",
                         static_cast<std::uint32_t>(ctx->r4), static_cast<int>(ctx->r5), flight_model);
    }
    if (!flight_pending || now_held >= 0 || !through_matrix) return;
    if (flight_model == 0 && !seen_while_held(model)) flight_model = model;
    if (model != flight_model) return;
    if (std::chrono::duration<double>(now - used_at).count() > 1.5) {
        flight_pending = false;
        return;
    }
    if (flight_start == Clock::time_point{}) flight_start = now;
    const double since = std::chrono::duration<double>(now - flight_start).count();
    if (since >= flight_seconds) {
        flight_pending = false;
        return;
    }
    // Ease in and out from the box onto the game's own path.
    const float t = static_cast<float>(since / flight_seconds);
    const float fade = 1.0f - t * t * (3.0f - 2.0f * t);
    const float shift = anchor_shift.load();
    // With BlockPlanet's models it comes in from off the screen's left edge
    // (the owner, 2026-11-22: it appeared beside the HUD), not from the box.
    static const bool models3d = std::getenv("BLOCKPLANET_MODELS3D") != nullptr;
    const float from_x = models3d ? box_x.load() - 0.9f : box_x.load();
    const float dx = ((from_x - box_correction - bowl_x) * fade + shift) * units_per_height;
    const float dy = -(box_y.load() - bowl_y) * fade * units_per_height;
    constexpr std::uint32_t m = translation_matrix;
    store_float(rdram, m + 0x30, load_float(rdram, m + 0x30) + dx);
    store_float(rdram, m + 0x34, load_float(rdram, m + 0x34) + dy);
    tetrisphere_ui_span_begin(rdram, ctx, 0x8002D93Cu, 1u);
    span_open = true;
}

extern "C" void blockplanet_model_draw_end(std::uint8_t* rdram, recomp_context* ctx) {
    if (flight_log_pending) {
        flight_log_pending = false;
        const std::uint32_t end = load32_at(rdram, 0x800F22B4u);
        std::uint32_t vtx = 0, verts = 0, cmds = 0;
        for (std::uint32_t a = flight_log_start; a < end && a < flight_log_start + 8u * 4000u; a += 8u, ++cmds) {
            const std::uint32_t w0 = load32_at(rdram, a);
            if ((w0 >> 24) == 0x04u) {
                if (vtx == 0) vtx = load32_at(rdram, a + 4u);
                verts += ((w0 >> 20) & 0xFu) + 1u;
            }
        }
        std::fprintf(stdout, "{\"event\":\"magic_after_use\",\"ms\":%lld,\"model\":\"0x%08X\",\"vtx\":\"0x%08X\",\"verts\":%u,\"cmds\":%u}\n",
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - flight_log_used).count(), flight_log_model, vtx, verts, cmds);
        std::fflush(stdout);
    }
    if (model3d_kind >= 0) {
        // Research: the level from guest byte 0x807FFF00 (1 to 3; a poke), else 1.
        const int poked = rdram[(0x807FFF00u ^ 3u) & 0x007FFFFFu];
        const int level = poked >= 1 && poked <= 3 ? poked : 1;
        std::uint32_t seen = 0;
        bool done;
        if (model3d_after_use) {
            // The Bomb turns over in flight (flies in fins first, lands red
            // end first): ours is turned round whenever its nose would point
            // against its movement, so it always flies nose first (the owner,
            // 2026-11-22).
            int draw_kind = -1;
            double turn = 0.0;  // degrees about the model's own x axis
            // (Only for the flying bomb's own draws: other models drawn now,
            // the core's character, have their own matrices.)
            std::uint32_t first_source = 0;
            for (std::uint32_t a = model3d_start, e = load32_at(rdram, 0x800F22B4u); a < e && a < model3d_start + 8u * 4000u; a += 8u)
                if ((load32_at(rdram, a) >> 24) == 0x04u) {
                    first_source = load32_at(rdram, a + 4u);
                    break;
                }
            if (model3d_kind == 4 && first_source == held_source[4]) {
                // Rolled over in step with the game's own turn: as the game
                // swings its nose round towards the camera (from into the screen,
                // about -0.3, to facing it, about 0.4), ours rolls half a turn
                // about its own axis, so it never flips in one frame (the owner,
                // 2026-11-22) and always dives nose first. Never rolls back.
                static Clock::time_point flight{};
                // (It flies in nose up, side on to the camera, so the roll waits
                // until it has first pointed into the screen.)
                static double rolled = 0.0;
                static bool into_screen = false;
                if (flight != used_time) rolled = 0.0, into_screen = false;
                flight = used_time;
                const double nz = tetrisphere::powerup_models::nose_towards_camera(rdram, model3d_start);
                if (nz > -1.5 && nz < -0.5) into_screen = true;
                if (nz > -1.5 && into_screen) {
                    const double u = std::clamp((nz + 0.3) / 0.7, 0.0, 1.0);
                    rolled = std::max(rolled, 180.0 * u * u * (3.0 - 2.0 * u));
                }
                turn = rolled;
            }
            // Level 3: the scene draws the launcher (barrage_scene.cpp), so
            // the game's flight is not drawn.
            if (model3d_kind == 4 && level == 3 && first_source == held_source[4])
                done = tetrisphere::powerup_models::hide(rdram, model3d_start);
            else
                done = tetrisphere::powerup_models::replace(rdram, model3d_start, model3d_kind, level, held_source[model3d_kind], &seen, false,
                                                            draw_kind, turn);
            // Dynamite splits into five sticks, each its own model of one
            // vertex load of 32 (2026-11-22): each drawn as one of ours.
            // The Bomb's second model (our Missile at levels 2 and 3, about
            // 2,500 vertices, its own object) shows the bomb in the crater
            // after the first blast.
            // Only the first such model's own shape (its first vertex source
            // and vertex count); after it the same object draws a fireball,
            // which stays the game's.
            if (!done && model3d_kind == 4 && model3d_through) {
                static std::uint32_t dive_source = 0, dive_verts = 0;
                static Clock::time_point dive_use{};
                if (dive_use != used_time) dive_use = used_time, dive_source = 0, dive_verts = 0;
                const std::uint32_t end = load32_at(rdram, 0x800F22B4u);
                std::uint32_t verts = 0, first = 0;
                for (std::uint32_t a = model3d_start; a < end && a < model3d_start + 8u * 8000u; a += 8u) {
                    const std::uint32_t w0 = load32_at(rdram, a);
                    if ((w0 >> 24) != 0x04u) continue;
                    if (first == 0) first = load32_at(rdram, a + 4u);
                    verts += ((w0 >> 20) & 0xFu) + 1u;
                }
                // (At impact the same model changes shape, 2,452 to 2,620
                // vertices, and grows into the blast: the shape it fell as only.)
                if (dive_source == 0 && verts >= 1000) dive_source = first, dive_verts = verts;
                // It comes after the impact's first blast: the missile is gone
                // by then (the owner, 2026-11-22: "it should just be despawned
                // after impact"), so nothing is drawn in its place.
                if (first != 0 && first == dive_source && verts == dive_verts) done = tetrisphere::powerup_models::hide(rdram, model3d_start);
                static const bool dlog = std::getenv("BLOCKPLANET_MODELS3D_LOG") != nullptr;  // research
                if (dlog) {
                    std::fprintf(stdout, "{\"event\":\"bomb_dive\",\"ms\":%lld,\"verts\":%u,\"first\":\"0x%08X\",\"dive\":\"0x%08X\",\"done\":%d}\n",
                                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - used_time).count(), verts, first,
                                 dive_source, int(done));
                    std::fflush(stdout);
                }
            }
            if (!done && model3d_kind == 1 && model3d_through) {
                const std::uint32_t end = load32_at(rdram, 0x800F22B4u);
                std::uint32_t loads = 0, verts = 0;
                for (std::uint32_t a = model3d_start; a < end && a < model3d_start + 8u * 4000u; a += 8u) {
                    const std::uint32_t w0 = load32_at(rdram, a);
                    if ((w0 >> 24) == 0x04u) ++loads, verts += ((w0 >> 20) & 0xFu) + 1u;
                }
                if (loads >= 2 && verts == 32)
                    done = tetrisphere::powerup_models::replace(rdram, model3d_start, tetrisphere::powerup_models::stick_kind, level, 0, nullptr, true);
            }
        } else
            done = tetrisphere::powerup_models::replace(rdram, model3d_start, model3d_kind, level, 0, &held_source[model3d_kind]);
        static const bool log = std::getenv("BLOCKPLANET_MODELS3D_LOG") != nullptr;  // research
        if (log) {
            std::fprintf(stdout, "{\"event\":\"model3d\",\"kind\":%d,\"after_use\":%d,\"done\":%d,\"held_source\":\"0x%08X\",\"seen\":\"0x%08X\"}\n",
                         model3d_kind, int(model3d_after_use), int(done), held_source[model3d_kind], seen);
            std::fflush(stdout);
        }
        model3d_kind = -1;
    }
    if (dl_log_kind >= 0) {
        std::fprintf(stdout, "MDL_BEGIN %d\n", dl_log_kind);
        dl_dump(rdram, dl_log_start, load32_at(rdram, 0x800F22B4u), 0);
        std::fprintf(stdout, "MDL_END %d\n", dl_log_kind);
        std::fflush(stdout);
        dl_logged[dl_log_kind] = true;
        dl_log_kind = -1;
    }
    if (!span_open) return;
    span_open = false;
    tetrisphere_ui_span_end(rdram, ctx, 0x8002D93Cu);
}

// Power-up levels (the owner, 2026-11-22): the held power-up's family (0 the
// Missile, 1 Dynamite, 2 the Magnet, 3 the Atom) and level (1 to 3), as last
// given by BlockPlanet (the earn rule below, or the test keys).
namespace {
int current_family = -1, current_level = 0;
}
namespace blockplanet {
int powerup_family() { return current_family; }
int powerup_level() { return current_level; }
void set_powerup(int family, int level) { current_family = family, current_level = level; }
}

// Earning a power-up (the owner, 2026-11-22): with nothing held, one of the
// four at random (equal odds) at level 1; with one held, its level goes up
// (to 3 at most) instead of the game's next kind. Called by the game's three
// ways of giving one (the magic meter, a mystery block, a 20+ combo; see the
// patch notes in config/recomp), with the slot's value before; returns the
// value to store. In the power-up test only for now (the levels' effects are
// not built yet); otherwise the game's own rule (one more).
extern "C" int blockplanet_magic_earn(std::uint8_t* rdram, int before) {
    static const bool test = std::getenv("BLOCKPLANET_POWERUP_TEST") != nullptr;
    if (!test) return before + 1;
    static std::mt19937 random{std::random_device{}()};
    static constexpr int kinds[4][3] = {{0, 4, 4}, {1, 1, 1}, {2, 2, 5}, {3, 3, 3}};
    static constexpr int family_of[6] = {0, 1, 2, 3, 0, 2};
    int family, level;
    if (before < 0 || before > 5) {
        family = std::uniform_int_distribution<int>(0, 3)(random), level = 1;
    } else {
        family = family_of[before];
        level = family == current_family && current_level >= 1 ? std::min(3, current_level + 1) : 2;
    }
    rdram[(0x807FFF00u ^ 3u) & 0x007FFFFFu] = std::uint8_t(level);
    current_family = family, current_level = level;
    std::fprintf(stdout, "{\"event\":\"magic_earned\",\"before\":%d,\"kind\":%d,\"level\":%d}\n", before, kinds[family][level - 1], level);
    std::fflush(stdout);
    return kinds[family][level - 1];
}

// The Missile at level 3, the barrage (the owner, 2026-11-22): three strikes,
// each 5 cells from the cursor at 0, 120 and 240 degrees, each the
// Firecracker's blast (radius 4, 49 columns); for now all three at once (see
// below why not one after another yet). In the power-up test,
// where the level-3 Missile is the game's Bomb: its first blast (kind 2, at
// the cursor) is replaced by the first strike, its second (kind 0) is dropped,
// and the next strikes follow as each blast goes off (the owner: "launch the
// next missile as the previous explosion triggers"), about 1.2 s apart. They
// stop once the core is open (+0x2B5C, the stage's end).
extern "C" void func_800C6474(std::uint8_t* rdram, recomp_context* ctx);
namespace {
constexpr std::uint32_t player = 0x80110220u;
int barrage_next = 0;                // 1 to 2: the next strike to fire; 0 none
Clock::time_point barrage_due{};
std::int16_t barrage_x = 0, barrage_y = 0, barrage_a2 = 0;  // the cursor's blast centre (1/256 cell)
std::uint32_t barrage_arg5 = 0;  // the routine's fifth argument (on the stack; it divides by it)
bool barrage_firing = false;          // our own call (not intercepted)
bool barrage_dropping = false;        // the Bomb's second blast to drop
constexpr std::int16_t strike_kind = 1;  // the Firecracker's radius (4)
// The blast routine's per-cell table (the board context +0x1280, 1,024
// bytes): 1 a cell that may still be hit, 0 one already scheduled. Each call
// resets it to 1 before marking its own circle, so a later strike scheduled
// again, with its own (longer) delay, the cells of a piece an earlier strike
// had already scheduled: the white and the break spread from the last strike
// (the owner, 2026-11-22). While the barrage's strikes go off, the cells
// earlier strikes scheduled are put back to 0 after each reset
// (blockplanet_blast_table_reset, a hook just after the reset loop).
bool barrage_keep_table = false;
std::array<std::uint8_t, 1024> barrage_table{};
std::uint32_t blast_table(const std::uint8_t* rdram) { return load32_at(rdram, 0x8013DD00u) + 0x1280u; }
void save_blast_table(const std::uint8_t* rdram) {
    const std::uint32_t t = blast_table(rdram);
    for (std::uint32_t i = 0; i < 1024u; ++i) barrage_table[i] = std::min<std::uint8_t>(barrage_table[i], rdram_byte(rdram, t + i));
}

void strike_offset(int n, std::int16_t& x, std::int16_t& y) {
    double dx, dy;
    tetrisphere::barrage::strike_offset(n, dx, dy);  // the scene's, so the missiles land where the blasts go off
    x = std::int16_t(barrage_x + std::lround(256.0 * dx));
    y = std::int16_t(barrage_y + std::lround(256.0 * dy));
}
// The game's list of bricks waiting to break: its count, one byte at the board
// context +0x21BA (slots at +0x20B6, entries at +0x168E).
int breaking_count(const std::uint8_t* rdram) { return rdram_byte(rdram, load32_at(rdram, 0x8013DD00u) + 0x21BAu); }
bool stage_ending(const std::uint8_t* rdram) { return std::int16_t(load32_at(rdram, player + 0x2B5Cu) >> 16) == 1; }
}  // namespace

extern "C" bool blockplanet_barrage_blast(std::uint8_t* rdram, recomp_context* ctx) {
    static const bool test = std::getenv("BLOCKPLANET_POWERUP_TEST") != nullptr;
    if (!test || barrage_firing) return false;
    const int kind = int(ctx->r7 & 0xFF);
    if (barrage_dropping && kind == 0) {
        barrage_dropping = false;
        return true;
    }
    if (kind != 2 || blockplanet::powerup_family() != 0 || blockplanet::powerup_level() != 3) return false;
    barrage_x = std::int16_t(ctx->r4), barrage_y = std::int16_t(ctx->r5), barrage_a2 = std::int16_t(ctx->r6);
    barrage_arg5 = load32_at(rdram, std::uint32_t(ctx->r29) + 0x14u);
    std::int16_t x, y;
    strike_offset(0, x, y);
    ctx->r4 = std::uint64_t(std::int64_t(x)), ctx->r5 = std::uint64_t(std::int64_t(y)), ctx->r7 = strike_kind;
    barrage_dropping = true;
    tetrisphere::barrage::impact();
    tetrisphere::barrage::clear_targets();
    std::fprintf(stdout, "{\"event\":\"barrage_timing\",\"frames_from_use\":%d,\"seconds\":%.2f}\n", barrage_game_frame - barrage_use_frame,
                 std::chrono::duration<double>(Clock::now() - barrage_use_time).count());
    barrage_next = 1, barrage_due = Clock::now() + std::chrono::milliseconds(1200);
    // The other two strikes go off in the same moment, while the magic is
    // active. One after another (BLOCKPLANET_BARRAGE_SEQUENCE=1, research)
    // leaves the later strikes' bricks white and unbroken: the game runs one
    // magic blast at a time from shared state (the timer at 0x80160C80, the
    // kind at 0x8015F5B8), and only while its magic is working.
    static const bool together = std::getenv("BLOCKPLANET_BARRAGE_SEQUENCE") == nullptr;
    if (together) {
        barrage_table.fill(1);
        barrage_keep_table = true;
        static const bool reversed = std::getenv("BLOCKPLANET_BARRAGE_REVERSED") != nullptr;  // research: strike 2 first
        for (int k = 1; k <= 2; ++k) {
            const int n = reversed ? 3 - k : k;
            std::int16_t sx, sy;
            strike_offset(n, sx, sy);
            const recomp_context saved = *ctx;
            ctx->r4 = std::uint64_t(std::int64_t(sx)), ctx->r5 = std::uint64_t(std::int64_t(sy)), ctx->r7 = strike_kind;
            barrage_firing = true;
            func_800C6474(rdram, ctx);
            barrage_firing = false;
            save_blast_table(rdram);
            std::fprintf(stdout, "{\"event\":\"barrage_breaking\",\"after_strike\":%d,\"count\":%d}\n", n, breaking_count(rdram));
            *ctx = saved;
        }
        barrage_next = 0;
    }
    std::fprintf(stdout, "{\"event\":\"barrage_strike\",\"n\":0,\"x\":%d,\"y\":%d,\"arg5\":\"%08X\"}\n", x, y, barrage_arg5);
    std::fflush(stdout);
    return false;  // the game's call goes on, as the first strike
}

// The power-up's button press (func_800C719C, the use; the game's own target
// shows from here until the blast): BlockPlanet's crosshairs instead.
// The board cell in the middle of the view (the game aims its strikes there).
void view_cell(const std::uint8_t* rdram, int& cx, int& cy) {
    const int ax = int(std::int16_t(load32_at(rdram, player + 0x2B40u) & 0xFFFF));
    const int ay = int(std::int16_t(load32_at(rdram, player + 0x2B44u) & 0xFFFF));
    cx = 8 + int(std::lround((ax - 128) / 256.0)), cy = 8 + int(std::lround((ay - 128) / 256.0));
}

// Where the held power-up would land, at the cursor (the board cells of the
// blasts' centres): one crosshair per strike, at the normal target's size;
// a bigger blast (the Bomb) is red and yellow with arrows pointing out.
namespace {
int power_targets(std::uint8_t* rdram, int held, tetrisphere::barrage::Target* t, bool smooth = false) {
    using tetrisphere::barrage::Target;
    // Not the cursor itself: the view, which eases towards it (+0x2B42,
    // +0x2B46: 128 + 256 x the cursor's offset), rounded to a cell; the blast
    // is a cell before it (measured 2026-11-22: launched with the view at
    // 8.86, 8 the blast fell at 8 x 256, 7 x 256).
    // `smooth`: while aiming, the view's own position (gliding with the
    // sphere as it turns, rather than stepping a cell at a time).
    double cx, cy;
    if (smooth) {
        cx = 8.0 + (int(std::int16_t(load32_at(rdram, player + 0x2B40u) & 0xFFFF)) - 128) / 256.0;
        cy = 8.0 + (int(std::int16_t(load32_at(rdram, player + 0x2B44u) & 0xFFFF)) - 128) / 256.0;
    } else {
        int ix, iy;
        view_cell(rdram, ix, iy);
        cx = ix, cy = iy;
    }
    constexpr std::uint32_t red = 0xFF3A3A, orange = 0xFF9A2E;
    constexpr double size = 1.6;  // cells: the game's own target's size
    static const bool test = std::getenv("BLOCKPLANET_POWERUP_TEST") != nullptr;
    const bool barrage = held == 4 && test && blockplanet::powerup_family() == 0 && blockplanet::powerup_level() == 3;
    if (barrage) {
        for (int n = 0; n < 3; ++n) {
            double dx, dy;
            tetrisphere::barrage::strike_offset(n, dx, dy);
            t[n] = Target{cx - 0.5 + dx, cy - 0.5 + dy, size, red};
        }
        return 3;
    }
    if (held == 0 || held == 4) {
        t[0] = Target{cx - 0.5, cy - 0.5, size, red, held == 4};
        return 1;
    }
    if (held == 1) {
        // Dynamite's five sticks (measured 2026-11-20: offsets from the cursor,
        // docs/research/blockplanet-score-and-magic.md).
        static constexpr double off[5][2] = {{0, -3.1}, {-3.8, -1.8}, {4.6, -2.2}, {-2.7, 2.7}, {3.3, 3.3}};
        for (int n = 0; n < 5; ++n) t[n] = Target{cx + off[n][0] + 0.5, cy + off[n][1] + 0.5, 1.2, orange};
        return 5;
    }
    return 0;
}
bool models_on() {
    static const bool on = std::getenv("BLOCKPLANET_MODELS3D") != nullptr;
    return on;
}
// BlockPlanet's crosshairs in place of the game's target: set aside for now
// (the owner, 2026-11-22: "leave the target as it was with the original game
// and come back to this"); BLOCKPLANET_CROSSHAIRS=1 turns them on.
bool crosshairs_on() {
    static const bool on = models_on() && std::getenv("BLOCKPLANET_CROSSHAIRS") != nullptr;
    return on;
}
}  // namespace

// The power-up's button press (func_800C719C, the use; the game's own target
// shows from here until the blast): the crosshairs stay where it was fired.
// As in the original, the target can still be steered during the countdown:
// the crosshairs follow the cursor until the launch (the held slot empties),
// then stay where it was fired.
bool aiming = false;
extern "C" void blockplanet_magic_use(std::uint8_t* rdram) {
    const int held = std::int8_t(rdram[((player + 0x702u) ^ 3u) & 0x7FFFFFu]);
    static const bool log = std::getenv("BLOCKPLANET_BLAST_LOG") != nullptr;  // research
    if (log) {
        std::fprintf(stdout, "{\"event\":\"magic_use\",\"held\":%d,\"angle\":[%d,%d],\"cursor\":[%d,%d]}\n", held,
                     int(load32_at(rdram, player + 0x2B40u)), int(load32_at(rdram, player + 0x2B44u)),
                     8 + int(std::int16_t(load32_at(rdram, player + 0x2B28u) & 0xFFFF)), 8 + int(std::int16_t(load32_at(rdram, player + 0x2B2Cu) & 0xFFFF)));
        std::fflush(stdout);
    }
    if (!crosshairs_on()) return;
    tetrisphere::barrage::Target t[5];
    const int n = power_targets(rdram, held, t, true);
    if (n > 0) {
        // At once, so the game's own target is not drawn first; any left from
        // an earlier strike go.
        aiming = true;
        tetrisphere::barrage::clear_targets();
        tetrisphere::barrage::set_aim(t, n);
    }
}

extern "C" void blockplanet_blast_table_reset(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    static const bool off = std::getenv("BLOCKPLANET_BARRAGE_SHARED_TABLE") != nullptr;  // research: the old way
    if (!barrage_keep_table || off) return;
    const std::uint32_t t = blast_table(rdram);
    for (std::uint32_t i = 0; i < 1024u; ++i)
        if (barrage_table[i] == 0) rdram_set_byte(rdram, t + i, 0);
}

extern "C" void blockplanet_barrage_tick(std::uint8_t* rdram, recomp_context* ctx) {
    if (barrage_keep_table)
        std::fprintf(stdout, "{\"event\":\"barrage_breaking\",\"after_strike\":0,\"count\":%d}\n", breaking_count(rdram));
    barrage_keep_table = false;  // (the strikes all went off in one call, before this frame's tick)
    // The scene: begins once the launcher's flight has ended (no draw of it
    // for a few frames), over the cursor's cell.
    ++barrage_game_frame;
    tetrisphere::barrage::frame();
    // From the press to the launch the crosshairs follow the cursor (the
    // owner, 2026-11-22: they showed only once it was fired).
    if (crosshairs_on()) {
        static int aim_kind = -1;
        static tetrisphere::barrage::Target last[5];
        static int last_n = 0;
        const int held = std::int8_t(rdram[((player + 0x702u) ^ 3u) & 0x7FFFFFu]);
        static int prev_held = -1;
        static const bool log = std::getenv("BLOCKPLANET_BLAST_LOG") != nullptr;  // research
        if (log && prev_held >= 0 && held < 0) {
            std::fprintf(stdout, "{\"event\":\"magic_launch\",\"was\":%d,\"angle\":[%d,%d],\"cursor\":[%d,%d]}\n", prev_held,
                         int(load32_at(rdram, player + 0x2B40u)), int(load32_at(rdram, player + 0x2B44u)),
                         8 + int(std::int16_t(load32_at(rdram, player + 0x2B28u) & 0xFFFF)), 8 + int(std::int16_t(load32_at(rdram, player + 0x2B2Cu) & 0xFFFF)));
            std::fflush(stdout);
        }
        prev_held = held;
        if (aiming && held >= 0) {
            aim_kind = held;
            tetrisphere::barrage::Target glide[5];
            tetrisphere::barrage::set_aim(glide, power_targets(rdram, held, glide, true));
            last_n = power_targets(rdram, held, last);  // where it would land now (whole cells)
        } else if (aiming) {  // launched: where it was fired
            aiming = false;
            tetrisphere::barrage::set_aim(nullptr, 0);
            if (last_n > 0) tetrisphere::barrage::set_targets(last, last_n);
        } else {
            tetrisphere::barrage::set_aim(nullptr, 0);
        }
        (void)aim_kind;
    }
    // Begins as the level-3 Missile is fired (the held slot goes from the
    // Bomb to empty), over the cursor's cell; the game's own flight is not
    // drawn (blockplanet_model_draw_end).
    {
        static int last_held = -1;
        const int held_now = std::int8_t(rdram[((player + 0x702u) ^ 3u) & 0x7FFFFFu]);

        static const bool test = std::getenv("BLOCKPLANET_POWERUP_TEST") != nullptr;
        if (test && last_held == 4 && held_now < 0 && blockplanet::powerup_family() == 0 && blockplanet::powerup_level() == 3) {
            barrage_use_frame = barrage_game_frame, barrage_use_time = Clock::now();
            int vx, vy;
            view_cell(rdram, vx, vy);
            tetrisphere::barrage::begin(vx, vy);
        }
        last_held = held_now;
    }
    if (barrage_next == 0 || Clock::now() < barrage_due) return;
    if (stage_ending(rdram)) {  // the core is open: no need for the rest
        barrage_next = 0;
        return;
    }
    std::int16_t x, y;
    strike_offset(barrage_next, x, y);
    const recomp_context saved = *ctx;
    ctx->r4 = std::uint64_t(std::int64_t(x)), ctx->r5 = std::uint64_t(std::int64_t(y)), ctx->r6 = std::uint64_t(std::int64_t(barrage_a2)),
    ctx->r7 = strike_kind;
    // Its own stack frame below ours, with the fifth argument in place.
    ctx->r29 = std::uint64_t(std::int64_t(std::int32_t(std::uint32_t(ctx->r29) - 0x40u)));
    {
        const std::uint32_t a = std::uint32_t(ctx->r29) + 0x14u;
        std::memcpy(rdram + (a & 0x007FFFFCu), &barrage_arg5, 4);
    }
    barrage_firing = true;
    func_800C6474(rdram, ctx);
    barrage_firing = false;
    *ctx = saved;
    std::fprintf(stdout, "{\"event\":\"barrage_strike\",\"n\":%d,\"x\":%d,\"y\":%d}\n", barrage_next, x, y);
    std::fflush(stdout);
    barrage_next = barrage_next == 2 ? 0 : barrage_next + 1;
    barrage_due = Clock::now() + std::chrono::milliseconds(1200);
}
