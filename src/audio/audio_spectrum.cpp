#include "tetrisphere/audio_spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <mutex>
#include <vector>

namespace tetrisphere::audio_spectrum {
namespace {
constexpr std::size_t window = 1024;
std::mutex mutex;
std::array<float, window> ring{};
std::size_t head = 0;
std::array<float, band_count> smoothed{};
float smoothed_level = 0.0f;

void fft(std::vector<std::complex<float>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * 3.14159265f / float(len);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v, a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}
} // namespace

void push(const std::int16_t* samples, std::size_t count) {
    if (samples == nullptr) return;
    std::lock_guard lock(mutex);
    for (std::size_t i = 0; i + 1 < count; i += 2) {
        ring[head] = (float(samples[i]) + float(samples[i + 1])) / 65536.0f;
        head = (head + 1) % window;
    }
}

std::array<float, band_count> analyse(const float* mono, std::size_t count, int frequency) {
    std::array<float, band_count> out{};
    std::size_t n = 1;
    while (n * 2 <= count) n *= 2;
    if (n < 64) return out;
    std::vector<std::complex<float>> a(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float hann = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * float(i) / float(n - 1));
        a[i] = {mono[i] * hann, 0.0f};
    }
    fft(a);
    // Bands spaced evenly in pitch from 40 Hz to 16 kHz.
    const float lo = 40.0f, hi = std::min(16000.0f, float(frequency) / 2.0f);
    for (std::size_t b = 0; b < band_count; ++b) {
        const float f0 = lo * std::pow(hi / lo, float(b) / band_count), f1 = lo * std::pow(hi / lo, float(b + 1) / band_count);
        std::size_t k0 = std::max<std::size_t>(1, std::size_t(f0 * n / frequency)), k1 = std::max(k0 + 1, std::size_t(f1 * n / frequency));
        k1 = std::min(k1, n / 2);
        float peak = 0.0f;
        for (std::size_t k = k0; k < k1; ++k) peak = std::max(peak, std::abs(a[k]));
        // To decibels, -60 dB to 0 as 0 to 1 (a full-scale sine reads about n/4).
        const float db = 20.0f * std::log10(std::max(1e-9f, peak / (float(n) / 4.0f)));
        out[b] = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    }
    return out;
}

std::array<float, band_count> bands() {
    std::array<float, window> copy;
    std::size_t at;
    {
        std::lock_guard lock(mutex);
        copy = ring, at = head;
    }
    std::array<float, window> mono;
    for (std::size_t i = 0; i < window; ++i) mono[i] = copy[(at + i) % window];
    const auto now = analyse(mono.data(), window);
    std::lock_guard lock(mutex);
    for (std::size_t b = 0; b < band_count; ++b)
        smoothed[b] = now[b] > smoothed[b] ? now[b] : smoothed[b] * 0.88f + now[b] * 0.12f;  // fast up, slow down
    float sum = 0.0f;
    for (float v : mono) sum += v * v;
    const float rms = std::sqrt(sum / window);
    const float lvl = std::clamp(rms * 4.0f, 0.0f, 1.0f);
    smoothed_level = lvl > smoothed_level ? lvl : smoothed_level * 0.9f + lvl * 0.1f;
    return smoothed;
}

float level() {
    std::lock_guard lock(mutex);
    return smoothed_level;
}

} // namespace tetrisphere::audio_spectrum
