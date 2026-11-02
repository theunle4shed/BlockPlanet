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
//
// Verify mode runs BlockPlanet's version first on a private overlay of guest
// memory, lets the original run, then checks every value BlockPlanet would
// have written against what the original left in memory.

#include "tetrisphere/recomp_support.h"
#include "tetrisphere/sphere_cell.h"
#include "tetrisphere/sphere_mesh.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace tetrisphere::sphere;

constexpr std::uint32_t mesh_pointer = 0x800E44E4u;
constexpr std::uint32_t base_grid = 0x800E3CD8u;
constexpr std::uint32_t base_row_bytes = 0x6C;
constexpr std::uint32_t active_context = 0x8013DD00u;
constexpr std::uint32_t player_count = 0x8015D980u;

// RDRAM is held as host-order 32-bit words: bytes at address ^ 3, halfwords at
// address ^ 2.
std::uint8_t* host(std::uint8_t* rdram, std::uint32_t address) { return rdram + (address & 0x00FFFFFFu); }
const std::uint8_t* host(const std::uint8_t* rdram, std::uint32_t address) {
    return rdram + (address & 0x00FFFFFFu);
}
std::uint8_t load8(const std::uint8_t* rdram, std::uint32_t address) { return *host(rdram, address ^ 3u); }
std::int16_t load16(const std::uint8_t* rdram, std::uint32_t address) {
    std::int16_t v;
    std::memcpy(&v, host(rdram, address ^ 2u), 2);
    return v;
}
std::uint32_t load32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t v;
    std::memcpy(&v, host(rdram, address), 4);
    return v;
}
void store8(std::uint8_t* rdram, std::uint32_t address, std::uint8_t v) { *host(rdram, address ^ 3u) = v; }
void store16(std::uint8_t* rdram, std::uint32_t address, std::int16_t v) {
    std::memcpy(host(rdram, address ^ 2u), &v, 2);
}
void store32(std::uint8_t* rdram, std::uint32_t address, std::uint32_t v) {
    std::memcpy(host(rdram, address), &v, 4);
}

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
    explicit OverlayMemory(const std::uint8_t* rdram) : rdram_(rdram) {}

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
    void clear() {
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

    const std::uint8_t* rdram_;
    std::vector<Write> writes_;
    mutable bool overlapped_ = false;
};

// Logs the first few differing values of the first few mismatched checks.
std::size_t count_differences(const std::uint8_t* rdram, const OverlayMemory& overlay, const char* event) {
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
    void report(std::size_t differing) {
        const auto n = ++calls_;
        if (differing != 0) ++mismatched_calls_;
        if (differing == 0 && (n & (n - 1)) != 0) return;
        std::fprintf(stdout, "{\"event\":\"%s\",\"calls\":%llu,\"mismatched_calls\":%llu,\"differing_%s\":%zu}\n",
                     event_, static_cast<unsigned long long>(n),
                     static_cast<unsigned long long>(mismatched_calls_.load()), unit_, differing);
        std::fflush(stdout);
    }

private:
    const char* event_;
    const char* unit_;
    std::atomic<std::uint64_t> calls_{0};
    std::atomic<std::uint64_t> mismatched_calls_{0};
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

OverlayMemory* cell_overlay = nullptr;
VerifyLog cell_log{"sphere_cell_verify", "values"};

} // namespace

extern "C" bool blockplanet_sphere_mesh_begin(std::uint8_t* rdram, recomp_context* ctx) {
    const Mode m = mode();
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
    if (mode() != Mode::Verify || expected_mesh.empty()) return;
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
    const Mode m = mode();
    if (m == Mode::Original) return false;
    if (m == Mode::Verify) {
        static OverlayMemory overlay(rdram);
        overlay.clear();
        prepare_cell(overlay);
        cell_overlay = &overlay;
        return false;
    }
    DirectMemory memory(rdram);
    prepare_cell(memory);
    return true;
}

extern "C" void blockplanet_sphere_cell_end(std::uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (mode() != Mode::Verify || cell_overlay == nullptr) return;
    cell_log.report(count_differences(rdram, *cell_overlay, "sphere_cell_verify"));
}
