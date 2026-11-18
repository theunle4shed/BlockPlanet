// Developer tools: the game frozen and stepped a frame at a time
// (include/tetrisphere/frame_step.h).
#include "tetrisphere/frame_step.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace tetrisphere::frame_step {
namespace {
std::atomic<bool> on{false};
std::atomic<bool> held{false};
std::atomic<int> steps{0};
std::atomic<bool> in_wait{false};
std::mutex pauser_mutex;
std::function<void(bool)> pauser;

void pause_picture(bool pause) {
    const std::lock_guard lock(pauser_mutex);
    if (pauser) pauser(pause);
}
} // namespace

std::mutex driver_mutex;
std::function<bool(const CameraMove&)> driver;
std::atomic<bool> sticks{false};
std::atomic<int> scene_request{0};

void request_scene(int change) { scene_request.store(change); }

void set_camera_driver(std::function<bool(const CameraMove&)> drive) {
    const std::lock_guard lock(driver_mutex);
    driver = std::move(drive);
    if (!driver) sticks.store(false);
}
bool drive_camera(const CameraMove& given) {
    if (!on.load()) return false;
    CameraMove move = given;
    if (const int change = scene_request.exchange(0); change != 0) move.scene = change;
    const std::lock_guard lock(driver_mutex);
    const bool active = driver && driver(move);
    if (active != sticks.load()) {
        std::fprintf(stdout, "{\"event\":\"free_camera_pad\",\"on\":%s,\"forward\":%.4f,\"yaw\":%.4f}\n", active ? "true" : "false",
                     move.forward, move.yaw);
        std::fflush(stdout);
    }
    sticks.store(active);
    static const bool log = std::getenv("BLOCKPLANET_FREE_CAMERA_LOG") != nullptr;  // research
    static int calls = 0;
    static float yaw = 0;
    static auto last = std::chrono::steady_clock::now();
    if (log) {
        ++calls, yaw += move.yaw;
        if (std::chrono::steady_clock::now() - last > std::chrono::seconds(1)) {
            last = std::chrono::steady_clock::now();
            std::fprintf(stdout, "{\"event\":\"free_camera_calls\",\"calls\":%d,\"yaw\":%.3f,\"active\":%s,\"held\":%s}\n", calls, yaw,
                         active ? "true" : "false", held.load() ? "true" : "false");
            std::fflush(stdout);
        }
    }
    return active;
}
bool camera_has_sticks() { return sticks.load(std::memory_order_relaxed); }

void set_enabled(bool developer) { on.store(developer); }
bool enabled() { return on.load(); }
void set_frozen(bool frozen) {
    if (!on.load()) return;
    held.store(frozen);
    if (!frozen) {
        steps.store(0);
        pause_picture(false);
    }
    std::fprintf(stdout, "{\"event\":\"frame_step\",\"frozen\":%s}\n", frozen ? "true" : "false");
    std::fflush(stdout);
}
bool frozen() { return held.load(); }
bool waiting() { return in_wait.load(); }
void step() {
    if (!on.load()) return;
    if (!held.load()) {
        set_frozen(true);
        return;
    }
    if (steps.load() < 2) steps.fetch_add(1);
}
void set_picture_pauser(std::function<void(bool)> pause) {
    const std::lock_guard lock(pauser_mutex);
    pauser = std::move(pause);
}

void gate(const std::function<void()>& present) {
    if (!on.load(std::memory_order_relaxed) || !held.load(std::memory_order_relaxed)) return;
    // The frame drawn last stays on screen, RT64's picture paused on it, so
    // its inspector and free camera look at it. The game waits meanwhile (for
    // this display list to be drawn).
    static unsigned long long frames = 0;
    pause_picture(true);
    std::fprintf(stdout, "{\"event\":\"frame_step_frame\",\"n\":%llu}\n", frames++);
    std::fflush(stdout);
    in_wait.store(true);
    // The picture kept coming meanwhile (RT64 shows its paused frame again,
    // with the inspector and the free camera's view), about 60 times a second:
    // the game's graphics thread, held here, is what presents the screen.
    auto next = std::chrono::steady_clock::now();
    while (on.load() && held.load() && steps.load() == 0) {
        if (present && std::chrono::steady_clock::now() >= next) {
            present();
            next = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    in_wait.store(false);
    if (held.load()) steps.fetch_sub(1);
    pause_picture(false);  // this one is drawn
}

} // namespace tetrisphere::frame_step
