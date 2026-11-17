// The jukebox (include/tetrisphere/jukebox.h). The game's tracks loop, so
// Repeat (on by default) is the game's own behaviour; with Repeat off a track
// moves on after three minutes (the game gives no track lengths). Stop
// silences the music (the game's song carries on underneath); Play starts the
// track again from its beginning.

#include "tetrisphere/jukebox.h"
#include "tetrisphere/audio_settings.h"
#include "../ui/ui_system.h"

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
constexpr int visualiser_count = 6;
const char* const visualiser_names[visualiser_count] = {"Water", "Pulse", "Bars", "Spectrum", "Scope", "Pieces"};
const char* const visualiser_icons[visualiser_count] = {"\uE283", "\uE560", "\uE105", "\uE2A3", "\uE038", "\uE4FF"};

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
    int pick = std::uniform_int_distribution<int>(0, track_count - 2)(random);
    return pick >= not_this && not_this >= 0 ? pick + 1 : pick;
}

void play(int t) {
    track = (t % track_count + track_count) % track_count;
    play_track(track);
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
    ui::set_text("jb_track", track_name(shown));
    std::string info = "Track " + std::to_string(shown + 1) + " of " + std::to_string(track_count);
    info += stopped ? " · Stopped" : track < 0 ? " · Menu music" : " · Playing";
    ui::set_text("jb_info", info);
    ui::set_text("jb_state", stopped ? "" : "");
    std::string modes = std::string(shuffle ? "Shuffle" : "In order") + " · " + (repeat ? "Repeat track" : "Play on") +
                        " · " + visualiser_names[visualiser];
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
    controls.push_back(button("playlist", "", "Playlist", "jukebox_list", "All 23 tracks"));
    controls.push_back(button("visualiser", "", visualiser_names[visualiser], "jukebox_vis",
                              "The background: Water (still), Pulse (the grid lifts with the beat) or Bars (columns rise with the music)",
                              visualiser != 0));
    ui::set_list("jukebox_controls", controls);

    std::vector<ui::ListItem> tracks;
    for (int t = 0; t < track_count; ++t) {
        char label[64];
        std::snprintf(label, sizeof label, "%02d. %s", t + 1, track_name(t));
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
    else if (command == "jukebox_hide") {
        ui::set_jukebox_hidden(true);
        return true;
    }
    else return false;
    publish(rdram);
    return true;
}

void tick() {
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
    if (stopped) set_music_muted(false);
    stopped = false;
    started.reset();
    track = -1;
}

} // namespace blockplanet::jukebox
