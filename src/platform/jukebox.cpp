// The jukebox (include/tetrisphere/jukebox.h). The game's tracks loop, so
// Repeat (on by default) is the game's own behaviour; with Repeat off a track
// moves on after three minutes (the game gives no track lengths). Stop
// silences the music (the game's song carries on underneath); Play starts the
// track again from its beginning.

#include "tetrisphere/jukebox.h"
#include "tetrisphere/audio_settings.h"
#include "tetrisphere/vortex_backdrop.h"
#include "../ui/ui_system.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <vector>

namespace blockplanet::jukebox {
namespace {

using clock = std::chrono::steady_clock;

int track = -1;                       // the track the jukebox last played (-1: none yet)
std::optional<clock::time_point> started;  // when it started (none: stopped, or not known)
bool stopped = false;
bool shuffle = false;
bool repeat = true;
int visualiser = 1;                   // 0 Water, 1 Pulse, 2 Bars
int shown_seconds = -2;

// The backgrounds: the grid's own (Water, Pulse, Bars), then full-screen
// Winamp-inspired ones built from the game's bricks (src/ui/ui_jukebox.cpp).
// Last, the Vortex: the game's own scene (src/graphics/vortex_backdrop.cpp).
// Then the game's 25 play backgrounds ("Stage 1" to "Stage 25"), drawn by the
// same means (vortex_backdrop.cpp). All open for now (the owner, 2026-11-17:
// everything unlocked for testing; how they are earned is for later).
constexpr int visualiser_known = 7;
constexpr int stage_backgrounds = 25;
constexpr int visualiser_count = visualiser_known + stage_backgrounds;
int stage_of(int v) { return v >= visualiser_known ? v - visualiser_known + 1 : 0; }

constexpr int vortex = 6;
// After the game's 23 tracks, three songs it plays outside them (started on
// its jingle player there; ids from docs/research/jukebox.md): the intro's
// (the N64 logo and the robots), the title's and the VORTEX scene's.
struct ExtraSong { const char* name; int id; };
constexpr ExtraSong extra_songs[] = {{"Intro", 0x129}, {"Title", 0x134}, {"Vortex", 0x143}};
constexpr int song_total = track_count + int(sizeof extra_songs / sizeof extra_songs[0]);

const char* song_name(int t) { return t < track_count ? track_name(t) : extra_songs[t - track_count].name; }

const char* const visualiser_names[visualiser_known] = {"Water", "Pulse", "Bars", "Spectrum", "Scope", "Pieces", "Vortex"};
const char* const visualiser_icons[visualiser_known] = {"\uE283", "\uE560", "\uE105", "\uE2A3", "\uE038", "\uE4FF", "\uE499"};
std::string visualiser_name(int v) {
    return v < visualiser_known ? visualiser_names[v] : "Stage " + std::to_string(stage_of(v));
}
const char* visualiser_icon(int v) { return v < visualiser_known ? visualiser_icons[v] : "\uE499"; }

int elapsed_seconds() {
    if (!started) return -1;
    return int(std::chrono::duration_cast<std::chrono::seconds>(clock::now() - *started).count());
}

std::string clock_text(int seconds) {
    if (seconds < 0) return "-:--";
    char text[16];
    std::snprintf(text, sizeof text, "%d:%02d", seconds / 60, seconds % 60);
    return text;
}

int random_other(int not_this) {
    static std::mt19937 random{std::random_device{}()};
    int pick = std::uniform_int_distribution<int>(0, song_total - 2)(random);
    return pick >= not_this && not_this >= 0 ? pick + 1 : pick;
}

void play(int t) {
    track = (t % song_total + song_total) % song_total;
    if (track < track_count) play_track(track);
    else play_song(extra_songs[track - track_count].id);
    set_music_muted(false);
    stopped = false;
    started = clock::now();
}

ui::ListItem button(const std::string& id, const char* icon, const std::string& label, const std::string& action,
                    const std::string& description, bool lit = false, bool focus = false) {
    ui::ListItem item{id, label, "", "command:" + action, true, 1, focus, description};
    item.icon = icon;
    item.classes = lit ? "player-button lit" : "player-button";
    return item;
}

void publish(const unsigned char* rdram) {
    const int shown = track >= 0 ? track : rdram != nullptr ? current_track(rdram) : 0;
    char number[8];
    std::snprintf(number, sizeof number, "%02d", shown + 1);
    ui::set_text("jb_number", number);
    ui::set_text("jb_track", song_name(shown));
    std::string info = "Track " + std::to_string(shown + 1) + " of " + std::to_string(song_total);
    info += stopped ? " · Stopped" : track < 0 ? " · Menu music" : " · Playing";
    ui::set_text("jb_info", info);
    ui::set_text("jb_state", stopped ? "" : "");
    std::string modes = std::string(shuffle ? "Shuffle" : "In order") + " · " + (repeat ? "Repeat track" : "Play on") +
                        " · " + visualiser_name(visualiser);
    ui::set_text("jb_modes", modes);
    shown_seconds = -2;
    tick();

    std::vector<ui::ListItem> controls;
    controls.push_back(button("prev", "", "Previous", "jukebox_prev", "The track before (or this one again, once it has played a few seconds)"));
    controls.push_back(button("play", "", "Play", "jukebox_play_now", "Play this track from its start", !stopped, true));
    controls.push_back(button("stop", "", "Stop", "jukebox_stop", "Silence the music", stopped));
    controls.push_back(button("next", "", "Next", "jukebox_next", shuffle ? "A random track" : "The next track"));
    controls.push_back(button("shuffle", "", "Shuffle", "jukebox_shuffle", "Next and Previous pick tracks at random", shuffle));
    controls.push_back(button("repeat", repeat ? "" : "", "Repeat", "jukebox_repeat",
                              "On: the track plays on and on, as in the game. Off: the next track after three minutes", repeat));
    controls.push_back(button("playlist", "", "Playlist", "jukebox_list", "All 26 songs: the 23 tracks, then the intro's, the title's and the vortex's"));
    controls.push_back(button("visualiser", visualiser_icon(visualiser), visualiser_name(visualiser), "jukebox_vis",
                              "The background: Water, Pulse and Bars on the tiles; Spectrum, Scope and Pieces full screen, "
                              "built from the game's bricks; Vortex, the game's hidden scene; then the backgrounds of the "
                              "game's stages, Stage 1 to Stage 25", visualiser != 0));
    ui::set_list("jukebox_controls", controls);

    std::vector<ui::ListItem> tracks;
    for (int t = 0; t < song_total; ++t) {
        char label[64];
        std::snprintf(label, sizeof label, "%02d. %s", t + 1, song_name(t));
        ui::ListItem item{"track" + std::to_string(t), label, t == shown && !stopped ? "Playing" : "",
                          "command:jukebox_play:" + std::to_string(t), true, 1, t == shown};
        item.icon = t == shown ? "" : "";
        tracks.push_back(item);
    }
    ui::set_list("jukebox", tracks);
    ui::set_jukebox_visualiser(visualiser);
}

} // namespace

bool command(const std::string& command, const unsigned char* rdram) {
    if (command.rfind("jukebox", 0) != 0) return false;
    const int now = track >= 0 ? track : rdram != nullptr ? current_track(rdram) : 0;
    if (command == "jukebox") {
        publish(rdram);
        ui::request_show("jukebox");
        return true;
    }
    if (command == "jukebox_list") {
        publish(rdram);
        ui::request_show("jukebox_list");
        return true;
    }
    if (command.rfind("jukebox_play:", 0) == 0) play(std::atoi(command.c_str() + 13));
    else if (command == "jukebox_play_now") play(now);
    else if (command == "jukebox_stop") stopped = true, started.reset(), set_music_muted(true);
    else if (command == "jukebox_next") play(shuffle ? random_other(now) : now + 1);
    else if (command == "jukebox_prev") {
        const int seconds = elapsed_seconds();
        play(seconds > 3 && !stopped ? now : shuffle ? random_other(now) : now - 1);
    }
    else if (command == "jukebox_shuffle") shuffle = !shuffle;
    else if (command == "jukebox_repeat") repeat = !repeat;
    else if (command == "jukebox_vis") visualiser = (visualiser + 1) % visualiser_count;
    else if (command == "jukebox_vortex") visualiser = vortex;  // for testing
    else if (command.rfind("jukebox_stage:", 0) == 0)  // for testing: a stage background
        visualiser = visualiser_known - 1 + std::clamp(std::atoi(command.c_str() + 14), 1, stage_backgrounds);
    else if (command == "jukebox_hide") {
        ui::set_jukebox_hidden(true);
        return true;
    }
    else return false;
    publish(rdram);
    return true;
}

void tick() {
    // The Vortex while a jukebox screen is on top and it is chosen.
    const auto screen = ui::current_screen();
    set_vortex_backdrop(visualiser >= vortex && (screen == "jukebox" || screen == "jukebox_list"), stage_of(visualiser));
    const int seconds = elapsed_seconds();
    if (!repeat && !stopped && seconds >= 180 && track_override()) {
        play(shuffle ? random_other(track) : track + 1);
        publish(nullptr);
        return;
    }
    if (seconds == shown_seconds) return;
    shown_seconds = seconds;
    ui::set_text("jb_time", clock_text(stopped ? 0 : seconds));
}

void on_launch() {
    stop_song();  // an extra song would play on over the game's music
    set_vortex_backdrop(false);
    if (stopped) set_music_muted(false);
    stopped = false;
    started.reset();
    track = -1;
}

} // namespace blockplanet::jukebox
