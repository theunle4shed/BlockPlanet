// The game's audio, driven the way its Audio page does
// (docs/research/blockplanet-game-state.md, Audio settings):
//   0x800E0B1C  balance, 0..12        0x800E0B20  selection: 0 auto, 1 random, 2 choose
//   0x800E0B24  track, 0..22          0x80102B08  save bytes: selection << 4 | balance, track
// The page sets one balance; BlockPlanet sets the two levels it controls
// separately: the music volume through func_80079B80(volume, music player
// 0x80107290) (0x800E28E8 holds it, at most 0x4FFF) and the effects volume
// through func_80079BD4(volume) (0x800E28EC, at most 0x7FFF). A track is
// played as the page does: flag 0x800DFE84, func_8007F720(track, player),
// func_8007ACFC(player).
//
// blockplanet_audio_frame runs from a hook at the top of the controller and
// save thread's loop (func_80029938 at 0x800299A0), about 60 times a second
// in every scene; the runtime runs one game thread at a time.

#include "tetrisphere/audio_settings.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

extern "C" {
void func_80079B80(std::uint8_t* rdram, recomp_context* ctx);
void func_80079BD4(std::uint8_t* rdram, recomp_context* ctx);
void func_8007F720(std::uint8_t* rdram, recomp_context* ctx);
void func_8007ACFC(std::uint8_t* rdram, recomp_context* ctx);
}

namespace blockplanet {
namespace {

using namespace tetrisphere::guest;

constexpr std::uint32_t balance_address = 0x800E0B1Cu;
constexpr std::uint32_t selection_address = 0x800E0B20u;
constexpr std::uint32_t track_address = 0x800E0B24u;
constexpr std::uint32_t save_bytes = 0x80102B08u;
constexpr std::uint32_t music_level_address = 0x800E28E8u;
constexpr std::uint32_t effects_level_address = 0x800E28ECu;
constexpr std::uint32_t changing_track = 0x800DFE84u;
constexpr std::uint32_t track_banner = 0x800E14A8u;
constexpr std::uint32_t music_player = 0x80107290u;
constexpr std::uint32_t music_max = 0x4FFF;
constexpr std::uint32_t effects_max = 0x7FFF;

std::atomic<int> volume{100};
std::atomic<bool> levels_wanted{false};
std::atomic<int> music_percent{100};
std::atomic<int> effects_percent{100};
std::atomic<int> selection_wanted{-1};  // 0 auto, 1 random, -1 leave the game's
std::atomic<int> override_track{-1};    // CHOOSE this track
std::atomic<int> play_request{-1};      // play this track now

const char* const names[track_count] = {
    "Prophetic", "Azule Lux", "Martist", "Compliant Confuse", "Faze", "Flim Flam", "Manicdrummix", "Hallucid",
    "Perpetuate303", "Phony", "T. Grey", "Learn", "Zyp Zyp", "Extol", "Magic Fluter", "Spooky Baby",
    "Zooming Car", "Cheddar Funk", "Snowy Mushrooms", "Dog Robot", "Sappy", "Skank Bowl Fight", "Birthday Bumps"};

gpr address(std::uint32_t value) { return static_cast<gpr>(static_cast<std::int64_t>(static_cast<std::int32_t>(value))); }

// Calls a game function, leaving the caller's registers as they were.
void call(void (*function)(std::uint8_t*, recomp_context*), std::uint8_t* rdram, recomp_context* ctx, gpr a0,
          gpr a1 = 0) {
    const recomp_context saved = *ctx;
    ctx->r4 = a0;
    ctx->r5 = a1;
    function(rdram, ctx);
    *ctx = saved;
}

} // namespace

void set_audio_volume(int percent) { volume.store(std::clamp(percent, 0, 100)); }
int audio_volume() { return volume.load(); }

void set_audio_levels(int music, int effects) {
    music_percent.store(std::clamp(music, 0, 100));
    effects_percent.store(std::clamp(effects, 0, 100));
    levels_wanted.store(true);
}

void set_music_random(bool random) {
    override_track.store(-1);
    selection_wanted.store(random ? 1 : 0);
}

void play_track(int track) {
    track = (track % track_count + track_count) % track_count;
    override_track.store(track);
    play_request.store(track);
}

int current_track(const unsigned char* rdram) {
    return std::clamp(static_cast<int>(load32(rdram, track_address)), 0, track_count - 1);
}

bool track_override() { return override_track.load() >= 0; }

const char* track_name(int track) { return track >= 0 && track < track_count ? names[track] : ""; }

} // namespace blockplanet

extern "C" void blockplanet_audio_frame(std::uint8_t* rdram, recomp_context* ctx) {
    using namespace blockplanet;
    using namespace tetrisphere::guest;
    bool save = false;
    // The game computes its levels from the balance; 6 gives both at full,
    // which BlockPlanet's levels then scale.
    if (levels_wanted.load(std::memory_order_relaxed)) {
        if (load32(rdram, balance_address) != 6) {
            store32(rdram, balance_address, 6);
            save = true;
        }
        const auto music = music_max * static_cast<std::uint32_t>(music_percent.load()) / 100;
        const auto effects = effects_max * static_cast<std::uint32_t>(effects_percent.load()) / 100;
        if (static_cast<std::uint16_t>(load16(rdram, music_level_address)) != music) {
            store16(rdram, music_level_address, static_cast<std::int16_t>(music));
            call(func_80079B80, rdram, ctx, music, address(music_player));
        }
        if (static_cast<std::uint16_t>(load16(rdram, effects_level_address)) != effects)
            call(func_80079BD4, rdram, ctx, effects);
    }
    // Music choice: CHOOSE while a track override is on, else auto or random.
    const int track = override_track.load();
    const int selection = track >= 0 ? 2 : selection_wanted.load();
    if (selection >= 0 && load32(rdram, selection_address) != static_cast<std::uint32_t>(selection)) {
        store32(rdram, selection_address, static_cast<std::uint32_t>(selection));
        save = true;
    }
    if (const int play = play_request.exchange(-1); play >= 0) {
        store32(rdram, track_address, static_cast<std::uint32_t>(play));
        store32(rdram, changing_track, 1);
        call(func_8007F720, rdram, ctx, static_cast<gpr>(play), address(music_player));
        call(func_8007ACFC, rdram, ctx, address(music_player));
        store32(rdram, changing_track, 0);
        store32(rdram, track_banner, 15);
        save = true;
    }
    if (save) {
        store8(rdram, save_bytes,
               static_cast<std::uint8_t>(load32(rdram, selection_address) << 4 | (load32(rdram, balance_address) & 0xF)));
        store8(rdram, save_bytes + 1, static_cast<std::uint8_t>(load32(rdram, track_address)));
    }
}
