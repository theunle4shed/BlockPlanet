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
#include "tetrisphere/recomp_support.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
// The held model sits this much right of where the numbers put it (measured).
constexpr float box_correction = 0.0f;
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
    const float w = box_w.load(), h = box_h.load();
    if (w <= 0.0f || h <= 0.0f) return;
    static const int alpha = [] {  // research: BLOCKPLANET_MAGIC_TINT (0 to 255)
        const char* v = std::getenv("BLOCKPLANET_MAGIC_TINT");
        return v ? std::atoi(v) : 0xB0;
    }();
    if (alpha <= 0) return;
    // Picture heights to the 320 x 240 frame (240 units per picture height).
    const auto to_x = [](float v) { return static_cast<int>(std::lround(160.0f + v * 240.0f)); };
    const auto to_y = [](float v) { return static_cast<int>(std::lround(120.0f + v * 240.0f)); };
    const recomp_context saved = *ctx;
    ctx->r29 = static_cast<gpr>(static_cast<std::int32_t>(saved.r29) - 0x40);  // a fresh area for the arguments
    const std::uint32_t sp = static_cast<std::uint32_t>(ctx->r29);
    store_word(rdram, sp + 0x10, 0x0B);
    store_word(rdram, sp + 0x14, 0x10);
    store_word(rdram, sp + 0x18, 0x20);
    store_word(rdram, sp + 0x1C, static_cast<std::uint32_t>(std::clamp(alpha, 0, 255)));
    ctx->r4 = to_x(centre_x - w / 2.0f);
    ctx->r5 = to_y(centre_y - h / 2.0f);
    ctx->r6 = to_x(centre_x + w / 2.0f);
    ctx->r7 = to_y(centre_y + h / 2.0f);
    func_8006FA28(rdram, ctx);
    *ctx = saved;
}

} // namespace

extern "C" void blockplanet_held_magic_draw(std::uint8_t* rdram, recomp_context* ctx) {
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
    const float px = bowl_x * units_per_height, py = -bowl_y * units_per_height;
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
    const float angle = std::isnan(fixed_angle) ? static_cast<float>(std::fmod(t * 1.2, 6.283185307179586)) : fixed_angle;
    const float c = std::cos(angle) * s, sn = std::sin(angle) * s;
    // Row-vector matrix (v * M): v' = ((v - p) * S * R_y) + target.
    const float tx = bx * units_per_height * near, ty = -by * units_per_height * near;
    const float tz = -camera_distance * near;
    const float row[4][4] = {
        {c, 0, -sn, 0},
        {0, s, 0, 0},
        {sn, 0, c, 0},
        {-(px * c + pz * sn) + tx, -py * s + ty, -(-px * sn + pz * c) + tz, 1},
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

extern "C" void blockplanet_magic_flight(std::uint8_t* rdram, recomp_context* ctx) {
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
    const float dx = ((box_x.load() - box_correction - bowl_x) * fade + shift) * units_per_height;
    const float dy = -(box_y.load() - bowl_y) * fade * units_per_height;
    constexpr std::uint32_t m = translation_matrix;
    store_float(rdram, m + 0x30, load_float(rdram, m + 0x30) + dx);
    store_float(rdram, m + 0x34, load_float(rdram, m + 0x34) + dy);
    tetrisphere_ui_span_begin(rdram, ctx, 0x8002D93Cu, 1u);
    span_open = true;
}

extern "C" void blockplanet_model_draw_end(std::uint8_t* rdram, recomp_context* ctx) {
    if (!span_open) return;
    span_open = false;
    tetrisphere_ui_span_end(rdram, ctx, 0x8002D93Cu);
}
