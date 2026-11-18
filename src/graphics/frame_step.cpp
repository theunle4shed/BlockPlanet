// Developer tools: the game frozen and stepped a frame at a time
// (include/tetrisphere/frame_step.h).
#include "tetrisphere/frame_step.h"

#include <atomic>
#include <chrono>
#include <cstdio>
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

void gate() {
    if (!on.load(std::memory_order_relaxed) || !held.load(std::memory_order_relaxed)) return;
    // The frame drawn last stays on screen, RT64's picture paused on it, so
    // its inspector and free camera look at it. The game waits meanwhile (for
    // this display list to be drawn).
    static unsigned long long frames = 0;
    pause_picture(true);
    std::fprintf(stdout, "{\"event\":\"frame_step_frame\",\"n\":%llu}\n", frames++);
    std::fflush(stdout);
    in_wait.store(true);
    while (held.load() && steps.load() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    in_wait.store(false);
    if (held.load()) steps.fetch_sub(1);
    pause_picture(false);  // this one is drawn
}

} // namespace tetrisphere::frame_step
