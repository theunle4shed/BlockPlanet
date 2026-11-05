// The game's audio, driven the way its Audio page does
// (docs/research/blockplanet-game-state.md, Audio settings):
//   0x800E0B1C  balance, 0..12        0x800E0B20  selection: 0 auto, 1 random, 2 choose
//   0x800E0B24  track, 0..22          0x80102B08  save bytes: selection << 4 | balance, track
//
// Levels: BlockPlanet keeps the game's balance at 6 (music and effects both
// at the game's full level) and scales every volume the game sets, at the
// two volume functions: func_80079B80(volume, player), used for the
// background music player (0x80107290), its fades and the jingle players, by
// the Music setting (hooked where it stores the final volume, 0x80079BB4,
// after it adds 0x2000 for some songs); func_80079BD4(volume), at its entry,
// by the Effects setting. A
// change is applied at once by calling them again with the game's levels
// (0x800E28E8 music, 0x800E28EC effects as last set).
//
// A track is played as the page does: flag 0x800DFE84, func_8007F720(track,
// player), func_8007ACFC(player).
//
// blockplanet_audio_frame runs from a hook at the top of the controller and
// save thread's loop (func_80029938 at 0x800299A0), about 60 times a second
// in every scene; the runtime runs one game thread at a time.

#include "tetrisphere/audio_settings.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/recomp_support.h"

#include <algorithm>
#include <atomic>
#include <cmath>
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
constexpr std::uint32_t effects_max = 0x7FFF;

std::atomic<bool> levels_wanted{false};
std::atomic<bool> levels_changed{false};
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


void set_audio_levels(int music, int effects) {
    music_percent.store(std::clamp(music, 0, 100));
    effects_percent.store(std::clamp(effects, 0, 100));
    levels_wanted.store(true);
    levels_changed.store(true);
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
    // The game's levels come from its balance; 6 gives both at full, which
    // the volume hooks then scale. A changed setting is applied at once.
    if (levels_wanted.load(std::memory_order_relaxed)) {
        if (load32(rdram, balance_address) != 6) {
            store32(rdram, balance_address, 6);
            save = true;
        }
        if (levels_changed.exchange(false)) {
            const auto music = static_cast<std::uint16_t>(load16(rdram, music_level_address));
            call(func_80079B80, rdram, ctx, music ? music : 0x4FFF, address(music_player));
            call(func_80079BD4, rdram, ctx, effects_max);
        }
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

// Volume hooks: a0 is the volume about to be used. The game's volume acts
// roughly on power (half the value gives a quarter of the loudness, measured
// on the title music), so a setting of p% scales it by sqrt(p / 100), which
// makes 50% half as loud.
namespace {
gpr scaled(gpr volume, int percent) {
    const double factor = std::sqrt(percent / 100.0);
    return static_cast<gpr>(static_cast<double>(volume & 0xFFFF) * factor);
}
} // namespace

extern "C" void blockplanet_scale_music_volume(std::uint8_t*, recomp_context* ctx) {
    using namespace blockplanet;
    if (!levels_wanted.load(std::memory_order_relaxed)) return;
    ctx->r4 = scaled(ctx->r4, music_percent.load());
}

extern "C" void blockplanet_scale_effects_volume(std::uint8_t*, recomp_context* ctx) {
    using namespace blockplanet;
    if (!levels_wanted.load(std::memory_order_relaxed)) return;
    ctx->r4 = scaled(ctx->r4, effects_percent.load());
}
