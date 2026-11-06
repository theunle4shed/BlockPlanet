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

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t letters_pointer = 0x800E1670u;
std::uint32_t letters_scene = 0;     // remembered from the intro
std::atomic<bool> drawing{false};    // set while BlockPlanet draws the letters

std::uint32_t load32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t v;
    std::memcpy(&v, rdram + (address & 0x00FFFFFFu), 4);
    return v;
}

} // namespace

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
