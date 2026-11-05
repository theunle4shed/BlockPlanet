// The game's audio settings, applied the way its Audio page does
// (docs/research/blockplanet-game-state.md, Audio):
//   0x800E0B1C  balance, 0..12        0x800E0B20  selection: 0 auto, 1 random, 2 choose
//   0x800E0B24  track, 0..22          0x80102B08  save byte: selection << 4 | balance
// A balance change sets the music volume, min(0x4FFF, 0xA008 - 3414 * b),
// through func_80079B80(volume, music player 0x80107290), and the effects
// volume, min(0x7FFF, 0x1556 * b), through func_80079BD4. A hook at the top
// of the controller and save thread's loop (func_80029938 at 0x800299A0),
// which runs continuously in every scene, calls blockplanet_audio_frame.
// The runtime runs one game thread at a time, so this cannot race the game.

#include "tetrisphere/audio_settings.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

extern "C" {
void func_80079B80(std::uint8_t* rdram, recomp_context* ctx);
void func_80079BD4(std::uint8_t* rdram, recomp_context* ctx);
}

namespace blockplanet {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t balance_address = 0x800E0B1Cu;
constexpr std::uint32_t selection_address = 0x800E0B20u;
constexpr std::uint32_t track_address = 0x800E0B24u;
constexpr std::uint32_t save_byte = 0x80102B08u;
constexpr std::uint32_t music_volume = 0x800E28E8u;
constexpr std::uint32_t music_player = 0x80107290u;

std::atomic<int> volume{100};
std::atomic<bool> wanted{false};
std::atomic<int> wanted_balance{6};
std::atomic<int> wanted_music{-2};

const char* const names[track_count] = {
    "Prophetic", "Azule Lux", "Martist", "Compliant Confuse", "Faze", "Flim Flam", "Manicdrummix", "Hallucid",
    "Perpetuate303", "Phony", "T. Grey", "Learn", "Zyp Zyp", "Extol", "Magic Fluter", "Spooky Baby",
    "Zooming Car", "Cheddar Funk", "Snowy Mushrooms", "Dog Robot", "Sappy", "Skank Bowl Fight", "Birthday Bumps"};

gpr address(std::uint32_t value) { return static_cast<gpr>(static_cast<std::int64_t>(static_cast<std::int32_t>(value))); }

// Calls a game function with two arguments, leaving the caller's registers as they were.
void call(void (*function)(std::uint8_t*, recomp_context*), std::uint8_t* rdram, recomp_context* ctx, gpr a0, gpr a1) {
    const recomp_context saved = *ctx;
    ctx->r4 = a0;
    ctx->r5 = a1;
    function(rdram, ctx);
    *ctx = saved;
}

} // namespace

void set_audio_volume(int percent) { volume.store(std::clamp(percent, 0, 100)); }
int audio_volume() { return volume.load(); }

void set_game_audio(int balance, int music) {
    wanted_balance.store(std::clamp(balance, 0, 12));
    wanted_music.store(std::clamp(music, -2, track_count - 1));
    wanted.store(true);
}

GameAudio read_game_audio(const unsigned char* rdram) {
    GameAudio audio;
    audio.balance = static_cast<int>(load32(rdram, balance_address));
    const auto selection = load32(rdram, selection_address);
    audio.music = selection == 0 ? -2 : selection == 1 ? -1 : static_cast<int>(load32(rdram, track_address));
    audio.balance = std::clamp(audio.balance, 0, 12);
    audio.music = std::clamp(audio.music, -2, track_count - 1);
    return audio;
}

const char* track_name(int track) { return track >= 0 && track < track_count ? names[track] : ""; }

} // namespace blockplanet

extern "C" void blockplanet_audio_frame(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace blockplanet;
    using namespace tetrisphere::guest;
    if (!wanted.load(std::memory_order_relaxed)) return;
    const auto balance = static_cast<std::uint32_t>(wanted_balance.load());
    const int music = wanted_music.load();
    const std::uint32_t selection = music == -2 ? 0 : music == -1 ? 1 : 2;
    bool changed = false;
    if (load32(rdram, selection_address) != selection) {
        store32(rdram, selection_address, selection);
        changed = true;
    }
    if (music >= 0 && load32(rdram, track_address) != static_cast<std::uint32_t>(music)) {
        store32(rdram, track_address, static_cast<std::uint32_t>(music));
        changed = true;
    }
    if (load32(rdram, balance_address) != balance) {
        store32(rdram, balance_address, balance);
        const auto music_level = std::min<std::uint32_t>(0x4FFF, 0xA008 - 3414 * balance);
        store16(rdram, music_volume, static_cast<std::int16_t>(music_level));
        call(func_80079B80, rdram, ctx, music_level, address(music_player));
        call(func_80079BD4, rdram, ctx, std::min<std::uint32_t>(0x7FFF, 0x1556 * balance), 0);
        changed = true;
    }
    if (changed) store8(rdram, save_byte, static_cast<std::uint8_t>(selection << 4 | balance));
}
