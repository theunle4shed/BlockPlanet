#include "tetrisphere/barrage_scene.h"

#include "tetrisphere/guest_memory.h"
#include "tetrisphere/powerup_models.h"
#include "tetrisphere/sphere_extended.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace tetrisphere::barrage {
namespace {
using guest::load16;
using guest::load32;
using guest::load8;
using guest::store16;
using guest::store32;

constexpr std::uint32_t display_list_pointer = 0x800F22B4u;
constexpr std::uint32_t active_context = 0x8013DD00u;
constexpr std::uint32_t board_pointer = 0x800E44DCu;  // one byte per cell, height << 4 | kind
// Our matrices, two frames' worth (the previous frame's list may still be read).
constexpr std::uint32_t matrix_memory = 0x8073C000u;
// The crosshairs' vertices, two frames' worth.
constexpr std::uint32_t ring_memory = 0x80760000u;

// Timing in seconds from the use: the game's logic runs in real time, and the
// blasts go off about 2.27 s after the use (measured 2026-11-22, two runs).
using Clock = std::chrono::steady_clock;
constexpr double arrive_seconds = 0.6;  // the launcher coming down from above
constexpr double release_seconds = 1.2; // the missiles leave it
constexpr double land_seconds = 2.2;    // and reach their strike points
constexpr double leave_seconds = 0.7;   // the empty launcher leaving, after the blasts
constexpr double give_up_seconds = 8.0;
constexpr double hover_layers = 4.0;  // the launcher's height above the surface, in layers

std::atomic<bool> running{false};
std::atomic<int> cursor_x{0}, cursor_y{0};
Clock::time_point started{}, hit_at{};
std::atomic<bool> hit{false};
// Crosshairs.
std::mutex targets_mutex;
std::vector<Target> targets, aim;
Clock::time_point targets_set{};
constexpr double target_seconds = 8.0;  // at most
double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }
std::uint32_t draw_count = 0;

struct V {
    double x = 0, y = 0, z = 0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(V a) { return std::sqrt(dot(a, a)); }
V unit(V a) {
    const double l = length(a);
    return l > 0 ? a * (1.0 / l) : V{0, 0, 1};
}

void append(std::uint8_t* rdram, std::uint32_t w0, std::uint32_t w1) {
    const std::uint32_t at = load32(rdram, display_list_pointer);
    store32(rdram, at, w0);
    store32(rdram, at + 4u, w1);
    store32(rdram, display_list_pointer, at + 8u);
}

// An N64 fixed-point matrix (row vectors): rows x, y, z scaled by `s`, then the position.
void write_matrix(std::uint8_t* rdram, std::uint32_t at, V x, V y, V z, double s, V p) {
    const double m[4][4] = {{x.x * s, x.y * s, x.z * s, 0}, {y.x * s, y.y * s, y.z * s, 0}, {z.x * s, z.y * s, z.z * s, 0}, {p.x, p.y, p.z, 1}};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const std::int32_t fixed = std::int32_t(std::lround(std::clamp(m[i][j], -32767.0, 32767.0) * 65536.0));
            const std::uint32_t k = std::uint32_t(i * 4 + j);
            store16(rdram, at + 2u * k, std::int16_t(fixed >> 16));
            store16(rdram, at + 32u + 2u * k, std::int16_t(fixed & 0xFFFF));
        }
}

} // namespace

void strike_offset(int n, double& dx, double& dy) {
    // 3.5 cells out (2026-11-23; it was 5): the game's list of bricks waiting
    // to break counts in one byte, and three strikes 5 cells apart scheduled
    // about 330 cells: the count went past 255, started again and wrote over
    // the first strike's entries, so half its circle never broke (the owner:
    // the top-right strike). Closer, the circles share cells (about 270
    // scheduled, nearly all breaking), and stay on the side of the sphere in
    // view (5 cells out was at its edge). BLOCKPLANET_BARRAGE_DISTANCE, research.
    static const double distance = [] { const char* v = std::getenv("BLOCKPLANET_BARRAGE_DISTANCE"); return v ? std::atof(v) : 3.5; }();
    const double a = 2.0 * 3.14159265358979 * double(n) / 3.0;
    dx = distance * std::cos(a), dy = distance * std::sin(a);
}

void begin(int x, int y) {
    cursor_x = x, cursor_y = y, started = Clock::now(), hit = false, running = true;
}
void impact() {
    if (running && !hit) hit_at = Clock::now(), hit = true;
}
void frame() {
    if (!running) return;
    static const bool markers = std::getenv("BLOCKPLANET_BARRAGE_MARKERS") != nullptr;
    if (!markers && ((hit && since(hit_at) > leave_seconds) || since(started) > give_up_seconds)) running = false;
}
bool active() { return running; }

void set_targets(const Target* t, int count) {
    std::lock_guard lock(targets_mutex);
    targets.assign(t, t + count);
    targets_set = Clock::now();
}
void blast_at(int a0, int a1) {
    std::lock_guard lock(targets_mutex);
    const double x = a0 / 256.0 + 0.5, y = a1 / 256.0 + 0.5;
    for (auto it = targets.begin(); it != targets.end(); ++it)
        if (std::hypot(it->x - x, it->y - y) < 1.6) {
            targets.erase(it);
            return;
        }
}
void set_aim(const Target* t, int count) {
    std::lock_guard lock(targets_mutex);
    if (count > 0 && aim.empty()) targets_set = Clock::now();  // (the pulse)
    if (count > 0) aim.assign(t, t + count);
    else aim.clear();
}
void clear_targets() {
    std::lock_guard lock(targets_mutex);
    targets.clear();
}
bool hide_game_target() {
    std::lock_guard lock(targets_mutex);
    return !aim.empty() || (!targets.empty() && since(targets_set) < target_seconds);
}

void draw(std::uint8_t* rdram, const sphere::ExtendedSphere& sphere) {
    std::vector<Target> rings;
    {
        std::lock_guard lock(targets_mutex);
        if (!targets.empty() && since(targets_set) > target_seconds) targets.clear();
        rings = targets;
        rings.insert(rings.end(), aim.begin(), aim.end());
    }
    if ((!running && rings.empty()) || rdram == nullptr) return;
    const double t_now = since(started);
    const bool went_off = hit.load();
    const double after_hit = went_off ? since(hit_at) : 0.0;
    const std::uint32_t context = load32(rdram, active_context);
    const int off_row = int(load32(rdram, context + 0x2B20u)), off_column = int(load32(rdram, context + 0x2B24u));
    const std::uint32_t map = load32(rdram, board_pointer);
    const int lo = sphere.first_cell(), hi = sphere.last_cell() + 1;  // mesh points
    // A board position (cells, fractional; the row is the board's x) to the
    // mesh's point coordinates, wrapped to the patch.
    const auto mesh = [&](double board, int offset) {
        double p = board - offset;
        p -= 32.0 * std::round((p - 7.5) / 32.0);
        return std::clamp(p, double(lo), double(hi) - 1e-6);
    };
    const auto layers_at = [&](double bx, double by) {
        const std::uint32_t x = std::uint32_t(int(std::floor(bx))) & 0x1Fu, y = std::uint32_t(int(std::floor(by))) & 0x1Fu;
        return double(std::min((load8(rdram, map + (y << 5) + x) & 0xF) + 1, 7));
    };
    // The surface point over a board position, `extra` layers up, and its outward direction.
    const auto surface = [&](double bx, double by, double extra, V& out) {
        const double r = mesh(bx, off_row), c = mesh(by, off_column);
        const int r0 = int(std::floor(r)), c0 = int(std::floor(c));
        const double fr = r - r0, fc = c - c0;
        V o, s;
        for (int k = 0; k < 4; ++k) {
            const int dr = k & 1, dc = k >> 1;
            const double w = (dr ? fr : 1 - fr) * (dc ? fc : 1 - fc);
            const auto& e = sphere.point(std::min(r0 + dr, hi), std::min(c0 + dc, hi));
            // (The mesh keeps its components in the game's order; vertices
            // take them as 1, 0, 2. The top of a column of h layers is at
            // outer + (h - 1) steps.)
            o = o + V{double(e.outer[1]), double(e.outer[0]), double(e.outer[2])} * w;
            s = s + V{double(e.inner[1]), double(e.inner[0]), double(e.inner[2])} * w;
        }
        out = unit(s);
        return o + s * (layers_at(bx, by) - 1.0 + extra);
    };
    // Research (BLOCKPLANET_BARRAGE_LOG=1): each strike's board cell against
    // the mesh patch (a target outside it is drawn at the patch's edge).
    if (static const bool log = std::getenv("BLOCKPLANET_BARRAGE_LOG") != nullptr; log && running) {
        static Clock::time_point logged{};
        if (logged != started) {
            logged = started;
            for (int n = 0; n < 3; ++n) {
                double dx, dy;
                strike_offset(n, dx, dy);
                const double bx = cursor_x - 0.5 + dx, by = cursor_y - 0.5 + dy;
                double pr = bx - off_row, pc = by - off_column;
                pr -= 32.0 * std::round((pr - 7.5) / 32.0), pc -= 32.0 * std::round((pc - 7.5) / 32.0);
                std::fprintf(stdout, "{\"event\":\"barrage_strike_mesh\",\"n\":%d,\"board\":[%.1f,%.1f],\"mesh\":[%.1f,%.1f],\"patch\":[%d,%d]}\n", n, bx, by, pr, pc, lo, hi);
            }
            std::fflush(stdout);
        }
    }
    // The blasts' centre: a cell before the cursor's on both axes (measured
    // 2026-11-22: the cursor at board 11, 9 gave a blast at 10 x 256, 8 x 256
    // and cleared cells centred on 10, 8), its first coordinate the board's x.
    const double cx = cursor_x - 0.5, cy = cursor_y - 0.5;
    V up;
    const V hover = surface(cx, cy, hover_layers, up);
    V up2;
    const double cell = length(surface(cx + 1.0, cy, hover_layers, up2) - hover);
    if (cell <= 1.0) return;
    // (While only crosshairs show, the cursor above is not the barrage's;
    // the launcher and its missiles are drawn only while it runs.)

    // Back up the sphere's lights and modes, to put back after.
    std::uint32_t lights[6][2] = {}, numlight = 0;
    for (std::uint32_t at = load32(rdram, display_list_pointer) - 8u, n = 0; n < 40000 && at >= 0x80000000u; at -= 8u, ++n) {
        const std::uint32_t w0 = load32(rdram, at);
        if (w0 == 0xBC000002u && numlight == 0) numlight = load32(rdram, at + 4u);
        if ((w0 & 0xFFF0FFFFu) == 0x03800010u) {
            const std::uint32_t k = ((w0 >> 16) & 0xFFu) - 0x86u;
            if (k < 12u && (k & 1u) == 0 && lights[k / 2][0] == 0) lights[k / 2][0] = w0, lights[k / 2][1] = load32(rdram, at + 4u);
        }
        if (numlight != 0 && lights[0][0] != 0) break;
    }

    // The crosshairs: quads on the bricks' tops (a little above), unlit, in
    // their colour, pulsing.
    if (!rings.empty()) {
        const std::uint32_t vbuf = ring_memory + (draw_count & 1u) * 0x8000u;
        std::uint32_t v = vbuf;
        const double pulse = 0.75 + 0.25 * std::sin(since(targets_set) * 9.0);
        append(rdram, 0xE7000000u, 0);
        append(rdram, 0xB6000000u, 0x00023000u);           // no lighting, no culling
        append(rdram, 0xFCFFFFFFu, 0xFFFE793Cu);           // G_CC_SHADE
        append(rdram, 0xBB000000u, 0);                     // texture off
        const auto vertex = [&](V p, std::uint32_t rgb) {
            store16(rdram, v, std::int16_t(std::lround(std::clamp(p.x, -32000.0, 32000.0))));
            store16(rdram, v + 2, std::int16_t(std::lround(std::clamp(p.y, -32000.0, 32000.0))));
            store16(rdram, v + 4, std::int16_t(std::lround(std::clamp(p.z, -32000.0, 32000.0))));
            store16(rdram, v + 6, 0);
            store32(rdram, v + 8, 0);
            const auto c = [&](int sh) { return std::uint32_t(std::lround(double((rgb >> sh) & 0xFF) * pulse)) & 0xFF; };
            store32(rdram, v + 12, c(16) << 24 | c(8) << 16 | c(0) << 8 | 0xFF);
            v += 16;
        };
        const auto quad = [&](V a, V b, V c, V d, std::uint32_t rgb) {
            if (v + 64 > vbuf + 0x8000u) return;
            const std::uint32_t at = v;
            vertex(a, rgb), vertex(b, rgb), vertex(c, rgb), vertex(d, rgb);
            append(rdram, 0x04300040u, at);
            append(rdram, 0xBF000000u, (0u << 16) | (10u << 8) | 20u);
            append(rdram, 0xBF000000u, (0u << 16) | (20u << 8) | 30u);
        };
        constexpr double lift = 0.25;  // layers above the bricks
        for (const Target& t : rings) {
            // The ring: its inner and outer edge on the surface.
            const double width = 0.2;
            constexpr int steps = 40;
            for (int k = 0; k < steps; ++k) {
                const double a0 = 6.283185307 * k / steps, a1 = 6.283185307 * (k + 1) / steps;
                V n;
                const V p0 = surface(t.x + (t.radius - width) * std::cos(a0), t.y + (t.radius - width) * std::sin(a0), lift, n);
                const V p1 = surface(t.x + t.radius * std::cos(a0), t.y + t.radius * std::sin(a0), lift, n);
                const V p2 = surface(t.x + t.radius * std::cos(a1), t.y + t.radius * std::sin(a1), lift, n);
                const V p3 = surface(t.x + (t.radius - width) * std::cos(a1), t.y + (t.radius - width) * std::sin(a1), lift, n);
                quad(p0, p1, p2, p3, t.rgb);
            }
            // Four ticks across the ring, and a small cross at the centre.
            const auto bar = [&](double x0, double y0, double x1, double y1, double half) {
                const double dx = x1 - x0, dy = y1 - y0, l = std::hypot(dx, dy);
                const double px = -dy / l * half, py = dx / l * half;
                V n;
                quad(surface(x0 + px, y0 + py, lift, n), surface(x1 + px, y1 + py, lift, n), surface(x1 - px, y1 - py, lift, n),
                     surface(x0 - px, y0 - py, lift, n), t.rgb);
            };
            for (int k = 0; k < 4; ++k) {
                const double a = 1.5707963 * k, c = std::cos(a), sn = std::sin(a);
                bar(t.x + c * (t.radius - 0.55), t.y + sn * (t.radius - 0.55), t.x + c * (t.radius + 0.35), t.y + sn * (t.radius + 0.35), 0.09);
            }
            bar(t.x - 0.3, t.y, t.x + 0.3, t.y, 0.07);
            bar(t.x, t.y - 0.3, t.x, t.y + 0.3, 0.07);
            if (t.big) {
                // A second, yellow ring outside, and four arrows pointing out
                // between the ticks: this one reaches further.
                constexpr std::uint32_t yellow = 0xFFD23A;
                const double r2 = t.radius + 0.55;
                for (int k = 0; k < steps; ++k) {
                    const double a0 = 6.283185307 * k / steps, a1 = 6.283185307 * (k + 1) / steps;
                    V n;
                    quad(surface(t.x + (r2 - 0.2) * std::cos(a0), t.y + (r2 - 0.2) * std::sin(a0), lift, n),
                         surface(t.x + r2 * std::cos(a0), t.y + r2 * std::sin(a0), lift, n),
                         surface(t.x + r2 * std::cos(a1), t.y + r2 * std::sin(a1), lift, n),
                         surface(t.x + (r2 - 0.2) * std::cos(a1), t.y + (r2 - 0.2) * std::sin(a1), lift, n), yellow);
                }
                for (int k = 0; k < 4; ++k) {
                    const double a = 0.785398 + 1.5707963 * k, c = std::cos(a), sn = std::sin(a);
                    const double pc = -sn, ps = c;  // across
                    const double r_in = r2 + 0.2, r_tip = r2 + 1.0;
                    bar(t.x + c * r_in, t.y + sn * r_in, t.x + c * (r_tip - 0.25), t.y + sn * (r_tip - 0.25), 0.09);
                    // the head: two short strokes back from the tip
                    bar(t.x + c * r_tip, t.y + sn * r_tip, t.x + c * (r_tip - 0.4) + pc * 0.35, t.y + sn * (r_tip - 0.4) + ps * 0.35, 0.08);
                    bar(t.x + c * r_tip, t.y + sn * r_tip, t.x + c * (r_tip - 0.4) - pc * 0.35, t.y + sn * (r_tip - 0.4) - ps * 0.35, 0.08);
                }
            }
        }
    }

    const std::uint32_t buffer = matrix_memory + (draw_count++ & 1u) * 0x800u;
    std::uint32_t slot = 0;
    const auto place = [&](int kind, V x, V y, V z, double scale, V at) {
        const std::uint32_t list = powerup_models::model_list(rdram, kind, 3, 0x5A33C500u,
                                                              kind == powerup_models::barrage_missile_kind ? powerup_models::flame_frame() : -1);
        if (list == 0 || slot >= 30) return;
        const std::uint32_t m = buffer + 64u * slot++;
        write_matrix(rdram, m, x, y, z, scale, at);
        append(rdram, 0x01040040u, m);  // G_MTX: modelview, multiply, push
        append(rdram, 0x06000000u, list);
        append(rdram, 0xBD000000u, 0);  // G_POPMTX
    };
    const V side = unit(cross(up, std::abs(up.x) < 0.9 ? V{1, 0, 0} : V{0, 1, 0})), side2 = cross(up, side);
    // (Declared before the launcher's position uses it.)

    if (running) {
        // The launcher (the owner, 2026-11-22: with the level 2 rocket's
        // presence): it flies in big from off the bottom-left corner, holds
        // there, near the camera, aimed at the strikes, fires, and leaves the
        // way it came. Screen directions on the sphere: moving right lowers
        // the board's x, up raises its y.
        // Worked out once as the scene starts and kept (the owner's
        // recording, frames 115-117: on a blast's frame the mesh read here
        // briefly went wrong and the launcher jumped to the screen's middle).
        static Clock::time_point frame_of{};
        static V base, right, screen_up, held_up;
        static double held_cell = 0.0;
        if (frame_of != started) {
            V n0;
            base = surface(cx, cy, 0.0, n0);
            right = unit(base - surface(cx + 1.0, cy, 0.0, n0));
            screen_up = unit(surface(cx, cy + 1.0, 0.0, n0) - base);
            held_up = up, held_cell = cell, frame_of = started;
        }
        const V& up = held_up;
        const double cell = held_cell;
        static const double hold_h = [] { const char* v = std::getenv("BLOCKPLANET_LAUNCHER_H"); return v ? std::atof(v) : 5.0; }();
        static const double hold_a = [] { const char* v = std::getenv("BLOCKPLANET_LAUNCHER_A"); return v ? std::atof(v) : 3.4; }();
        static const double hold_b = [] { const char* v = std::getenv("BLOCKPLANET_LAUNCHER_B"); return v ? std::atof(v) : 1.2; }();
        static const double size = [] { const char* v = std::getenv("BLOCKPLANET_LAUNCHER_SIZE"); return v ? std::atof(v) : 2.4; }();
        const V hold = base + up * (hold_h * cell) - right * (hold_a * cell) - screen_up * (hold_b * cell);
        const V away = (right * -1.0 + screen_up * -0.7 + up * 0.4) * (16.0 * cell);
        const double launcher_scale = size * cell / 3600.0;
        const double leave = went_off ? std::clamp(after_hit / leave_seconds, 0.0, 1.0) : 0.0;
        const double arrive = std::clamp(t_now / arrive_seconds, 0.0, 1.0), coming = 1.0 - arrive;
        const V launcher_at = hold + away * (coming * coming * coming + leave * leave);
        // Aimed at the strikes' middle: its tubes' open ends (-z) towards it.
        const V lz = unit(launcher_at - base);
        const V lx = unit(cross(screen_up, lz)), ly = cross(lz, lx);
        place(t_now < release_seconds ? 4 : powerup_models::empty_launcher_kind, lx, ly, lz, launcher_scale, launcher_at);
        // (Level 3's held model is the loaded launcher: kind 4 at level 3.)

        // The missiles: from the launcher's tubes to their strike points; gone
        // once the blasts go off.
        if (t_now >= release_seconds && !went_off) {
            const double t = std::clamp((t_now - release_seconds) / (land_seconds - release_seconds), 0.0, 1.0);
            // Each leaves its tube along the tube, as it sat in the launcher,
            // and curves round to its strike point, turned along its path.
            const double u = t * (0.35 + 0.65 * t);  // slow out of the tube, faster on
            const double reach = length(launcher_at - base);
            // Which missile goes where (the owner, 2026-11-22: the left one
            // crossed the right one's path): each to the strike on its own side,
            // the pairing whose directions on screen (from the launcher's axis,
            // from the strikes' middle; across the view) agree best.
            V targets[3], normals[3], middle;
            for (int m = 0; m < 3; ++m) {
                double dx, dy;
                strike_offset(m, dx, dy);
                targets[m] = surface(cx + dx, cy + dy, 0.0, normals[m]);
                middle = middle + targets[m] * (1.0 / 3.0);
            }
            const auto flat = [&](V v) { return unit(v - up * dot(v, up)); };
            static const int pairings[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
            int best = 0;
            double best_score = -1e9;
            for (int q = 0; q < 6; ++q) {
                double score = 0;
                for (int n = 0; n < 3; ++n) {
                    const double a = 6.2831853 * double(n) / 3.0;
                    score += dot(flat(lx * std::cos(a) + ly * std::sin(a)), flat(targets[pairings[q][n]] - middle));
                }
                if (score > best_score) best_score = score, best = q;
            }
            for (int n = 0; n < 3; ++n) {
                const V normal = normals[pairings[best][n]];
                const V target = targets[pairings[best][n]];
                const double a = 6.2831853 * double(n) / 3.0;
                const V across = (lx * std::cos(a) + ly * std::sin(a)) * (double(powerup_models::launcher_missile_radius) * launcher_scale);
                const V p0 = launcher_at + across + lz * (300.0 * launcher_scale);  // where it sat in the launcher
                // Out of the tube, then down onto the target nose first (a
                // cubic curve).
                const V p1 = p0 - lz * (0.35 * reach);
                const V p2 = target + normal * (0.25 * reach);
                const V p3 = target;
                const double w = 1 - u;
                const V at = p0 * (w * w * w) + p1 * (3 * w * w * u) + p2 * (3 * w * u * u) + p3 * (u * u * u);
                const V tangent = (p1 - p0) * (3 * w * w) + (p2 - p1) * (6 * w * u) + (p3 - p2) * (3 * u * u);
                const V z = unit(tangent) * -1.0;  // our models' nose is -z
                const V x = unit(cross(z, std::abs(z.x) < 0.9 ? V{1, 0, 0} : V{0, 1, 0})), y = cross(z, x);
                // From the launcher's size down to a size that suits the sphere.
                place(powerup_models::barrage_missile_kind, x, y, z, 0.55 * launcher_scale * (1.0 - 0.45 * u), at);
            }
        }

        // Research: BLOCKPLANET_BARRAGE_MARKERS=1 stands a missile on each strike
        // point throughout, to check them against the blasts.
        static const bool markers = std::getenv("BLOCKPLANET_BARRAGE_MARKERS") != nullptr;
        if (markers)
            for (int n = 0; n < 3; ++n) {
                double dx, dy;
                strike_offset(n, dx, dy);
                V normal;
                const V target = surface(cx + dx, cy + dy, 1.0, normal);
                const V z = normal * -1.0;
                const V x = unit(cross(z, std::abs(z.x) < 0.9 ? V{1, 0, 0} : V{0, 1, 0})), y = cross(z, x);
                place(powerup_models::barrage_missile_kind, x, y, z, 1.1 * cell / 5900.0, target);
            }

    }

    // The sphere's state back for the passes that follow.
    if (numlight != 0) append(rdram, 0xBC000002u, numlight);
    for (const auto& l : lights)
        if (l[0] != 0) append(rdram, l[0], l[1]);
    append(rdram, 0xB6000000u, 0x001F3205u);
    append(rdram, 0xB7000000u, 0x00020205u);

    static const bool log = std::getenv("BLOCKPLANET_MODELS3D_LOG") != nullptr;  // research
    if (log && draw_count % 10 == 0) {
        std::fprintf(stdout, "{\"event\":\"barrage_scene\",\"t\":%.2f,\"hit\":%d,\"cell\":%.0f,\"hover\":[%.0f,%.0f,%.0f]}\n", t_now, int(went_off), cell,
                     hover.x, hover.y, hover.z);
        std::fflush(stdout);
    }
}

} // namespace tetrisphere::barrage
