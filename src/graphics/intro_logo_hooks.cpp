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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

extern "C" void blockplanet_logo_scene_stepped(std::uint8_t* rdram, recomp_context* ctx) {
    if (!enabled()) return;
    const std::uint32_t scene = static_cast<std::uint32_t>(ctx->r16);
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
        std::fprintf(stdout, "{\"event\":\"intro_logo_held\",\"scene\":\"0x%08X\",\"from_frame\":%d,\"hold_frame\":%d,\"end_frame\":%d}\n",
                     scene, frame, hold_frame(), static_cast<int>(load32(rdram, load32(rdram, scene + 0x1Cu) + 0xCu)));
        std::fflush(stdout);
    }
    if (frame >= hold_frame() || held.size() != held_list().size() * state_size) return;
    for (std::size_t i = 0; i < held_list().size(); ++i)
        std::memcpy(host(rdram, objects + std::uint32_t(held_list()[i]) * object_size + state_offset), held.data() + i * state_size, state_size);
}
