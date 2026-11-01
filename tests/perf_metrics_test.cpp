#include "tetrisphere/audio_length_pacer.h"
#include "tetrisphere/perf_metrics.h"

#include <cassert>
#include <chrono>
#include <string>

namespace {

void timer_windows() {
    tetrisphere::PerfTimer timer{16'667};
    timer.record(10'000);
    timer.record(20'000);
    timer.record(5'000);
    const auto first = timer.take();
    assert(first.count == 3);
    assert(first.total_us == 35'000);
    assert(first.max_us == 20'000);
    assert(first.over_threshold == 1);
    const auto second = timer.take();
    assert(second.count == 0 && second.total_us == 0 && second.max_us == 0 &&
           second.over_threshold == 0);
}

void report_format() {
    tetrisphere::PerfWindow window;
    window.window_ms = 5'000;
    window.vis = 300;
    window.display_list = {150, 300'000, 25'000, 2};
    window.screen_update = {300, 60'000, 1'000, 0};
    window.audio_rejections = 1;
    window.present_mode = "skip-buffering";
    const auto line = tetrisphere::format_perf_report(window);
    assert(line.front() == '{' && line.back() == '}');
    assert(line.find("\"event\":\"perf_window\"") != std::string::npos);
    assert(line.find("\"present_mode\":\"skip-buffering\"") != std::string::npos);
    assert(line.find("\"game_fps\":30.0") != std::string::npos);
    assert(line.find("\"vi_hz\":60.0") != std::string::npos);
    assert(line.find("\"display_list\":{\"count\":150,\"avg_ms\":2.00,"
                     "\"max_ms\":25.00,\"total_ms\":300.0,\"over_16ms\":2}") !=
           std::string::npos);
    assert(line.find("\"audio_wait\":{\"count\":0,\"avg_ms\":0.00") != std::string::npos);
    assert(line.find("\"audio_rejections\":1}") != std::string::npos);
    tetrisphere::PerfWindow empty;
    assert(tetrisphere::format_perf_report(empty).find("\"game_fps\":0.0") !=
           std::string::npos);
}

void audio_deadline_uses_elapsed_time() {
    using namespace std::chrono_literals;
    using TimePoint = std::chrono::steady_clock::time_point;
    // Each simulated sleep lasts a whole Windows timer tick. Counting sleeps
    // would allow 20 of them (over 300 ms); the deadline allows only two.
    TimePoint clock{};
    int sleeps = 0;
    const bool stalled = tetrisphere::wait_for_audio_fifo_slot_until(
        [] { return true; }, [&] { return clock; },
        [&] { ++sleeps; clock += 15'625us; }, TimePoint{} + 20ms);
    assert(!stalled && sleeps == 2);

    clock = TimePoint{};
    int checks = 0;
    const bool drained = tetrisphere::wait_for_audio_fifo_slot_until(
        [&] { return ++checks < 3; }, [&] { return clock; },
        [&] { clock += 1ms; }, TimePoint{} + 20ms);
    assert(drained && checks == 3 && clock == TimePoint{} + 2ms);

    clock = TimePoint{};
    const bool immediate = tetrisphere::wait_for_audio_fifo_slot_until(
        [] { return false; }, [&] { return clock; },
        [] { assert(false); }, TimePoint{});
    assert(immediate);
}

} // namespace

int main() {
    timer_windows();
    report_format();
    audio_deadline_uses_elapsed_time();
    return 0;
}
