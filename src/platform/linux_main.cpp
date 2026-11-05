#include "tetrisphere/linux_platform.h"
#include "tetrisphere/data_directory.h"
#include "tetrisphere/audio_length_pacer.h"
#include "tetrisphere/controller_ports.h"
#include "tetrisphere/controller_mappings.h"
#include "tetrisphere/keyboard_state.h"
#include "tetrisphere/port_input.h"
#include "tetrisphere/qa_virtual_controllers.h"
#include "tetrisphere/audio_output.h"
#include "tetrisphere/diagnostic_log.h"
#include "tetrisphere/perf_metrics.h"
#include "tetrisphere/trophy_service.h"
#include "tetrisphere/game_event_watcher.h"
#include "tetrisphere/game_options.h"
#include "tetrisphere/close_game_menu.h"

#include "tetrisphere/rt64_context.h"
#include "tetrisphere/run_options.h"
#include "tetrisphere/run_identity.h"
#include "tetrisphere/runtime.h"
#include "tetrisphere/settings_store.h"
#include "tetrisphere/guest_memory.h"
#include "tetrisphere/profiles.h"
#include "tetrisphere/profile_menu.h"
#include "tetrisphere/relaunch.h"
#include "tetrisphere/menu_input.h"
#include "tetrisphere/mode_launcher.h"
#include "ui_system.h"
#include "tetrisphere/profile_save.h"
#include "tetrisphere/sphere_mesh.h"
#include "tetrisphere/wav_capture.h"
#include "tetrisphere/window_mode_controller.h"
#include "tetrisphere/pc_settings_hook.h"
#include "tetrisphere/controls_sdl.h"

#include "librecomp/rsp.hpp"
#include "ultramodern/input.hpp"
#include "ultramodern/ultramodern.hpp"
#define XXH_INLINE_ALL
#include "xxhash.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#include <timeapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <cstring>
#include <mutex>
#include <memory>
#include <string>
#include <fstream>
#include <set>
#include <vector>

RspExitReason tetrisphere_audio_rsp(std::uint8_t*, std::uint32_t);
extern std::atomic_bool exited;
extern std::atomic<std::uint64_t> tetrisphere_ai_attempts;
extern std::atomic<std::uint64_t> tetrisphere_ai_fifo_rejections;
extern std::atomic<std::uint64_t> tetrisphere_ai_fifo_wait_events;
extern std::atomic<std::uint64_t> tetrisphere_ai_fifo_waits_ms;
extern std::atomic<std::uint64_t> tetrisphere_ai_length_caps;
extern moodycamel::LightweightSemaphore graphics_shutdown_ready;

namespace tetrisphere {
namespace {

SDL_Window* window = nullptr;
ControllerPorts controller_ports;
QaVirtualControllers qa_virtual_controllers;
bool qa_controllers_active = false;
KeyboardState keyboard_state;
std::mutex input_snapshot_mutex;
std::array<ControllerPortSnapshot, 2> input_snapshot{};
std::uint16_t keyboard_snapshot = 0;
AudioOutput audio_output;
std::mutex audio_mutex;
WavCapture audio_capture;
std::atomic<std::uint64_t> observed_vis{0};
std::atomic<std::uint64_t> input_polls{0};
std::atomic<std::uint64_t> audio_rsp_tasks{0};
std::atomic<std::uint64_t> audio_rsp_total_us{0};
std::atomic<std::uint64_t> audio_rsp_max_us{0};
bool perf_log_enabled = false;
const std::uint8_t* guest_rdram = nullptr;
GameEventWatcher game_event_watcher;
std::unique_ptr<GameOptionApplier> game_option_applier;
bool log_game_events = false;

// The active profile's save file (progress and preferences); none for a
// guest. Progress is not recorded with --start-chapter (a testing option).
std::unique_ptr<ProfileSave> profile_save;
// The profile list, for the profile screens; and whether this is guest play.
std::unique_ptr<ProfileStore> profile_store;
bool guest_session = false;
// The name screen's text, and the profile the character screen is for.
NameEditor name_editor;
std::string character_profile_id;
ProfileSaveData profile_data;
bool profile_recording = false;
bool profile_dirty = false;
// A Chapter 0 (a Training lesson) started from a chapter list: when it is
// finished, carry on into that mode's next part (Story Chapter 1, or the
// puzzles). continue_mode is then set until it starts.
std::string chapter_zero_lesson;  // "basics" or "puzzle"
std::optional<LaunchMode> continue_mode;
// The PC's default controls from settings.dat, for new profiles and guests.
ControlPreferences pc_controls;
std::chrono::steady_clock::time_point last_play_poll{};

ControlPreferences controls_of(const Settings& settings) {
    return {settings.family_override, settings.bindings, settings.magic};
}

void apply_controls(Settings& settings, const ControlPreferences& controls) {
    settings.family_override = controls.family_override;
    settings.bindings = controls.bindings;
    settings.magic = controls.magic;
}

void save_profile() {
    if (!profile_save || !profile_dirty) return;
    std::string error;
    if (profile_save->save(profile_data, error)) profile_dirty = false;
    else std::fprintf(stderr, "Could not save the profile: %s\n", error.c_str());
}

// Research aid: BLOCKPLANET_WATCH="0x800E44A8:2,0x80103958:4" logs each
// guest value (1, 2 or 4 bytes) when it changes, as a watch event.
void poll_watches() {
    struct Watch {
        std::uint32_t address;
        int size;
        std::int64_t last;
    };
    static std::vector<Watch> watches = [] {
        std::vector<Watch> list;
        const char* spec = std::getenv("BLOCKPLANET_WATCH");
        if (spec == nullptr) return list;
        std::string text(spec);
        std::size_t position = 0;
        while (position < text.size()) {
            const auto comma = std::min(text.find(',', position), text.size());
            const auto item = text.substr(position, comma - position);
            const auto colon = item.find(':');
            const auto address = static_cast<std::uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
            const int size = colon == std::string::npos ? 4 : std::atoi(item.c_str() + colon + 1);
            if (address != 0 && (size == 1 || size == 2 || size == 4)) list.push_back({address, size, INT64_MIN});
            position = comma + 1;
        }
        return list;
    }();
    for (auto& watch : watches) {
        const std::int64_t value = watch.size == 1 ? guest::load8(guest_rdram, watch.address)
                                   : watch.size == 2 ? guest::load16(guest_rdram, watch.address)
                                                     : guest::load32(guest_rdram, watch.address);
        if (value == watch.last) continue;
        watch.last = value;
        std::fprintf(stdout, "{\"event\":\"watch\",\"address\":\"0x%08X\",\"value\":%lld}\n", watch.address,
                     static_cast<long long>(value));
        std::fflush(stdout);
    }
}

void poll_game_events() {
    if (guest_rdram == nullptr) return;
    poll_watches();
    if (game_option_applier) game_option_applier->apply(const_cast<std::uint8_t*>(guest_rdram));
    const auto state = read_game_state(guest_rdram);
    const auto now = std::chrono::steady_clock::now();
    if (profile_recording && state.scene == -1 && last_play_poll != std::chrono::steady_clock::time_point{}) {
        // Time in play, in whole seconds (the remainder carries over).
        static std::chrono::steady_clock::duration carried{};
        carried += now - last_play_poll;
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(carried);
        if (seconds.count() > 0) {
            profile_data.progress.statistics.seconds_played += seconds.count();
            carried -= seconds;
            profile_dirty = true;
        }
    }
    last_play_poll = now;
    for (const auto& event : game_event_watcher.update(state)) {
        if (log_game_events) {
            std::string fields;
            for (const auto& [key, value] : event.fields) {
                fields += ",\"" + key + "\":";
                fields += std::holds_alternative<std::int64_t>(value)
                    ? std::to_string(std::get<std::int64_t>(value))
                    : "\"" + std::get<std::string>(value) + "\"";
            }
            std::fprintf(stdout, "{\"event\":\"game_event\",\"name\":\"%s\"%s}\n",
                         event.name.c_str(), fields.c_str());
            std::fflush(stdout);
        }
        if (event.name == "training_completed" && !chapter_zero_lesson.empty() &&
            std::get<std::string>(event.fields.at("lesson")) == chapter_zero_lesson) {
            continue_mode = chapter_zero_lesson == "basics" ? LaunchMode::Rescue : LaunchMode::Puzzle;
            chapter_zero_lesson.clear();
        }
        post_game_event(event);
        if (profile_recording) {
            profile_dirty = true;
            if (record_game_event(profile_data.progress, event)) save_profile();  // milestones at once
        }
    }
}
const char* perf_present_mode = "console";

std::atomic<ControllerFamily> active_family{ControllerFamily::Keyboard};
std::atomic<ControllerFamily> active_p2_family{ControllerFamily::Keyboard};
Settings active_settings{};
Settings persisted_settings{};
SDL_Rect windowed_bounds{};
std::unique_ptr<WindowModeController> window_mode_controller;

void remember_windowed_bounds() {
    SDL_GetWindowPosition(window, &windowed_bounds.x, &windowed_bounds.y);
    SDL_GetWindowSize(window, &windowed_bounds.w, &windowed_bounds.h);
}

bool enter_fullscreen(const char*& kind) {
    bool exact_uhd = false;
    if (active_settings.graphics.resolution == GraphicsResolution::P2160) {
        const int window_display = SDL_GetWindowDisplayIndex(window);
        SDL_DisplayMode selected{};
        int selected_display = -1;
        int best_difference = 1000;
        for (int display = 0; display < SDL_GetNumVideoDisplays(); ++display) {
            SDL_DisplayMode current{};
            const int current_refresh = SDL_GetCurrentDisplayMode(display, &current) == 0
                ? current.refresh_rate : 60;
            for (int index = 0; index < SDL_GetNumDisplayModes(display); ++index) {
                SDL_DisplayMode candidate{};
                if (SDL_GetDisplayMode(display, index, &candidate) != 0 ||
                    candidate.w != 3840 || candidate.h != 2160) continue;
                const int difference = std::abs(candidate.refresh_rate - current_refresh);
                if (selected_display < 0 ||
                    (display == window_display && selected_display != window_display) ||
                    ((display == window_display) == (selected_display == window_display) &&
                     difference < best_difference)) {
                    selected = candidate;
                    selected_display = display;
                    best_difference = difference;
                }
            }
        }
        if (selected_display >= 0) {
            SDL_SetWindowPosition(window,
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(selected_display),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(selected_display));
        }
        if (selected_display >= 0 && SDL_SetWindowDisplayMode(window, &selected) == 0 &&
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) == 0) {
            SDL_PumpEvents();
            SDL_DisplayMode applied{};
            int drawable_width = 0;
            int drawable_height = 0;
            SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);
            exact_uhd = SDL_GetWindowDisplayIndex(window) == selected_display &&
                SDL_GetCurrentDisplayMode(selected_display, &applied) == 0 &&
                applied.w == 3840 && applied.h == 2160 &&
                drawable_width == 3840 && drawable_height == 2160;
            if (exact_uhd) kind = "exclusive-uhd";
        }
    }
    if (!exact_uhd) {
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
            return false;
        kind = "desktop";
    }
    return true;
}

void report_graphics_surface(const char* fullscreen_kind) {
    int actual_window_width = 0;
    int actual_window_height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GetWindowSize(window, &actual_window_width, &actual_window_height);
    SDL_Vulkan_GetDrawableSize(window, &drawable_width, &drawable_height);
    SDL_DisplayMode active_display{};
    const int display_index = SDL_GetWindowDisplayIndex(window);
    if (display_index >= 0) SDL_GetCurrentDisplayMode(display_index, &active_display);
    const auto requested_internal_scale =
        graphics_internal_scale(active_settings.graphics.resolution);
    std::fprintf(stdout,
                 "{\"event\":\"graphics_surface_dimensions\","
                 "\"run_id\":\"%s\",\"window_width\":%d,\"window_height\":%d,"
                 "\"drawable_width\":%d,\"drawable_height\":%d,"
                 "\"requested_internal_height\":%d,\"aspect\":\"%s\","
                 "\"window_mode\":\"%s\",\"fullscreen_kind\":\"%s\","
                 "\"display_index\":%d,\"display_width\":%d,\"display_height\":%d}\n",
                 run_id(), actual_window_width, actual_window_height,
                 drawable_width, drawable_height,
                 requested_internal_scale
                     ? static_cast<int>(*requested_internal_scale * 240.0) : 0,
                 graphics_aspect_name(active_settings.graphics.aspect),
                 graphics_window_mode_name(active_settings.graphics.window_mode),
                 fullscreen_kind, display_index, active_display.w, active_display.h);
    std::fflush(stdout);
}

bool apply_window_mode(bool fullscreen) {
    if (fullscreen) remember_windowed_bounds();
    const char* kind = fullscreen ? "desktop" : "none";
    if (fullscreen) {
        if (!enter_fullscreen(kind)) {
            SDL_SetWindowFullscreen(window, 0);
            SDL_SetWindowPosition(window, windowed_bounds.x, windowed_bounds.y);
            SDL_SetWindowSize(window, windowed_bounds.w, windowed_bounds.h);
            return false;
        }
    } else {
        if (SDL_SetWindowFullscreen(window, 0) != 0) return false;
        SDL_SetWindowDisplayMode(window, nullptr);
        SDL_SetWindowSize(window, windowed_bounds.w, windowed_bounds.h);
        SDL_SetWindowPosition(window, windowed_bounds.x, windowed_bounds.y);
    }
    active_settings.graphics.window_mode = fullscreen
        ? GraphicsWindowMode::Fullscreen : GraphicsWindowMode::Windowed;
    auto config = ultramodern::renderer::get_graphics_config();
    config.wm_option = fullscreen ? ultramodern::renderer::WindowMode::Fullscreen
                                  : ultramodern::renderer::WindowMode::Windowed;
    ultramodern::renderer::set_graphics_config(config);
    report_graphics_surface(kind);
    return true;
}

const char* family_override() {
    const char* temporary = std::getenv("TETRISPHERE_CONTROLLER_FAMILY");
    return temporary == nullptr ? active_settings.family_override.c_str() : temporary;
}

void poll_input() {
    input_polls.fetch_add(1, std::memory_order_relaxed);
    const auto controllers = controller_ports.snapshot();
    const auto keys = keyboard_state.buttons();
    std::lock_guard lock(input_snapshot_mutex);
    input_snapshot = controllers;
    keyboard_snapshot = keys;
}

void count_vi() {
    observed_vis.fetch_add(1, std::memory_order_relaxed);
}

// BlockPlanet's menus take the input while open, and until every button is
// released after they close (so the closing press does not reach the game).
std::atomic<bool> menus_block_game{false};
// Buttons the mode launcher presses on port 1 (bit 16 set while active).
std::atomic<std::uint32_t> launcher_buttons{0};
ModeLauncher mode_launcher;

// Settings changed in BlockPlanet's menus, applied like the PC settings menu.
std::deque<PCSettingsRequest> menu_settings_requests;


bool get_input(int port, std::uint16_t* buttons, float* x, float* y) {
    if (port < 0 || port >= 2) return false;
    if (const auto injected = launcher_buttons.load(); injected & 0x10000u) {
        *buttons = port == 0 ? static_cast<std::uint16_t>(injected & 0xFFFFu) : 0;
        *x = 0; *y = 0;
        return true;
    }
    if (controls_editor_blocks_game() || pc_settings_blocks_game() || menus_block_game.load()) {*buttons=0;*x=0;*y=0;return true;}
    // The runtime asks for ports in order. Freeze both readings together so
    // an SDL event between get_input(0) and get_input(1) cannot mix cycles.
    struct ReadCycle {
        std::array<ControllerPortSnapshot, 2> controllers{};
        std::uint16_t keys = 0;
        bool valid = false;
    };
    thread_local ReadCycle cycle;
    if (port == 0 || !cycle.valid) {
        std::lock_guard lock(input_snapshot_mutex);
        cycle.controllers = input_snapshot;
        cycle.keys = keyboard_snapshot;
        cycle.valid = true;
    }
    const auto& controller = cycle.controllers[port];
    const auto keys = cycle.keys;
    if (port == 1) cycle.valid = false;
    if (port == 1 && !controller.connected) return false;
    const auto mapped = map_port_input(port, controller, keys,
                                       controller_bindings(), magic_control());
    *buttons = mapped.buttons;
    *x = mapped.x;
    *y = mapped.y;
    static std::array<std::uint16_t, 2> previous{};
    if (mapped.buttons != previous[port]) {
        const auto family = port == 0 ? active_family.load() : active_p2_family.load();
        std::fprintf(stdout,
                     "{\"event\":\"logical_input_sample\",\"run_id\":\"%s\",\"port\":%d,\"family\":\"%s\","
                     "\"buttons\":\"0x%04X\",\"prompt_confirm\":\"%s\"}\n",
                     run_id(), port, controller_family_name(family),
                     static_cast<unsigned>(mapped.buttons),
                     logical_action_label(family, controller_bindings(),
                                          LogicalAction::Confirm));
        std::fflush(stdout);
        previous[port] = mapped.buttons;
    }
    return true;
}

void set_rumble(int port, bool enabled) {
    controller_ports.rumble(port, enabled);
}

ultramodern::input::connected_device_info_t device_info(int port) {
    if (port < 0 || port >= 2)
        return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    std::lock_guard lock(input_snapshot_mutex);
    if (input_snapshot[port].connected)
        return {ultramodern::input::Device::Controller,
                ultramodern::input::Pak::RumblePak};
    if (port == 0)
        return {ultramodern::input::Device::Controller,
                ultramodern::input::Pak::None};
    return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
}

void set_audio_frequency(std::uint32_t frequency) {
    std::lock_guard lock(audio_mutex);
    std::string error;
    if (!audio_output.open(frequency, error)) {
        diagnostic_log().error("audio_open_failed");
        std::fprintf(stderr, "{\"event\":\"fatal_audio\",\"detail\":\"%s\"}\n",
                     error.c_str());
        std::exit(70);
    }
    const char* capture_path = std::getenv("TETRISPHERE_AUDIO_CAPTURE_WAV");
    if (capture_path != nullptr && *capture_path != '\0' && !audio_capture.active()) {
        if (!audio_capture.open(capture_path, frequency)) {
            std::fprintf(stderr,
                         "{\"event\":\"fatal_audio_capture\","
                         "\"detail\":\"cannot_open_wav\"}\n");
            std::exit(70);
        }
        std::fprintf(stdout,
                     "{\"event\":\"audio_capture_started\","
                     "\"source\":\"queued_to_sdl_device\",\"frequency\":%u}\n",
                     frequency);
        std::fflush(stdout);
    }
}

void queue_audio(std::int16_t* samples, std::size_t count) {
    std::lock_guard lock(audio_mutex);
    std::string error;
    if (!audio_output.queue(samples, count, error)) {
        diagnostic_log().error("audio_queue_failed");
        std::fprintf(stderr,
                     "{\"event\":\"fatal_audio_queue\",\"detail\":\"%s\","
                     "\"sample_count\":%zu}\n", error.c_str(), count);
        std::exit(70);
    }
    if (audio_capture.active() && !audio_capture.append(samples, count)) {
        std::fprintf(stderr,
                     "{\"event\":\"fatal_audio_capture\","
                     "\"detail\":\"wav_write_failed\"}\n");
        std::exit(70);
    }
}

std::size_t audio_frames_remaining() {
    return audio_output.frames_remaining();
}

void rsp_init() { recomp::rsp::constants_init(); }

RspUcodeFunc* select_rsp_ucode(const OSTask* task) {
    constexpr std::uint32_t audio_ucode = 0x800DE7D0u;
    return task->t.type == M_AUDTASK &&
           static_cast<std::uint32_t>(task->t.ucode) == audio_ucode
        ? tetrisphere_audio_rsp : nullptr;
}

bool run_rsp(std::uint8_t* rdram, const OSTask* task) {
    constexpr std::uint32_t audio_ucode = 0x800DE7D0u;
    constexpr std::size_t audio_ucode_size = 0xDF0;
    constexpr std::uint64_t audio_ucode_hash = 0xE621BF918F29B135ULL;
    if (task->t.type != M_AUDTASK ||
        static_cast<std::uint32_t>(task->t.ucode) != audio_ucode ||
        XXH3_64bits(rdram + (audio_ucode & 0x7FFFFFu), audio_ucode_size) !=
            audio_ucode_hash) {
        return false;
    }
    recomp::rsp::set_callbacks({select_rsp_ucode});
    const auto start = std::chrono::steady_clock::now();
    const bool result = recomp::rsp::run_task(rdram, task);
    const auto elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
    audio_rsp_tasks.fetch_add(1, std::memory_order_relaxed);
    audio_rsp_total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
    auto previous_max = audio_rsp_max_us.load(std::memory_order_relaxed);
    while (elapsed_us > previous_max && !audio_rsp_max_us.compare_exchange_weak(
               previous_max, elapsed_us, std::memory_order_relaxed)) {}
    return result;
}

void show_error(const char* message) {
    diagnostic_log().error("runtime_error");
    std::fprintf(stderr, "{\"event\":\"runtime_error\",\"detail\":\"%s\"}\n",
                 message == nullptr ? "unknown" : message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tetrisphere error",
        message == nullptr ? "Unknown error." : message, window);
}

} // namespace

void publish_menu_settings(const Settings& settings);

extern "C" void tetrisphere_rt64_clear_keyboard_input() {
    SDL_Event focus_lost{};
    focus_lost.type = SDL_WINDOWEVENT;
    focus_lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    keyboard_state.handle_event(focus_lost);
    std::lock_guard lock(input_snapshot_mutex);
    keyboard_snapshot = 0;
}

ControllerFamily active_controller_family() { return active_family.load(); }

ControllerFamily active_controller_family_for_port(int port) {
    if (port == 0) return active_family.load();
    if (port == 1) return active_p2_family.load();
    return ControllerFamily::Keyboard;
}

bool linux_audio_fifo_full() {
    return audio_output.full();
}

std::size_t linux_audio_current_bytes() {
    return audio_output.current_bytes();
}

unsigned linux_audio_drain_wait_budget_ms() {
    const auto pacing = audio_output.pacing_snapshot();
    return audio_drain_wait_budget_ms(pacing.device_buffer_frames,
                                      pacing.obtained_frequency);
}

void initialize_linux_platform(std::uint8_t* rdram, const char* rom_sha256,
                               const RunOptions& run_options) {
#ifndef _WIN32
    // Without a D-Bus session (headless machines), SDL's shutdown makes a
    // call libdbus only warns about; its default is to abort on warnings.
    setenv("DBUS_FATAL_WARNINGS", "0", 0);
#endif
    guest_rdram = rdram;
    {
        const char* value = std::getenv("TETRISPHERE_LOG_GAME_EVENTS");
        log_game_events = value != nullptr && std::strcmp(value, "1") == 0;
    }
    initialize_controller_mappings();
#ifdef _WIN32
    // ultramodern paces VIs and the audio wait with Win32 Sleep. Ask for 1 ms
    // timer resolution explicitly instead of relying on SDL to have done so.
    timeBeginPeriod(1);
#endif
    {
        const char* perf_env = std::getenv("TETRISPHERE_PERF_METRICS");
        perf_log_enabled = run_options.perf_log ||
            (perf_env != nullptr && std::strcmp(perf_env, "1") == 0);
        perf_present_mode = present_mode_name(
            run_options.present_mode.value_or(PresentMode::Console));
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    const char* data_dir_override = std::getenv("TETRISPHERE_DATA_DIR");
    const auto user_path = application_data_directory();
    if (user_path.empty()) {
        show_error("Cannot determine the game data directory.");
        std::exit(74);
    }
    // Player profiles: the cartridge save and trophies live in the active
    // profile's folder; settings are shared (docs/SAVES.md). A guest has no
    // folder: the save stays in memory and no trophies are earned.
    profile_store = std::make_unique<ProfileStore>(user_path);
    guest_session = run_options.guest;
    auto& profiles = *profile_store;
    std::string profile_error;
    if (!profiles.load(profile_error) ||
        (run_options.profile && !profiles.use(*run_options.profile, profile_error))) {
        show_error(profile_error.c_str());
        std::exit(74);
    }
    profiles.purge_deleted(30);
    const auto profile_path = run_options.guest ? std::filesystem::path{} : profiles.directory(profiles.active());
    std::string eeprom_error;
    std::filesystem::path eeprom_path;
    if (!run_options.guest) {
        std::error_code ignored;
        std::filesystem::create_directories(profile_path, ignored);
        eeprom_path = profile_path / "cartridge.eep";
        // Keep the cartridge saves of the last three sessions.
        if (!rotate_backup(eeprom_path, profile_path / "backups", 3, eeprom_error))
            std::fprintf(stderr, "Could not back up the save: %s\n", eeprom_error.c_str());
    }
    std::fprintf(stdout, "{\"event\":\"profile\",\"id\":\"%s\",\"guest\":%s,\"profiles\":%zu}\n",
                 run_options.guest ? "" : profiles.active().id.c_str(), run_options.guest ? "true" : "false",
                 profiles.profiles().size());
    std::fflush(stdout);
    // With several profiles, ask who is playing (unless the command line said).
    if (profiles.profiles().size() > 1 && !run_options.profile && !run_options.guest) {
        blockplanet::ui::set_list("profiles", profile_list(profiles, false));
        blockplanet::ui::request_open("profiles");
    }
    if (!initialize_eeprom(eeprom_path, eeprom_error)) {
        show_error(eeprom_error.c_str());
        std::exit(74);
    }
    const auto settings_path = user_path / "settings.dat";
    SettingsStore settings_store(settings_path);
    std::string settings_error;
    if (!settings_store.load(active_settings, settings_error)) {
        show_error(settings_error.c_str());
        std::exit(74);
    }
    if (!std::filesystem::exists(settings_path) &&
        !settings_store.save(active_settings, settings_error)) {
        show_error(settings_error.c_str());
        std::exit(74);
    }
    // Controls belong to the profile. settings.dat keeps the PC's defaults,
    // which a profile without controls of its own starts from; a guest uses
    // them for the session.
    pc_controls = controls_of(active_settings);
    if (!run_options.guest) {
        profile_save = std::make_unique<ProfileSave>(profile_path / "save.json");
        std::string save_error;
        if (!profile_save->load(profile_data, save_error)) {
            show_error(save_error.c_str());
            std::exit(74);
        }
        if (profile_data.preferences.controls) {
            apply_controls(active_settings, *profile_data.preferences.controls);
        } else {
            profile_data.preferences.controls = pc_controls;
            profile_dirty = true;
            save_profile();
        }
        profile_recording = !run_options.start_chapter;
    }
    persisted_settings=active_settings;
    initialize_pc_settings(persisted_settings);
    publish_menu_settings(persisted_settings);
    if (run_options.fullscreen) {
        active_settings.graphics.window_mode = *run_options.fullscreen
            ? GraphicsWindowMode::Fullscreen : GraphicsWindowMode::Windowed;
    }
    set_controller_bindings(active_settings.bindings);
    set_magic_control(active_settings.magic);
    active_family.store(ControllerFamily::Keyboard);
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto diagnostic_path = user_path / "diagnostics" /
        (std::to_string(timestamp) + "-" + run_id() + ".jsonl");
    std::string diagnostic_error;
    if (!diagnostic_log().start(diagnostic_path, run_id(), TETRISPHERE_BUILD_ID,
                                rom_sha256, active_settings, diagnostic_error)) {
        show_error(diagnostic_error.c_str());
        std::exit(74);
    }
    std::fprintf(stdout, "{\"event\":\"diagnostic_file_ready\",\"run_id\":\"%s\"}\n",
                 run_id());
    std::fflush(stdout);
    {
        char* base = SDL_GetBasePath();
        const auto definitions = base != nullptr
            ? std::filesystem::u8path(base) / "trophies.json" : std::filesystem::path("trophies.json");
        if (base != nullptr) SDL_free(base);
        if (!run_options.guest) initialize_trophies(definitions, profile_path / "trophies.json");
    }
    game_option_applier = std::make_unique<GameOptionApplier>(
        GameOptions{run_options.casual, run_options.start_chapter});
    if (game_option_applier->affects_awards()) {
        trophy_engine().set_awards_enabled(false);
        std::fprintf(stdout, "{\"event\":\"game_options\",\"casual\":%s,\"start_chapter\":%d,"
                     "\"trophies\":\"paused\"}\n", run_options.casual ? "true" : "false",
                     run_options.start_chapter.value_or(0));
        std::fflush(stdout);
    }
    SDL_Rect usable_display{};
    if (SDL_GetDisplayUsableBounds(0, &usable_display) != 0)
        usable_display = SDL_Rect{0, 0, 960, 720};
    const auto [window_width, window_height] = graphics_window_size(
        active_settings.graphics, usable_display.w, usable_display.h);
    window = SDL_CreateWindow("Tetrisphere",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              window_width, window_height,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    remember_windowed_bounds();
    const bool fullscreen_requested =
        active_settings.graphics.window_mode == GraphicsWindowMode::Fullscreen;
    const char* fullscreen_kind = "none";
    if (fullscreen_requested && !enter_fullscreen(fullscreen_kind)) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    report_graphics_surface(fullscreen_kind);
    window_mode_controller = std::make_unique<WindowModeController>(
        fullscreen_requested, apply_window_mode);
    const char* qa_actions = std::getenv("TETRISPHERE_QA_CONTROLLERS");
    if (qa_actions != nullptr) {
        if (*qa_actions == '\0' || data_dir_override == nullptr ||
            *data_dir_override == '\0') {
            show_error("QA controllers require TETRISPHERE_DATA_DIR and an actions file");
            std::exit(74);
        }
        std::string qa_error;
        if (!qa_virtual_controllers.initialize(std::filesystem::u8path(qa_actions),
                                                user_path, qa_error) ||
            !controller_ports.set_preferred_ids(qa_virtual_controllers.instance_ids())) {
            qa_virtual_controllers.close();
            show_error(qa_error.empty() ? "QA controller port assignment failed" : qa_error.c_str());
            std::exit(74);
        }
        qa_controllers_active = true;
        const auto ids = qa_virtual_controllers.instance_ids();
        std::fprintf(stdout,
                     "{\"event\":\"qa_virtual_controllers\",\"run_id\":\"%s\","
                     "\"p1_instance_id\":%d,\"p2_instance_id\":%d}\n",
                     run_id(), static_cast<int>(ids[0]), static_cast<int>(ids[1]));
        std::fflush(stdout);
    }
    controller_ports.open_all();
    {
        const auto initial = controller_ports.snapshot();
        std::lock_guard lock(input_snapshot_mutex);
        input_snapshot = initial;
        keyboard_snapshot = keyboard_state.buttons();
        if (initial[0].connected)
            active_family.store(select_controller_family(initial[0].name.c_str(), family_override()));
        if (initial[1].connected)
            active_p2_family.store(select_controller_family(initial[1].name.c_str(), family_override()));
    }
    std::fprintf(stdout,
#ifdef _WIN32
                 "{\"event\":\"native_windows_started\",\"run_id\":\"%s\",\"backend\":\"rt64-vulkan\","
#else
                 "{\"event\":\"native_linux_started\",\"run_id\":\"%s\",\"backend\":\"rt64-vulkan\","
#endif
                 "\"controller_family\":\"%s\",\"prompt_confirm\":\"%s\"}\n",
                 run_id(),
                 controller_family_name(active_family.load()),
                 logical_action_label(active_family.load(), controller_bindings(),
                                      LogicalAction::Confirm));
    std::fflush(stdout);

    ultramodern::renderer::GraphicsConfig graphics{};
    graphics.developer_mode = false;
    graphics.res_option = ultramodern::renderer::Resolution::Auto;
    graphics.wm_option = fullscreen_requested
        ? ultramodern::renderer::WindowMode::Fullscreen
        : ultramodern::renderer::WindowMode::Windowed;
    graphics.hr_option = ultramodern::renderer::HUDRatioMode::Original;
    graphics.api_option = ultramodern::renderer::GraphicsApi::Vulkan;
    graphics.ar_option = active_settings.graphics.aspect == GraphicsAspect::Expand16x9
        ? ultramodern::renderer::AspectRatio::Manual
        : ultramodern::renderer::AspectRatio::Original;
    graphics.msaa_option = ultramodern::renderer::Antialiasing::None;
    set_rt64_run_options(resolve_graphics_run_options(active_settings.graphics, run_options));
    tetrisphere::sphere::set_detail(
        resolve_graphics_run_options(active_settings.graphics, run_options).sphere_extended.value_or(false)
            ? tetrisphere::sphere::Detail::Extended : tetrisphere::sphere::Detail::Original);
    graphics.rr_option = active_settings.graphics.refresh == GraphicsRefresh::Original
        ? ultramodern::renderer::RefreshRate::Original
        : ultramodern::renderer::RefreshRate::Manual;
    graphics.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    switch (active_settings.graphics.refresh) {
        case GraphicsRefresh::Fps30: graphics.rr_manual_value = 30; break;
        case GraphicsRefresh::Fps60: graphics.rr_manual_value = 60; break;
        case GraphicsRefresh::Fps120: graphics.rr_manual_value = 120; break;
        case GraphicsRefresh::Original: graphics.rr_manual_value = 60; break;
    }
    graphics.ds_option = 1;
    set_rt64_internal_resolution_scale(
        graphics_internal_scale(active_settings.graphics.resolution));
    ultramodern::renderer::set_graphics_config(graphics);

    ultramodern::set_callbacks(
        {rsp_init, run_rsp}, {create_rt64_context},
        {queue_audio, audio_frames_remaining, set_audio_frequency},
        {poll_input, get_input, set_rumble, device_info},
        {}, {count_vi, nullptr}, {show_error}, {});
#ifdef _WIN32
    SDL_SysWMinfo window_info{};
    SDL_VERSION(&window_info.version);
    if (SDL_GetWindowWMInfo(window, &window_info) != SDL_TRUE) {
        show_error(SDL_GetError());
        std::exit(70);
    }
    ultramodern::renderer::WindowHandle native_window{
        window_info.info.win.window, GetCurrentThreadId()};
    ultramodern::preinit(rdram, native_window);
#else
    ultramodern::preinit(rdram, window);
#endif
}

// Current values for the menus' setting rows (assets/ui/screens/options.rml).
void publish_menu_settings(const Settings& settings) {
    blockplanet::ui::set_setting("sphere", graphics_sphere_name(settings.graphics.sphere));
    blockplanet::ui::set_setting("window",
        settings.graphics.window_mode == GraphicsWindowMode::Fullscreen ? "fullscreen" : "windowed");
    blockplanet::ui::set_setting("texture_filter", graphics_texture_filter_name(settings.graphics.three_point_filter));
    blockplanet::ui::set_setting("presentation_filter",
                                 graphics_presentation_filter_name(settings.graphics.presentation_filter));
}

// "setting:NAME=VALUE" from the menus, as a settings change (field numbers
// as in the PC settings menu). False if the command is not a known setting.
bool queue_menu_setting(const std::string& command) {
    const auto equals = command.find('=');
    if (command.rfind("setting:", 0) != 0 || equals == std::string::npos) return false;
    const auto name = command.substr(8, equals - 8);
    const auto value = command.substr(equals + 1);
    Settings requested = persisted_settings;
    int field = -1;
    if (name == "sphere" && parse_graphics_sphere(value, requested.graphics.sphere)) field = 7;
    else if (name == "window" && (value == "fullscreen" || value == "windowed")) {
        requested.graphics.window_mode =
            value == "fullscreen" ? GraphicsWindowMode::Fullscreen : GraphicsWindowMode::Windowed;
        field = 3;
    } else if (name == "texture_filter" && parse_graphics_texture_filter(value, requested.graphics.three_point_filter))
        field = 6;
    else if (name == "presentation_filter" &&
             parse_graphics_presentation_filter(value, requested.graphics.presentation_filter))
        field = 5;
    if (field < 0) return false;
    menu_settings_requests.push_back({requested, field});
    return true;
}

// The Story chapter select: Chapter 0 (Training Basics), the chapters this
// profile has completed, and the next one. A guest gets Chapter 0 and 1.
constexpr int rescue_chapters = 15;
std::vector<blockplanet::ui::ListItem> chapter_choices() {
    std::set<int> completed;
    if (profile_save) {
        const auto found = profile_data.progress.chapters_completed.find("rescue");
        if (found != profile_data.progress.chapters_completed.end()) completed = found->second;
    }
    return story_chapters(completed, rescue_chapters);
}

void launch_mode(LaunchMode mode) {
    if (guest_rdram == nullptr) return;
    const bool accepted = mode_launcher.request(mode, guest_rdram, std::chrono::steady_clock::now());
    if (accepted) blockplanet::ui::request_close();
    std::fprintf(stdout, "{\"event\":\"mode_launch\",\"mode\":\"%s\",\"status\":\"%s\"}\n",
                 launch_mode_name(mode), accepted ? "started" : "refused");
    std::fflush(stdout);
}

// Saves everything and stops the game's threads.
void stop_game() {
    save_trophies();
    save_profile();
    exited.store(true);
    graphics_shutdown_ready.signal();
    ultramodern::join_event_threads();
    ultramodern::join_thread_cleaner_thread();
}

// Saves everything, stops the game and closes its devices and window.
void shut_down() {
    stop_game();
    // RT64's event-filter userdata dies with its rendering thread. Controller
    // teardown can emit SDL events, so remove the filter before closing devices.
    SDL_SetEventFilter(nullptr, nullptr);
    controller_ports.close();
    qa_virtual_controllers.close();
    audio_output.close();
    SDL_DestroyWindow(window);
    SDL_Quit();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

[[noreturn]] void close_game() {
    shut_down();
    std::exit(0);
}

// Restarts the game as another profile or as a guest (the game reads its
// cartridge save while booting). `remove_id`, if set, is a profile deleted
// once nothing is using its folder any more.
[[noreturn]] void restart_as(std::optional<std::string> profile, bool guest, const std::string& remove_id = {}) {
    std::fprintf(stdout, "{\"event\":\"profile_restart\",\"profile\":\"%s\",\"guest\":%s}\n",
                 profile ? profile->c_str() : "", guest ? "true" : "false");
    std::fflush(stdout);
    shut_down();
    if (!remove_id.empty()) {
        std::string error;
        if (const auto* removed = profile_by_id(*profile_store, remove_id)) {
            const std::string name = removed->name;
            if (!profile_store->remove(name, error))
                std::fprintf(stderr, "Could not delete the profile: %s\n", error.c_str());
        }
        if (!guest) profile = profile_store->active().name;
    }
    if (!relaunch(relaunch_arguments(launch_arguments(), profile, guest))) {
        std::fprintf(stderr, "Could not restart BlockPlanet.\n");
        std::exit(75);
    }
    std::exit(0);  // Windows: the new process is running
}

// The profile screens' commands (profile_menu.h). True if `command` was one.
bool handle_profile_command(const std::string& command) {
    if (!profile_store) return false;
    auto& store = *profile_store;
    const auto argument = [&](const char* prefix) -> std::optional<std::string> {
        const std::string text(prefix);
        if (command.rfind(text, 0) != 0) return std::nullopt;
        return command.substr(text.size());
    };
    if (command == "profiles") {
        blockplanet::ui::set_list("profiles", profile_list(store, guest_session));
        blockplanet::ui::request_show("profiles");
        return true;
    }
    // Refreshes the lists that show this profile.
    const auto refresh = [&](const std::string& id) {
        blockplanet::ui::set_list("profiles", profile_list(store, guest_session));
        blockplanet::ui::set_list("profile_actions", profile_actions(store, id, guest_session));
        blockplanet::ui::set_list("characters", character_choices(store, id));
    };
    const auto show_name = [&](const std::string& hint) {
        blockplanet::ui::set_text("name_edit", name_editor.text() + "|");
        blockplanet::ui::set_text("name_hint", hint);
    };
    if (const auto id = argument("profile_rename:")) {
        const auto* profile = profile_by_id(store, *id);
        if (profile == nullptr) return true;
        name_editor.start(*id, profile->name);
        blockplanet::ui::set_list("keyboard", name_editor.keyboard());
        show_name("");
        blockplanet::ui::request_show("name");
        return true;
    }
    if (command == "profile_new") {
        name_editor.start("new", "");
        blockplanet::ui::set_list("keyboard", name_editor.keyboard());
        show_name("Leave it empty for \"" + next_profile_name(store) + "\"");
        blockplanet::ui::request_show("name");
        return true;
    }
    if (const auto key = argument("name_key:")) {
        if (name_editor.press(*key)) blockplanet::ui::set_list("keyboard", name_editor.keyboard());
        show_name("");
        return true;
    }
    if (const auto text = argument("name_type:")) {
        name_editor.type(*text);
        show_name("");
        return true;
    }
    if (command == "name_done") {
        std::string name = name_editor.text();
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name_editor.target() == "new") {
            if (name.empty()) name = next_profile_name(store);
            if (store.find(name) != nullptr) show_name("That name is taken");
            else restart_as(name, false);  // --profile creates it
            return true;
        }
        const auto* profile = profile_by_id(store, name_editor.target());
        if (profile == nullptr) return true;
        if (name.empty()) {
            show_name("Enter a name");
            return true;
        }
        std::string error;
        const std::string old_name = profile->name;
        if (!store.rename(old_name, name, error)) {
            show_name(store.find(name) != nullptr ? "That name is taken" : "Names are letters, digits and symbols");
            return true;
        }
        refresh(name_editor.target());
        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        return true;
    }
    if (const auto id = argument("profile_character:")) {
        const auto* profile = profile_by_id(store, *id);
        if (profile == nullptr) return true;
        character_profile_id = *id;
        blockplanet::ui::set_text("character_for", "For " + profile->name);
        blockplanet::ui::set_setting("profile_colour", std::to_string(profile->palette));
        blockplanet::ui::set_list("characters", character_choices(store, *id));
        blockplanet::ui::request_show("character");
        return true;
    }
    if (const auto choice = argument("profile_set_character:")) {
        const auto colon = choice->find(':');
        if (colon == std::string::npos) return true;
        const auto id = choice->substr(0, colon);
        const auto character = choice->substr(colon + 1);
        const auto* profile = profile_by_id(store, id);
        if (profile == nullptr) return true;
        std::string error;
        const std::string name = profile->name;
        if (!store.set_character(name, character == "none" ? "" : character, profile->palette, error))
            std::fprintf(stderr, "Could not set the character: %s\n", error.c_str());
        refresh(id);
        return true;
    }
    if (const auto colour = argument("setting:profile_colour=")) {
        const auto* profile = profile_by_id(store, character_profile_id);
        if (profile == nullptr) return true;
        std::string error;
        const std::string name = profile->name;
        const int palette = std::atoi(colour->c_str());
        if (!store.set_character(name, profile->character, palette, error))
            std::fprintf(stderr, "Could not set the colour: %s\n", error.c_str());
        blockplanet::ui::set_setting("profile_colour", std::to_string(profile_by_id(store, character_profile_id)->palette));
        return true;
    }
    if (const auto id = argument("profile:")) {
        blockplanet::ui::set_list("profile_actions", profile_actions(store, *id, guest_session));
        blockplanet::ui::request_show("profile");
        return true;
    }
    if (const auto id = argument("profile_play:")) {
        if (const auto* profile = profile_by_id(store, *id)) restart_as(profile->name, false);
        return true;
    }
    if (command == "profile_guest") restart_as(std::nullopt, true);
    if (const auto id = argument("profile_delete_confirmed:")) {
        const auto* profile = profile_by_id(store, *id);
        if (profile == nullptr) return true;
        // The playing profile's folder is in use until the game stops.
        if (!guest_session && profile->id == store.active().id) restart_as(std::nullopt, false, *id);
        std::string error;
        const std::string name = profile->name;
        if (!store.remove(name, error)) std::fprintf(stderr, "Could not delete the profile: %s\n", error.c_str());
        // Back to the (updated) profile list.
        blockplanet::ui::set_list("profiles", profile_list(store, guest_session));
        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        blockplanet::ui::queue_action(blockplanet::ui::Action::Back);
        return true;
    }
    if (const auto id = argument("profile_delete:")) {
        blockplanet::ui::set_list("confirm", delete_confirmation(store, *id));
        blockplanet::ui::request_show("confirm");
        return true;
    }
    return false;
}

[[noreturn]] void run_linux_event_loop() {
    SDL_Event event{};
    const auto event_loop_start = std::chrono::steady_clock::now();
    auto next_audio_report = event_loop_start + std::chrono::seconds(10);
    auto next_trophy_save = event_loop_start + std::chrono::seconds(30);
    const char* audio_metrics = std::getenv("TETRISPHERE_AUDIO_METRICS");
    const bool report_audio = audio_metrics != nullptr && std::strcmp(audio_metrics, "1") == 0;
    constexpr auto perf_interval = std::chrono::seconds(5);
    auto perf_window_start = event_loop_start;
    std::uint64_t perf_last_vis = observed_vis.load(std::memory_order_relaxed);
    std::uint64_t perf_last_rejections =
        tetrisphere_ai_fifo_rejections.load(std::memory_order_relaxed);
    if (perf_log_enabled) {
        // Start the first window now so boot work is not counted.
        auto& metrics = perf_metrics();
        metrics.display_list.take(); metrics.display_list_rt64.take();
        metrics.screen_update.take(); metrics.audio_wait.take();
        std::fprintf(stdout, "{\"event\":\"perf_log_started\",\"interval_ms\":%lld,"
                     "\"present_mode\":\"%s\"}\n",
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                         perf_interval).count()), perf_present_mode);
        std::fflush(stdout);
    }
    for (;;) {
        for (;;) {
            auto request=take_pc_settings_change();
            if(!request) {
                if(auto controls=take_controls_apply())request=PCSettingsRequest{*controls,13};
            }
            if(!request && !menu_settings_requests.empty()) {
                request=menu_settings_requests.front();
                menu_settings_requests.pop_front();
            }
            if(!request)break;
            const auto merged=merge_pc_settings_change(persisted_settings,request->settings,request->field);
            const auto* requested=&merged;
            std::string error;
            SettingsStore store(application_data_directory() / "settings.dat");
            // settings.dat keeps the PC's default controls; a profile's own
            // controls go to its save file, and a guest's last the session.
            auto on_disk = *requested;
            apply_controls(on_disk, pc_controls);
            if (request->field==13 && profile_save) {
                profile_data.preferences.controls = controls_of(*requested);
                profile_dirty = true;
                std::string save_error;
                if (!profile_save->save(profile_data, save_error)) {
                    std::fprintf(stderr, "Could not save the profile's controls: %s\n", save_error.c_str());
                    acknowledge_controls_settings(persisted_settings);
                    complete_controls_apply(false);
                    continue;
                }
                profile_dirty = false;
            }
            if (!store.save(on_disk, error)) {
                std::fprintf(stderr, "Could not save PC options: %s\n", error.c_str());
                acknowledge_controls_settings(persisted_settings);
                if(request->field==13)complete_controls_apply(false);
                continue;
            }
            persisted_settings=merged;
            acknowledge_controls_settings(persisted_settings);
            bool window_applied = true;
            if (request->field==3) window_applied=window_mode_controller->set_fullscreen(
                requested->graphics.window_mode == GraphicsWindowMode::Fullscreen);
            active_settings.family_override=requested->family_override;
            active_settings.bindings=requested->bindings;
            active_settings.magic=requested->magic;
            if(request->field==5)active_settings.graphics.presentation_filter=requested->graphics.presentation_filter;
            if(request->field==6)active_settings.graphics.three_point_filter=requested->graphics.three_point_filter;
            if(request->field==7) {
                active_settings.graphics.sphere=requested->graphics.sphere;
                // Applies from the next frame. A --sphere option on the
                // command line only sets the starting value.
                tetrisphere::sphere::set_detail(requested->graphics.sphere==GraphicsSphere::Extended
                    ? tetrisphere::sphere::Detail::Extended : tetrisphere::sphere::Detail::Original);
            }
            set_controller_bindings(requested->bindings);
            set_magic_control(requested->magic);
            const auto ports = controller_ports.snapshot();
            if (active_family.load() != ControllerFamily::Keyboard && ports[0].connected)
                active_family.store(select_controller_family(ports[0].name.c_str(), family_override()));
            if (active_p2_family.load() != ControllerFamily::Keyboard && ports[1].connected)
                active_p2_family.store(select_controller_family(ports[1].name.c_str(), family_override()));
            set_rt64_live_filters(request->field==5
                ? std::optional{requested->graphics.presentation_filter} : std::nullopt,
                request->field==6 ? std::optional{requested->graphics.three_point_filter} : std::nullopt);
            if(request->field==13) {
                complete_controls_apply(true);
            }
            publish_menu_settings(persisted_settings);
            std::fprintf(stdout, "{\"event\":\"pc_options_saved\",\"window_applied\":%s}\n",
                         window_applied ? "true" : "false");
            std::fflush(stdout);
        }
        if (take_close_game_request()) {
            std::fprintf(stdout, "{\"event\":\"close_game_requested\"}\n");
            std::fflush(stdout);
            close_game();
        }
        if (qa_controllers_active) {
            std::vector<QaControllerAction> applied;
            std::string qa_error;
            if (!qa_virtual_controllers.pump(applied, qa_error)) {
                std::fprintf(stderr, "QA controller input failed: %s\n", qa_error.c_str());
                std::fflush(stderr);
                controller_ports.close();
                qa_virtual_controllers.close();
                std::exit(74);
            }
            for (const auto& action : applied) {
                std::fprintf(stdout,
                             "{\"event\":\"qa_virtual_action\",\"run_id\":\"%s\","
                             "\"seq\":%llu,\"port\":%d,\"button\":%d,\"pressed\":%s}\n",
                             run_id(), static_cast<unsigned long long>(action.seq),
                             action.port, static_cast<int>(action.button),
                             action.pressed ? "true" : "false");
            }
            if (!applied.empty()) std::fflush(stdout);
        }
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                close_game();
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                window_mode_controller->focus_lost();
            }
            const auto before_removal = event.type == SDL_CONTROLLERDEVICEREMOVED
                ? controller_ports.snapshot()
                : std::array<ControllerPortSnapshot, 2>{};
            const int active_port = controller_ports.handle_event(event);
            if (active_port >= 0) {
                const auto current = controller_ports.snapshot();
                const auto family = select_controller_family(current[active_port].name.c_str(),
                                                             family_override());
                if (active_port == 0) active_family.store(family);
                else active_p2_family.store(family);
            }
            std::string audio_error;
            if (audio_output.handle_event(event, audio_error)) {
                diagnostic_log().audio(audio_error.empty() ? "reopened" : "unavailable");
                std::fprintf(audio_error.empty() ? stdout : stderr,
                             "{\"event\":\"audio_device_change\",\"status\":\"%s\"}\n",
                             audio_error.empty() ? "reopened" : "unavailable");
                std::fflush(audio_error.empty() ? stdout : stderr);
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (before_removal[0].instance_id == event.cdevice.which)
                    active_family.store(ControllerFamily::Keyboard);
                if (before_removal[1].instance_id == event.cdevice.which)
                    active_p2_family.store(ControllerFamily::Keyboard);
            }
            if (keyboard_state.handle_event(event)) {
                active_family.store(ControllerFamily::Keyboard);
            }
            const bool removed_assigned=event.type==SDL_CONTROLLERDEVICEREMOVED &&
                (before_removal[0].instance_id==event.cdevice.which || before_removal[1].instance_id==event.cdevice.which);
            int event_port=active_port;
            if(event.type==SDL_CONTROLLERAXISMOTION || event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_CONTROLLERBUTTONUP) {
                const auto ports=controller_ports.snapshot();
                const auto id=event.type==SDL_CONTROLLERAXISMOTION ? event.caxis.which:event.cbutton.which;
                for(int i=0;i<2;++i)if(ports[i].connected && ports[i].instance_id==id)event_port=i;
            }
            const auto detected=event_port>=0
                ? detect_controller_family(controller_ports.snapshot()[event_port].name.c_str())
                : ControllerFamily::Keyboard;
            // Research aid: with BLOCKPLANET_DUMP_DIR set, F2 saves the guest's 8 MB
            // of memory there (as held in memory: 32-bit words in host order).
            if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F2 &&
                std::getenv("BLOCKPLANET_DUMP_DIR") != nullptr && guest_rdram != nullptr) {
                static int dumps = 0;
                const auto path = std::filesystem::u8path(std::getenv("BLOCKPLANET_DUMP_DIR")) /
                                  ("rdram-" + std::to_string(++dumps) + ".bin");
                std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(guest_rdram), 0x800000);
                std::fprintf(stdout, "{\"event\":\"rdram_dump\",\"path\":\"%s\"}\n", path.string().c_str());
                std::fflush(stdout);
                continue;
            }
            if (handle_menu_event(event, event_port>=0, active_settings.bindings)) continue;
            handle_controls_event(event,event_port>=0 || removed_assigned,detected);
        }
        bool held=keyboard_state.buttons()!=0;
        for(const auto& port:controller_ports.snapshot()) {
            if(!port.connected)continue;
            const auto& s=port.sample;
            held=held || s.south || s.east || s.west || s.north || s.back || s.start ||
                s.dpad_up || s.dpad_down || s.dpad_left || s.dpad_right ||
                s.left_shoulder || s.right_shoulder || s.trigger_left>8000 || s.trigger_right>8000 ||
                std::abs(s.left_x)>8000 || std::abs(s.left_y)>8000;
        }
        if((controls_editor_blocks_game() || pc_settings_blocks_game()) && !controls_editor_active() && !pc_settings_active() && !held) {
            // Refresh the guest snapshot before releasing the input quarantine.
            // Otherwise the last polled held Apply button could enter the guest again.
            const auto current=controller_ports.snapshot();
            std::lock_guard lock(input_snapshot_mutex);
            input_snapshot=current;keyboard_snapshot=keyboard_state.buttons();
        }
        controls_editor_release_guard(held);
        if (blockplanet::ui::menus_open()) menus_block_game.store(true);
        else if (menus_block_game.load() && !held) {
            // As above: refresh the snapshot before letting input through.
            const auto current=controller_ports.snapshot();
            std::lock_guard lock(input_snapshot_mutex);
            input_snapshot=current;keyboard_snapshot=keyboard_state.buttons();
            menus_block_game.store(false);
        }
        while (auto command = blockplanet::ui::take_command()) {
            std::fprintf(stdout, "{\"event\":\"ui_command\",\"command\":\"%s\"}\n", command->c_str());
            std::fflush(stdout);
            if (*command == "exit") close_game();
            if (queue_menu_setting(*command)) continue;
            if (handle_profile_command(*command)) continue;
            if (*command == "story") {
                blockplanet::ui::set_text("chapters_title", "Story");
                blockplanet::ui::set_list("chapters", chapter_choices());
                blockplanet::ui::request_show("chapters");
                continue;
            }
            if (*command == "puzzle") {
                const auto found = profile_data.progress.chapters_completed.find("puzzle");
                const bool done = profile_save && found != profile_data.progress.chapters_completed.end() &&
                                  found->second.count(0) != 0;
                blockplanet::ui::set_text("chapters_title", "Puzzle");
                blockplanet::ui::set_list("chapters", puzzle_chapters(done));
                blockplanet::ui::request_show("chapters");
                continue;
            }
            if (command->rfind("puzzle_chapter:", 0) == 0) {
                const bool training = std::atoi(command->c_str() + 15) == 0;
                chapter_zero_lesson = training ? "puzzle" : "";
                continue_mode.reset();
                launch_mode(training ? LaunchMode::TrainingPuzzle : LaunchMode::Puzzle);
                continue;
            }
            if (command->rfind("chapter:", 0) == 0) {
                const int chapter = std::atoi(command->c_str() + 8);
                chapter_zero_lesson = chapter == 0 ? "basics" : "";
                continue_mode.reset();
                if (chapter == 0) {
                    launch_mode(LaunchMode::TrainingBasics);
                } else if (chapter <= rescue_chapters && game_option_applier) {
                    game_option_applier->choose_chapter(chapter);
                    launch_mode(LaunchMode::Rescue);
                }
                continue;
            }
            if (const auto mode = launch_mode_for_command(*command)) launch_mode(*mode);
        }
        if (mode_launcher.active() && guest_rdram != nullptr) {
            std::uint16_t press = 0;
            const auto result = mode_launcher.update(const_cast<std::uint8_t*>(guest_rdram),
                                                     std::chrono::steady_clock::now(), press);
            launcher_buttons.store(result == ModeLauncher::Result::Working ? (0x10000u | press) : 0u);
            if (result == ModeLauncher::Result::Started || result == ModeLauncher::Result::TimedOut) {
                std::fprintf(stdout, "{\"event\":\"mode_launch\",\"status\":\"%s\"}\n",
                             result == ModeLauncher::Result::Started ? "playing" : "timed_out");
                std::fflush(stdout);
            }
        }
        pc_settings_release_guard(held);
        poll_game_events();
        // After a Chapter 0, the mode's next part starts once the game is back
        // at its menu (Story at Chapter 1, or the puzzles).
        static auto last_continue_try = std::chrono::steady_clock::time_point{};
        if (continue_mode && !mode_launcher.active() && guest_rdram != nullptr && game_option_applier &&
            read_game_state(guest_rdram).scene >= 0 &&
            std::chrono::steady_clock::now() - last_continue_try > std::chrono::seconds(1)) {
            last_continue_try = std::chrono::steady_clock::now();
            if (*continue_mode == LaunchMode::Rescue) game_option_applier->choose_chapter(1);
            launch_mode(*continue_mode);
            if (mode_launcher.active()) continue_mode.reset();
        }
        const auto now = std::chrono::steady_clock::now();
        if (perf_log_enabled && now - perf_window_start >= perf_interval) {
            auto& metrics = perf_metrics();
            PerfWindow window;
            window.window_ms = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - perf_window_start).count());
            const auto vis = observed_vis.load(std::memory_order_relaxed);
            const auto rejections = tetrisphere_ai_fifo_rejections.load(std::memory_order_relaxed);
            window.vis = vis - perf_last_vis;
            window.audio_rejections = rejections - perf_last_rejections;
            window.display_list = metrics.display_list.take();
            window.display_list_rt64 = metrics.display_list_rt64.take();
            window.screen_update = metrics.screen_update.take();
            window.audio_wait = metrics.audio_wait.take();
            window.present_mode = perf_present_mode;
            const auto line = format_perf_report(window);
            std::fprintf(stdout, "%s\n", line.c_str());
            std::fflush(stdout);
            diagnostic_log().perf(line);
            perf_last_vis = vis;
            perf_last_rejections = rejections;
            perf_window_start = now;
        }
        if (now >= next_trophy_save) {
            save_trophies();
            save_profile();
            next_trophy_save = now + std::chrono::seconds(30);
        }
        if (report_audio && now >= next_audio_report) {
            const auto pacing = audio_output.pacing_snapshot();
            if (pacing.submissions != 0) {
                const auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - event_loop_start).count();
                std::fprintf(stdout,
                             "{\"event\":\"audio_pacing\",\"run_id\":\"%s\","
                             "\"uptime_ms\":%lld,\"requested_hz\":%u,\"obtained_hz\":%u,"
                             "\"device_buffer_frames\":%u,\"vis\":%llu,\"input_polls\":%llu,"
                             "\"audio_rsp_tasks\":%llu,\"audio_rsp_total_us\":%llu,\"audio_rsp_max_us\":%llu,"
                             "\"ai_attempts\":%llu,\"ai_fifo_rejections\":%llu,"
                             "\"ai_fifo_wait_events\":%llu,\"ai_fifo_waits_ms\":%llu,"
                             "\"ai_length_caps\":%llu,\"submissions\":%llu,"
                             "\"submitted_frames\":%llu,\"empty_software_queue_count\":%llu,"
                             "\"queued_frames\":%zu}\n",
                             run_id(), static_cast<long long>(uptime_ms),
                             pacing.requested_frequency, pacing.obtained_frequency,
                             static_cast<unsigned>(pacing.device_buffer_frames),
                             static_cast<unsigned long long>(observed_vis.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(input_polls.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(audio_rsp_tasks.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(audio_rsp_total_us.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(audio_rsp_max_us.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_attempts.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_fifo_rejections.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_fifo_wait_events.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_fifo_waits_ms.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(tetrisphere_ai_length_caps.load(std::memory_order_relaxed)),
                             static_cast<unsigned long long>(pacing.submissions),
                             static_cast<unsigned long long>(pacing.submitted_frames),
                             static_cast<unsigned long long>(pacing.empty_software_queue_count),
                             pacing.current_software_queue_frames);
                std::fflush(stdout);
            }
            next_audio_report = now + std::chrono::seconds(10);
        }
        SDL_Delay(2);
    }
}

} // namespace tetrisphere
