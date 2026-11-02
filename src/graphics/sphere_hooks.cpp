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

#include "tetrisphere/recomp_support.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/sphere_cell.h"
#include "tetrisphere/sphere_faces.h"
#include "tetrisphere/sphere_mesh.h"
#include "tetrisphere/sphere_quadrants.h"

#include <array>
#include <chrono>
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
        return std::array<std::int16_t, 3>{load16(rdram, a), load16(rdram, a + 2), load16(rdram, a + 4)};
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

} // namespace

extern "C" bool blockplanet_sphere_mesh_begin(std::uint8_t* rdram, recomp_context* ctx) {
    const Mode m = mode_for("mesh");
    if (m == Mode::Original) return false;
    if (m == Mode::Verify) {
        // Inputs are read before the original caps the scale values, so read
        // them the same way without writing.
        build_mesh(gather(rdram, ctx, false), expected_mesh);
        return false;
    }
    std::vector<MeshEntry> mesh;
    build_mesh(gather(rdram, ctx, true), mesh);
    store_mesh(rdram, mesh);
    return true;
}

extern "C" void blockplanet_sphere_mesh_end(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
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
    const Mode m = mode_for("quadrants");
    if (m == Mode::Original) return false;
    if (m == Mode::Verify) {
        quadrant_checks[static_cast<std::size_t>(quadrant)].begin(rdram, ctx, quadrant_schedule.next());
        return false;
    }
    draw_quadrant(rdram, ctx, static_cast<Quadrant>(quadrant));
    return true;
}

extern "C" void blockplanet_sphere_quadrant_end(std::uint8_t* rdram, recomp_context* ctx, int quadrant) {
    (void)ctx;
    if (replaying_original || mode_for("quadrants") != Mode::Verify) return;
    // BLOCKPLANET_SPHERE_SELFTEST=1 replays the original instead of
    // BlockPlanet's version, to measure how often the check itself differs
    // (for example because of other host threads).
    static const bool self_test = std::getenv("BLOCKPLANET_SPHERE_SELFTEST") != nullptr;
    quadrant_checks[static_cast<std::size_t>(quadrant)].end(rdram, [quadrant](std::uint8_t* r, recomp_context* c) {
        if (!self_test) return draw_quadrant(r, c, static_cast<Quadrant>(quadrant));
        static constexpr void (*originals[4])(std::uint8_t*, recomp_context*) = {
            func_8009E630, func_8009EB28, func_8009F024, func_8009F520};
        replaying_original = true;
        originals[quadrant](r, c);
        replaying_original = false;
    });
}
