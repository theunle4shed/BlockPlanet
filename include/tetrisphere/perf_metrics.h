#pragma once

// BlockPlanet frame-timing diagnostics.
//
// Timers are always recorded (a steady_clock read costs tens of nanoseconds),
// but a report is only printed when the player asks for one with --perf-log
// or TETRISPHERE_PERF_METRICS=1. Each report covers the window since the
// previous one, so slow scenes are not averaged away by quiet menus.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace tetrisphere {

struct PerfSample {
    std::uint64_t count = 0;
    std::uint64_t total_us = 0;
    std::uint64_t max_us = 0;
    std::uint64_t over_threshold = 0;
};

// Lock-free duration accumulator. record() may run on any thread.
class PerfTimer {
public:
    explicit constexpr PerfTimer(std::uint64_t threshold_us) : threshold_us_(threshold_us) {}

    void record(std::uint64_t elapsed_us) {
        count_.fetch_add(1, std::memory_order_relaxed);
        total_us_.fetch_add(elapsed_us, std::memory_order_relaxed);
        if (elapsed_us > threshold_us_)
            over_threshold_.fetch_add(1, std::memory_order_relaxed);
        auto previous = max_us_.load(std::memory_order_relaxed);
        while (elapsed_us > previous &&
               !max_us_.compare_exchange_weak(previous, elapsed_us,
                                              std::memory_order_relaxed)) {}
    }

    // Return the current window and start a new one.
    PerfSample take() {
        PerfSample sample;
        sample.count = count_.exchange(0, std::memory_order_relaxed);
        sample.total_us = total_us_.exchange(0, std::memory_order_relaxed);
        sample.max_us = max_us_.exchange(0, std::memory_order_relaxed);
        sample.over_threshold = over_threshold_.exchange(0, std::memory_order_relaxed);
        return sample;
    }

    std::uint64_t threshold_us() const { return threshold_us_; }

private:
    const std::uint64_t threshold_us_;
    std::atomic<std::uint64_t> count_{0};
    std::atomic<std::uint64_t> total_us_{0};
    std::atomic<std::uint64_t> max_us_{0};
    std::atomic<std::uint64_t> over_threshold_{0};
};

struct PerfMetrics {
    // One N64 frame at 60 Hz. A graphics task slower than this can delay the
    // game's next frame, because the game waits for the RDP to finish.
    static constexpr std::uint64_t frame_us = 16'667;

    // Whole graphics task on the renderer thread: BlockPlanet's display-list
    // fixes plus RT64 parsing. The count is the number of frames the game drew.
    PerfTimer display_list{frame_us};
    // RT64 processDisplayLists alone.
    PerfTimer display_list_rt64{frame_us};
    // RT64 updateScreen, which runs on the same thread once per VI.
    PerfTimer screen_update{frame_us};
    // Game-thread time blocked in osAiSetNextBuffer waiting for audio space.
    PerfTimer audio_wait{frame_us};
};

inline PerfMetrics& perf_metrics() {
    static PerfMetrics metrics;
    return metrics;
}

// Times a scope with the steady clock and records it on destruction.
class ScopedPerfTimer {
public:
    explicit ScopedPerfTimer(PerfTimer& timer)
        : timer_(timer), start_(std::chrono::steady_clock::now()) {}
    ~ScopedPerfTimer() {
        timer_.record(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_).count()));
    }
    ScopedPerfTimer(const ScopedPerfTimer&) = delete;
    ScopedPerfTimer& operator=(const ScopedPerfTimer&) = delete;

private:
    PerfTimer& timer_;
    std::chrono::steady_clock::time_point start_;
};

struct PerfWindow {
    std::uint64_t window_ms = 0;
    std::uint64_t vis = 0;
    PerfSample display_list;
    PerfSample display_list_rt64;
    PerfSample screen_update;
    PerfSample audio_wait;
    std::uint64_t audio_rejections = 0;
    const char* present_mode = "console";
};

namespace perf_detail {

inline void append_timer(std::string& out, const char* name, const PerfSample& s) {
    char buffer[192];
    const double average_ms = s.count == 0 ? 0.0
        : static_cast<double>(s.total_us) / static_cast<double>(s.count) / 1000.0;
    std::snprintf(buffer, sizeof(buffer),
                  ",\"%s\":{\"count\":%llu,\"avg_ms\":%.2f,\"max_ms\":%.2f,"
                  "\"total_ms\":%.1f,\"over_16ms\":%llu}",
                  name, static_cast<unsigned long long>(s.count), average_ms,
                  static_cast<double>(s.max_us) / 1000.0,
                  static_cast<double>(s.total_us) / 1000.0,
                  static_cast<unsigned long long>(s.over_threshold));
    out += buffer;
}

inline double per_second(std::uint64_t count, std::uint64_t window_ms) {
    return window_ms == 0 ? 0.0
        : static_cast<double>(count) * 1000.0 / static_cast<double>(window_ms);
}

} // namespace perf_detail

// One JSON line. game_fps is how many frames the game itself drew; vi_hz is
// the emulated video interrupt rate, which should stay close to 60.
inline std::string format_perf_report(const PerfWindow& window) {
    std::string out;
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"event\":\"perf_window\",\"window_ms\":%llu,"
                  "\"present_mode\":\"%s\",\"game_fps\":%.1f,\"vi_hz\":%.1f",
                  static_cast<unsigned long long>(window.window_ms),
                  window.present_mode,
                  perf_detail::per_second(window.display_list.count, window.window_ms),
                  perf_detail::per_second(window.vis, window.window_ms));
    out += buffer;
    perf_detail::append_timer(out, "display_list", window.display_list);
    perf_detail::append_timer(out, "display_list_rt64", window.display_list_rt64);
    perf_detail::append_timer(out, "screen_update", window.screen_update);
    perf_detail::append_timer(out, "audio_wait", window.audio_wait);
    std::snprintf(buffer, sizeof(buffer), ",\"audio_rejections\":%llu}",
                  static_cast<unsigned long long>(window.audio_rejections));
    out += buffer;
    return out;
}

} // namespace tetrisphere
