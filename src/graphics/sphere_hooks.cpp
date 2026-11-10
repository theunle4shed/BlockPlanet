// Hooks that connect the generated game code to BlockPlanet's sphere renderer
// passes (stage 1 of the sphere renderer). Mode is BLOCKPLANET_SPHERE:
// original (default), verify or replace.
//
// func_800ADD6C (mesh table builder):
//   start:    blockplanet_sphere_mesh_begin — returns true to skip the original
//   epilogue: blockplanet_sphere_mesh_end — compares in verify mode
// func_8009D90C (cell setup):
//   start:    blockplanet_sphere_cell_begin — returns true to skip the original
//   epilogue: blockplanet_sphere_cell_end — compares in verify mode
// func_8009C330, func_8009C6D4, func_8009CA98, func_8009CE38, func_8009D1D4,
// func_8009D570 (faces):
//   start:    blockplanet_sphere_face_begin
//   epilogue: blockplanet_sphere_face_end
// func_8009E630, func_8009EB28, func_8009F024, func_8009F520 (quadrants):
//   start:    blockplanet_sphere_quadrant_begin
//   epilogue: blockplanet_sphere_quadrant_end
//
// Verify mode, for passes that only touch memory (mesh, cell, faces): BlockPlanet's
// version runs first on a private overlay of guest memory, the original runs,
// then every value BlockPlanet would have written is checked against what the
// original left in memory.
//
// Verify mode, for passes that call other game functions (quadrants): a
// snapshot of guest memory and registers is taken when the original starts.
// When it is about to return, BlockPlanet's version runs on a private copy
// of that snapshot, and the copy is compared with the original's results in
// guest memory (except the stack below the caller's frame, which holds
// scratch data). Live guest memory is never changed by the check.

#include "tetrisphere/escape_view.h"
#include "tetrisphere/recomp_support.h"
#include "tetrisphere/runtime.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/sphere_cell.h"
#include "tetrisphere/sphere_extended.h"
#include "tetrisphere/sphere_faces.h"
#include "tetrisphere/sphere_grid.h"
#include "tetrisphere/sphere_mesh.h"
#include "tetrisphere/sphere_quadrants.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using namespace tetrisphere::sphere;
using namespace tetrisphere::guest;

// Mode for one group of passes. In verify mode, BLOCKPLANET_SPHERE_VERIFY can
// list the groups to check (comma-separated: mesh, cell, faces, quadrants);
// the others then run as the original.
Mode mode_for(const char* group) {
    const Mode m = mode();
    if (m != Mode::Verify) return m;
    static const char* selection = std::getenv("BLOCKPLANET_SPHERE_VERIFY");
    if (selection == nullptr) return m;
    const std::size_t length = std::strlen(group);
    for (const char* p = selection; (p = std::strstr(p, group)) != nullptr; p += length) {
        const bool starts = p == selection || p[-1] == ',';
        const bool ends = p[length] == '\0' || p[length] == ',';
        if (starts && ends) return m;
    }
    return Mode::Original;
}

constexpr std::uint32_t mesh_pointer = 0x800E44E4u;
constexpr std::uint32_t base_grid = 0x800E3CD8u;
constexpr std::uint32_t base_row_bytes = 0x6C;
constexpr std::uint32_t active_context = 0x8013DD00u;
constexpr std::uint32_t player_count = 0x8015D980u;

// Guest memory read and written in place (replace mode).
class DirectMemory final : public GuestMemory {
public:
    explicit DirectMemory(std::uint8_t* rdram) : rdram_(rdram) {}
    std::uint8_t read8(std::uint32_t a) const override { return load8(rdram_, a); }
    std::int16_t read16(std::uint32_t a) const override { return load16(rdram_, a); }
    std::uint32_t read32(std::uint32_t a) const override { return load32(rdram_, a); }
    void write8(std::uint32_t a, std::uint8_t v) override { store8(rdram_, a, v); }
    void write16(std::uint32_t a, std::int16_t v) override { store16(rdram_, a, v); }
    void write32(std::uint32_t a, std::uint32_t v) override { store32(rdram_, a, v); }

private:
    std::uint8_t* rdram_;
};

// Guest memory with writes kept aside (verify mode): reads see earlier writes
// of the same pass, and guest memory itself is left untouched.
class OverlayMemory final : public GuestMemory {
public:
    struct Write {
        std::uint32_t address;
        std::uint32_t size;
        std::uint32_t value;
    };
    OverlayMemory() = default;

    std::uint8_t read8(std::uint32_t a) const override {
        return static_cast<std::uint8_t>(read(a, 1, load8(rdram_, a)));
    }
    std::int16_t read16(std::uint32_t a) const override {
        return static_cast<std::int16_t>(read(a, 2, static_cast<std::uint16_t>(load16(rdram_, a))));
    }
    std::uint32_t read32(std::uint32_t a) const override { return read(a, 4, load32(rdram_, a)); }
    void write8(std::uint32_t a, std::uint8_t v) override { record(a, 1, v); }
    void write16(std::uint32_t a, std::int16_t v) override { record(a, 2, static_cast<std::uint16_t>(v)); }
    void write32(std::uint32_t a, std::uint32_t v) override { record(a, 4, v); }

    // Final value of every address written, one entry per address.
    const std::vector<Write>& writes() const { return writes_; }
    // Set when a read or write partly overlapped an earlier write of another
    // size, which the overlay cannot model; the check then counts as failed.
    bool overlapped() const { return overlapped_; }
    // Starts a new pass over `rdram` (live memory, or a snapshot check's copy).
    void reset(const std::uint8_t* rdram) {
        rdram_ = rdram;
        writes_.clear();
        overlapped_ = false;
    }

private:
    std::uint32_t read(std::uint32_t a, std::uint32_t size, std::uint32_t from_memory) const {
        for (const Write& w : writes_) {
            if (w.address == a && w.size == size) return w.value;
            if (a < w.address + w.size && w.address < a + size) overlapped_ = true;
        }
        return from_memory;
    }
    void record(std::uint32_t a, std::uint32_t size, std::uint32_t v) {
        for (Write& w : writes_) {
            if (w.address == a && w.size == size) {
                w.value = v;
                return;
            }
            if (a < w.address + w.size && w.address < a + size) overlapped_ = true;
        }
        writes_.push_back({a, size, v});
    }

    const std::uint8_t* rdram_ = nullptr;
    std::vector<Write> writes_;
    mutable bool overlapped_ = false;
};

// A word the original adds to in the branch delay slot of its return, which
// runs after the epilogue hook: when checking, the original's value is taken
// as already increased.
struct DelaySlotAdd {
    std::uint32_t address = 0;
    std::uint32_t amount = 0;
};

// Logs the first few differing values of the first few mismatched checks.
std::size_t count_differences(const std::uint8_t* rdram, const OverlayMemory& overlay, const char* event,
                              DelaySlotAdd delay_slot = {}) {
    static std::atomic<int> detailed_checks{0};
    std::size_t differing = overlay.overlapped() ? 1 : 0;
    bool detail = false;
    for (const OverlayMemory::Write& w : overlay.writes()) {
        std::uint32_t actual = 0;
        switch (w.size) {
        case 1: actual = load8(rdram, w.address); break;
        case 2: actual = static_cast<std::uint16_t>(load16(rdram, w.address)); break;
        default: actual = load32(rdram, w.address); break;
        }
        if (w.size == 4 && w.address == delay_slot.address) actual += delay_slot.amount;
        if (actual == w.value) continue;
        if (differing++ == 0) detail = detailed_checks++ < 3;
        if (detail && differing <= 40) {
            std::fprintf(stdout,
                         "{\"event\":\"%s_detail\",\"address\":\"0x%08X\",\"size\":%u,"
                         "\"blockplanet\":%u,\"original\":%u}\n",
                         event, w.address, w.size, w.value, actual);
        }
    }
    return differing;
}

// Counts checks and reports every mismatch, plus checks 1, 2, 4, 8, ...
class VerifyLog {
public:
    VerifyLog(const char* event, const char* unit) : event_(event), unit_(unit) {}
    // `outside_total`, when given, is the running count of differences
    // attributed to other threads (snapshot checks only).
    void report(std::size_t differing, long long outside_total = -1) {
        const auto n = ++calls_;
        if (differing != 0) ++mismatched_calls_;
        if (differing == 0 && (n & (n - 1)) != 0) return;
        std::fprintf(stdout, "{\"event\":\"%s\",\"calls\":%llu,\"mismatched_calls\":%llu,\"differing_%s\":%zu",
                     event_, static_cast<unsigned long long>(n),
                     static_cast<unsigned long long>(mismatched_calls_.load()), unit_, differing);
        if (outside_total >= 0) std::fprintf(stdout, ",\"outside_bytes_total\":%lld", outside_total);
        std::fprintf(stdout, "}\n");
        std::fflush(stdout);
    }

private:
    const char* event_;
    const char* unit_;
    std::atomic<std::uint64_t> calls_{0};
    std::atomic<std::uint64_t> mismatched_calls_{0};
};

// Overlay check for passes that only touch memory (see the top). Also runs
// BlockPlanet's version directly in replace mode.
class OverlayCheck {
public:
    OverlayCheck(const char* group, const char* event, DelaySlotAdd delay_slot = {})
        : group_(group), event_(event), delay_slot_(delay_slot), log_(event, "values") {}

    // Returns true when BlockPlanet's version replaced the original.
    template <typename Port>
    bool begin(std::uint8_t* rdram, Port&& port) {
        const Mode m = mode_for(group_);
        if (m == Mode::Original) return false;
        if (m == Mode::Replace) {
            DirectMemory memory(rdram);
            port(memory);
            return true;
        }
        if (!overlay_) overlay_ = std::make_unique<OverlayMemory>();
        overlay_->reset(rdram);
        port(*overlay_);
        pending_ = true;
        return false;
    }

    void end(const std::uint8_t* rdram) {
        if (!pending_) return;
        pending_ = false;
        log_.report(count_differences(rdram, *overlay_, event_, delay_slot_));
    }

private:
    const char* group_;
    const char* event_;
    DelaySlotAdd delay_slot_;
    VerifyLog log_;
    std::unique_ptr<OverlayMemory> overlay_;
    bool pending_ = false;
};

// Snapshot check for passes that call other game functions (see the top).
//
// The check must stay quick: while it runs the game thread is stalled, and a
// long stall lets the runtime's message queues (retrace, display list done)
// overflow, which loses messages and hangs the game. So it copies guest
// memory once, compares 4 KB pages with memcmp, and the checks are spread
// out (see SnapshotSchedule).
class SnapshotCheck {
public:
    explicit SnapshotCheck(const char* event) : log_(event, "bytes"), event_(event) {}

    void begin(const std::uint8_t* rdram, const recomp_context* ctx, bool selected) {
        active_ = selected;
        if (!active_) return;
        snapshot_.assign(rdram, rdram + rdram_bytes);
        entry_ctx_ = *ctx;
    }

    template <typename Port>
    void end(const std::uint8_t* rdram, Port&& port) {
        if (!active_) return;
        active_ = false;
        const auto started = std::chrono::steady_clock::now();

        // Pages the original changed (it has finished; the snapshot still
        // holds the starting state).
        for (std::uint32_t page = 0; page < pages; ++page) {
            changed_[page] = std::memcmp(rdram + page * page_bytes, snapshot_.data() + page * page_bytes,
                                         page_bytes) != 0;
        }

        // BlockPlanet's version runs on the snapshot, a private copy; live
        // memory is never touched. (Other host threads post messages into
        // queues in guest memory, so writing a snapshot back over live memory
        // could lose one.) The game's functions reach memory only through the
        // rdram pointer they are given, so the ones the port calls work on the
        // copy too.
        recomp_context shadow_ctx = entry_ctx_;
        port(snapshot_.data(), &shadow_ctx);
        const std::uint8_t* result = snapshot_.data();

        const std::uint32_t stack_top = static_cast<std::uint32_t>(entry_ctx_.r29) & 0x00FFFFFFu;
        const std::uint32_t stack_bottom = stack_top > stack_scratch ? stack_top - stack_scratch : 0;
        // Other host threads (the audio task, RT64's framebuffer copies) can
        // write guest memory while a check runs. Differences in pages the
        // pass has never written in a clean check are counted separately as
        // "outside" and do not fail the check.
        std::size_t differing = 0;
        std::size_t outside = 0;
        int detailed = 0;
        for (std::uint32_t page = 0; page < pages; ++page) {
            const std::uint32_t base = page * page_bytes;
            if (std::memcmp(rdram + base, result + base, page_bytes) == 0) continue;
            for (std::uint32_t offset = base; offset < base + page_bytes; ++offset) {
                if (result[offset] == rdram[offset]) continue;
                if (offset >= stack_bottom && offset < stack_top) continue;
                if (!pass_pages_[page]) {
                    ++outside;
                    continue;
                }
                if (differing++ == 0) detail_ = detailed_checks_++ < 3;
                if (detail_ && detailed++ < 40) {
                    std::fprintf(stdout,
                                 "{\"event\":\"%s_detail\",\"address\":\"0x%08X\",\"blockplanet\":%u,"
                                 "\"original\":%u}\n",
                                 event_, 0x80000000u | (offset ^ 3u), result[offset], rdram[offset]);
                }
            }
        }
        outside_bytes_ += outside;
        if (differing == 0 && outside == 0) {
            for (std::uint32_t page = 0; page < pages; ++page) pass_pages_[page] = pass_pages_[page] || changed_[page];
        }
        log_.report(differing, static_cast<long long>(outside_bytes_));
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (ms > slowest_ms_) {
            slowest_ms_ = ms;
            std::fprintf(stdout, "{\"event\":\"%s_slowest\",\"ms\":%.1f}\n", event_, ms);
        }
    }

private:
    static constexpr std::uint32_t rdram_bytes = 8u * 1024u * 1024u;
    static constexpr std::uint32_t page_bytes = 4096;
    static constexpr std::uint32_t pages = rdram_bytes / page_bytes;
    static constexpr std::uint32_t stack_scratch = 0x10000u;

    VerifyLog log_;
    const char* event_;
    std::vector<std::uint8_t> snapshot_;
    recomp_context entry_ctx_{};
    bool active_ = false;
    bool detail_ = false;
    int detailed_checks_ = 0;
    std::vector<bool> changed_ = std::vector<bool>(pages, false);
    std::vector<bool> pass_pages_ = std::vector<bool>(pages, false);
    std::uint64_t outside_bytes_ = 0;
    double slowest_ms_ = 0;
};

// Picks which calls of a group of snapshot-checked passes are checked: one
// call in `interval`, counted across the group, so that a group of four
// passes run once per frame gets one check every few frames and each pass in
// turn (with an interval not divisible by four).
class SnapshotSchedule {
public:
    explicit SnapshotSchedule(std::uint64_t interval) : interval_(interval) {}
    bool next() { return calls_++ % interval_ == 0; }

private:
    std::uint64_t interval_;
    std::uint64_t calls_ = 0;
};

// ---- mesh table builder -------------------------------------------------

MeshInputs gather(std::uint8_t* rdram, recomp_context* ctx, bool apply_caps) {
    MeshInputs in;
    in.row_fraction = static_cast<std::int16_t>(ctx->r4);
    in.column_fraction = static_cast<std::int16_t>(ctx->r5);
    in.base = [rdram](int row, int column) {
        const std::uint32_t a = base_grid + static_cast<std::uint32_t>(row) * base_row_bytes +
                                static_cast<std::uint32_t>(column) * 6u;
        return std::array<std::int32_t, 3>{load16(rdram, a), load16(rdram, a + 2), load16(rdram, a + 4)};
    };
    const std::uint32_t player = load32(rdram, active_context);
    for (int axis = 0; axis < 3; ++axis) {
        const std::uint32_t a = player + 0x247Au + 2u * static_cast<std::uint32_t>(axis);
        std::int16_t value = load16(rdram, a);
        if (value > 0x4000) {
            value = 0x4000;
            if (apply_caps) store16(rdram, a, value);  // the original caps these in place
        }
        in.scale[axis] = value;
    }
    in.scale_enabled = load16(rdram, player + 0x2478u) != 0;
    // Framing (sphere_mesh.h, set_view_scale): on top of the game's own scale.
    if (const double size = view_scale(); size != 1.0) {
        for (int axis = 0; axis < 3; ++axis) {
            const double base = in.scale_enabled ? in.scale[axis] : 0x4000;
            in.scale[axis] = static_cast<std::int16_t>(std::lround(base * size));
        }
        in.scale_enabled = true;
    }
    in.two_player = load16(rdram, player_count) >= 2;
    in.player_one = load16(rdram, player + 0x23B2u) == 1;
    return in;
}

void store_mesh(std::uint8_t* rdram, const std::vector<MeshEntry>& mesh) {
    const std::uint32_t table = load32(rdram, mesh_pointer);
    for (std::size_t i = 0; i < mesh.size(); ++i) {
        const std::uint32_t a = table + static_cast<std::uint32_t>(i) * 12u;
        for (int axis = 0; axis < 3; ++axis) {
            store16(rdram, a + 2u * static_cast<std::uint32_t>(axis), mesh[i].outer[axis]);
            store16(rdram, a + 6u + 2u * static_cast<std::uint32_t>(axis), mesh[i].inner[axis]);
        }
    }
}

std::vector<MeshEntry> expected_mesh;
VerifyLog mesh_log{"sphere_mesh_verify", "entries"};

// ---- cell setup ---------------------------------------------------------

OverlayCheck cell_check{"cell", "sphere_cell_verify"};

// ---- face functions -----------------------------------------------------

// Every face function adds 4 to the vertex count at 0x800E44F0 in its return's
// delay slot.
constexpr DelaySlotAdd face_vertex_count{0x800E44F0u, 4};
std::array<OverlayCheck, 6> face_checks{OverlayCheck{"faces", "sphere_face_outer_verify", face_vertex_count},
                                        OverlayCheck{"faces", "sphere_face_inner_verify", face_vertex_count},
                                        OverlayCheck{"faces", "sphere_face_row_start_verify", face_vertex_count},
                                        OverlayCheck{"faces", "sphere_face_row_end_verify", face_vertex_count},
                                        OverlayCheck{"faces", "sphere_face_column_start_verify", face_vertex_count},
                                        OverlayCheck{"faces", "sphere_face_column_end_verify", face_vertex_count}};

// ---- quadrant passes ----------------------------------------------------

SnapshotSchedule quadrant_schedule{9};
std::array<SnapshotCheck, 4> quadrant_checks{SnapshotCheck{"sphere_quadrant0_verify"},
                                             SnapshotCheck{"sphere_quadrant1_verify"},
                                             SnapshotCheck{"sphere_quadrant2_verify"},
                                             SnapshotCheck{"sphere_quadrant3_verify"}};

// ---- extended patch (BLOCKPLANET_SPHERE_RING, replace mode) --------------

ExtendedSphere extended_sphere;
bool extended_ready = false;  // built this frame, single player

bool extended_test() {
    static const bool on = std::getenv("BLOCKPLANET_SPHERE_EXTENDED_TEST") != nullptr;
    return on;
}

// The game's base grid table, read from guest memory.
BaseTable base_table(const std::uint8_t* rdram) {
    return [rdram](int row, int column) {
        const std::uint32_t a =
            base_grid + static_cast<std::uint32_t>(row) * base_row_bytes + static_cast<std::uint32_t>(column) * 6u;
        return std::array<std::int16_t, 3>{load16(rdram, a), load16(rdram, a + 2), load16(rdram, a + 4)};
    };
}

// The extended patch's vertices live above 4 MB, which the game never uses:
// one buffer per game frame buffer, 2 MB each (131,072 vertices).
constexpr std::array<std::uint32_t, 2> extended_vertex_buffers{0x80400000u, 0x80600000u};
constexpr std::uint32_t vertex_pool_frame_start = 0x8013DCFCu;
constexpr std::uint32_t face_vertex_pointer = 0x8015F848u;
constexpr std::uint32_t face_batch_count = 0x8015F84Eu;
constexpr std::uint32_t face_vertex_total = 0x800E44F0u;
constexpr std::int16_t full_batch = 16;  // makes the next face load a fresh batch
std::uint32_t game_vertex_pointer = 0;

void begin_extended_vertices(std::uint8_t* rdram) {
    static std::uint32_t first_pool = 0;
    const std::uint32_t pool = load32(rdram, vertex_pool_frame_start);
    if (first_pool == 0) first_pool = pool;
    game_vertex_pointer = load32(rdram, face_vertex_pointer);
    store32(rdram, face_vertex_pointer, extended_vertex_buffers[pool == first_pool ? 0 : 1]);
    store16(rdram, face_batch_count, full_batch);
}

void end_extended_vertices(std::uint8_t* rdram) {
    // Later passes continue in the game's pool, from where it was, and the
    // pool is only moved on by their vertices.
    store32(rdram, face_vertex_pointer, game_vertex_pointer);
    store16(rdram, face_batch_count, full_batch);
    store32(rdram, face_vertex_total, 0);
}

} // namespace

extern "C" bool blockplanet_sphere_mesh_begin(std::uint8_t* rdram, recomp_context* ctx) {
    latch_frame_mode();
    extended_ready = false;
    if (const int ring = frame_ring(); ring > 0 && load16(rdram, player_count) < 2) {
        static const ExtendedBaseGrid grid(base_table(rdram), ring);
        extended_sphere.build(ring, gather(rdram, ctx, false), grid);
        extended_ready = true;
    }
    // BLOCKPLANET_SPHERE_EXTENDED_TEST=1 in verify mode checks the extended
    // path with no extra cells against the game's quadrant passes.
    if (extended_test() && mode() == Mode::Verify) {
        static const ExtendedBaseGrid exact_grid(base_table(rdram), 0, true);
        extended_sphere.build(0, gather(rdram, ctx, false), exact_grid);
    }
    const Mode m = mode_for("mesh");
    if (m == Mode::Original) return false;
    if (m == Mode::Verify) {
        // Inputs are read before the original caps the scale values, so read
        // them the same way without writing.
        build_mesh(gather(rdram, ctx, false), expected_mesh);
        return false;
    }
    // Experiment: BLOCKPLANET_SPHERE_CORE_SCALE (replace mode) resizes the
    // core, keeping the outer surface in place.
    static const double core_scale = [] {
        const char* value = std::getenv("BLOCKPLANET_SPHERE_CORE_SCALE");
        const double v = value != nullptr ? std::atof(value) : 1.0;
        return v >= 0.5 && v <= 2.0 ? v : 1.0;
    }();
    MeshInputs inputs = gather(rdram, ctx, true);
    inputs.core_scale = core_scale;
    std::vector<MeshEntry> mesh;
    build_mesh(inputs, mesh);
    store_mesh(rdram, mesh);
    return true;
}

// Once per run in verify mode: how far the grid formula is from the game's
// base grid table (expected: under 50 units).
void check_grid_formula(const std::uint8_t* rdram) {
    static bool done = false;
    if (done) return;
    done = true;
    int largest = 0, compared = 0, clamped = 0;
    for (int row = 0; row < base_table_rows; ++row) {
        for (int column = 0; column < base_table_columns; ++column) {
            const std::uint32_t a = base_grid + static_cast<std::uint32_t>(row) * base_row_bytes +
                                    static_cast<std::uint32_t>(column) * 6u;
            const std::array<std::int16_t, 3> value{load16(rdram, a), load16(rdram, a + 2), load16(rdram, a + 4)};
            if (base_table_value_clamped(value)) {
                ++clamped;
                continue;
            }
            const auto f = grid_formula_point(row, column);
            for (int axis = 0; axis < 3; ++axis) {
                largest = std::max(largest, static_cast<int>(std::lround(std::fabs(f[axis] - value[axis]))));
            }
            ++compared;
        }
    }
    std::fprintf(stdout, "{\"event\":\"sphere_grid_formula_check\",\"compared\":%d,\"clamped\":%d,\"largest_difference\":%d}\n",
                 compared, clamped, largest);
    std::fflush(stdout);
}

extern "C" void blockplanet_sphere_mesh_end(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (mode_for("mesh") == Mode::Verify) check_grid_formula(rdram);
    if (mode_for("mesh") != Mode::Verify || expected_mesh.empty()) return;
    const std::uint32_t table = load32(rdram, mesh_pointer);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < expected_mesh.size(); ++i) {
        const std::uint32_t a = table + static_cast<std::uint32_t>(i) * 12u;
        for (int axis = 0; axis < 3; ++axis) {
            if (load16(rdram, a + 2u * static_cast<std::uint32_t>(axis)) != expected_mesh[i].outer[axis] ||
                load16(rdram, a + 6u + 2u * static_cast<std::uint32_t>(axis)) != expected_mesh[i].inner[axis]) {
                ++differing;
                break;
            }
        }
    }
    mesh_log.report(differing);
}

extern "C" bool blockplanet_sphere_cell_begin(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    return cell_check.begin(rdram, [](GuestMemory& memory) { prepare_cell(memory); });
}

extern "C" void blockplanet_sphere_cell_end(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    cell_check.end(rdram);
}

extern "C" bool blockplanet_sphere_face_begin(std::uint8_t* rdram, recomp_context* ctx, int face) {
    (void)ctx;
    return face_checks[static_cast<std::size_t>(face)].begin(
        rdram, [face](GuestMemory& memory) { draw_face(memory, static_cast<Face>(face)); });
}

extern "C" void blockplanet_sphere_face_end(std::uint8_t* rdram, recomp_context* ctx, int face) {
    (void)ctx;
    face_checks[static_cast<std::size_t>(face)].end(rdram);
}

// ---- usage statistics (BLOCKPLANET_SPHERE_STATS=1) ------------------------
//
// How much of the frame's display list and vertex pool the sphere uses, for
// sizing the larger patches of stage 2. One line every 300 graphics tasks.

namespace {

constexpr std::uint32_t display_list_current = 0x800F22B4u;
constexpr std::uint32_t display_list_start = 0x800F22B8u;
constexpr std::uint32_t vertex_pool_current = 0x8013DCF8u;
constexpr std::uint32_t vertex_pool_start = 0x8013DCFCu;
constexpr std::uint32_t sphere_vertex_pointer = 0x8015F848u;

struct SphereStats {
    bool enabled = std::getenv("BLOCKPLANET_SPHERE_STATS") != nullptr;
    std::uint32_t frame_dl_at_sphere = 0;
    std::uint32_t frame_vertices_at_sphere = 0;
    // Maximums over the window.
    std::uint32_t sphere_dl_bytes = 0;
    std::uint32_t sphere_vertex_bytes = 0;
    std::uint32_t frame_dl_bytes = 0;
    std::uint32_t frame_vertex_bytes = 0;
    // Ranges of the buffers seen.
    std::uint32_t lowest_dl = 0xFFFFFFFFu, highest_dl = 0;
    std::uint32_t lowest_vertex = 0xFFFFFFFFu, highest_vertex = 0;
    // Distinct buffer starts (double buffering gives two of each).
    std::array<std::uint32_t, 4> dl_starts{};
    std::array<std::uint32_t, 4> vertex_starts{};
    unsigned tasks = 0;
};
SphereStats stats;

void note_start(std::array<std::uint32_t, 4>& starts, std::uint32_t start) {
    for (auto& s : starts) {
        if (s == start) return;
        if (s == 0) {
            s = start;
            return;
        }
    }
}

void note_range(std::uint32_t& low, std::uint32_t& high, std::uint32_t from, std::uint32_t to) {
    if (from < low) low = from;
    if (to > high) high = to;
}


void record_gfx_task(const std::uint8_t* rdram) {
    const std::uint32_t dl_start = load32(rdram, display_list_start);
    const std::uint32_t dl_end = load32(rdram, display_list_current);
    const std::uint32_t pool_start = load32(rdram, vertex_pool_start);
    const std::uint32_t pool_end = load32(rdram, vertex_pool_current);
    if (dl_end >= dl_start && dl_end - dl_start < 0x100000u) {
        stats.frame_dl_bytes = std::max(stats.frame_dl_bytes, dl_end - dl_start);
        note_range(stats.lowest_dl, stats.highest_dl, dl_start, dl_end);
        note_start(stats.dl_starts, dl_start);
    }
    if (pool_end >= pool_start && pool_end - pool_start < 0x100000u) {
        stats.frame_vertex_bytes = std::max(stats.frame_vertex_bytes, pool_end - pool_start);
        note_range(stats.lowest_vertex, stats.highest_vertex, pool_start, pool_end);
        note_start(stats.vertex_starts, pool_start);
    }
    if (++stats.tasks % 300 != 0) return;
    std::fprintf(stdout,
                 "{\"event\":\"sphere_usage\",\"sphere_dl_bytes\":%u,\"sphere_vertex_bytes\":%u,"
                 "\"frame_dl_bytes\":%u,\"frame_vertex_bytes\":%u,"
                 "\"dl_range\":[\"0x%08X\",\"0x%08X\"],\"vertex_range\":[\"0x%08X\",\"0x%08X\"]}\n",
                 stats.sphere_dl_bytes, stats.sphere_vertex_bytes, stats.frame_dl_bytes, stats.frame_vertex_bytes,
                 stats.lowest_dl, stats.highest_dl, stats.lowest_vertex, stats.highest_vertex);
    std::fprintf(stdout, "{\"event\":\"sphere_usage_buffers\",\"dl_starts\":[\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\"],"
                 "\"vertex_starts\":[\"0x%08X\",\"0x%08X\",\"0x%08X\",\"0x%08X\"]}\n",
                 stats.dl_starts[0], stats.dl_starts[1], stats.dl_starts[2], stats.dl_starts[3],
                 stats.vertex_starts[0], stats.vertex_starts[1], stats.vertex_starts[2], stats.vertex_starts[3]);
    std::fflush(stdout);
    stats = SphereStats{};
}

// Registers with the runtime when the statistics are turned on.
[[maybe_unused]] const bool stats_registered = [] {
    if (stats.enabled) tetrisphere::set_gfx_task_observer(record_gfx_task);
    return stats.enabled;
}();

void stats_sphere_begin(const std::uint8_t* rdram) {
    if (!stats.enabled) return;
    stats.frame_dl_at_sphere = load32(rdram, display_list_current);
    stats.frame_vertices_at_sphere = load32(rdram, sphere_vertex_pointer);
}
void stats_sphere_end(const std::uint8_t* rdram) {
    if (!stats.enabled) return;
    stats.sphere_dl_bytes = std::max(stats.sphere_dl_bytes, load32(rdram, display_list_current) - stats.frame_dl_at_sphere);
    stats.sphere_vertex_bytes =
        std::max(stats.sphere_vertex_bytes, load32(rdram, sphere_vertex_pointer) - stats.frame_vertices_at_sphere);
}
} // namespace

// Game functions for the quadrant self-test (see blockplanet_sphere_quadrant_end).
extern "C" {
void func_8009E630(std::uint8_t* rdram, recomp_context* ctx);
void func_8009EB28(std::uint8_t* rdram, recomp_context* ctx);
void func_8009F024(std::uint8_t* rdram, recomp_context* ctx);
void func_8009F520(std::uint8_t* rdram, recomp_context* ctx);
}

namespace {
// Set while a snapshot check replays the original itself (self-test), so its
// own hooks stay out of the way.
bool replaying_original = false;
} // namespace

extern "C" bool blockplanet_sphere_quadrant_begin(std::uint8_t* rdram, recomp_context* ctx, int quadrant) {
    if (replaying_original) return false;
    // Research: BLOCKPLANET_HIDE_SPHERE=1 draws no sphere at all (its
    // quarters are skipped), for stills of the background alone.
    static const bool hide = std::getenv("BLOCKPLANET_HIDE_SPHERE") != nullptr;
    if (hide) return true;
    // Research: BLOCKPLANET_ESCAPE_VIEW=1, the Escape mode's view from inside
    // (src/graphics/escape_view.cpp) in place of the sphere.
    if (tetrisphere::escape_view::enabled()) {
        static bool drawn = false;
        if (quadrant == 0) {
            drawn = tetrisphere::escape_view::begin(rdram);
            if (drawn) {
                draw_inside(rdram, ctx);
                tetrisphere::escape_view::end(rdram);
            }
        }
        if (drawn) return true;
    }
    if (quadrant == 0) stats_sphere_begin(rdram);
    const Mode m = mode_for("quadrants");
    if (m == Mode::Original) return false;
    if (m == Mode::Verify) {
        quadrant_checks[static_cast<std::size_t>(quadrant)].begin(rdram, ctx, quadrant_schedule.next());
        return false;
    }
    if (extended_ready) {
        if (quadrant == 0) begin_extended_vertices(rdram);
        draw_extended_quadrant(rdram, ctx, static_cast<Quadrant>(quadrant), extended_sphere);
        if (quadrant == 3) {
            stats_sphere_end(rdram);
            end_extended_vertices(rdram);
        }
        return true;
    }
    draw_quadrant(rdram, ctx, static_cast<Quadrant>(quadrant));
    if (quadrant == 3) stats_sphere_end(rdram);  // the end hook does not run when replaced
    return true;
}

extern "C" void blockplanet_sphere_quadrant_end(std::uint8_t* rdram, recomp_context* ctx, int quadrant) {
    (void)ctx;
    if (replaying_original) return;
    if (quadrant == 3) stats_sphere_end(rdram);
    if (mode_for("quadrants") != Mode::Verify) return;
    // BLOCKPLANET_SPHERE_SELFTEST=1 replays the original instead of
    // BlockPlanet's version, to measure how often the check itself differs
    // (for example because of other host threads).
    static const bool self_test = std::getenv("BLOCKPLANET_SPHERE_SELFTEST") != nullptr;
    quadrant_checks[static_cast<std::size_t>(quadrant)].end(rdram, [quadrant](std::uint8_t* r, recomp_context* c) {
        if (extended_test()) return draw_extended_quadrant(r, c, static_cast<Quadrant>(quadrant), extended_sphere);
        if (!self_test) return draw_quadrant(r, c, static_cast<Quadrant>(quadrant));
        static constexpr void (*originals[4])(std::uint8_t*, recomp_context*) = {
            func_8009E630, func_8009EB28, func_8009F024, func_8009F520};
        replaying_original = true;
        originals[quadrant](r, c);
        replaying_original = false;
    });
}
