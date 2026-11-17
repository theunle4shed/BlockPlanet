#include "tetrisphere/audio_spectrum.h"

#include <cassert>
#include <cmath>
#include <vector>

using namespace tetrisphere::audio_spectrum;

int main() {
    // A low tone lights the low bands, a high tone the high ones.
    std::vector<float> low(1024), high(1024);
    for (std::size_t i = 0; i < 1024; ++i) {
        low[i] = 0.8f * std::sin(2.0f * 3.14159265f * 80.0f * float(i) / 44100.0f);
        high[i] = 0.8f * std::sin(2.0f * 3.14159265f * 8000.0f * float(i) / 44100.0f);
    }
    const auto a = analyse(low.data(), low.size()), b = analyse(high.data(), high.size());
    assert(a[1] > 0.7f && a[13] < a[1] - 0.3f);
    assert(b[13] > 0.7f || b[14] > 0.7f);
    assert(b[1] < 0.5f);
    // Silence is zero everywhere.
    std::vector<float> quiet(1024, 0.0f);
    for (float v : analyse(quiet.data(), quiet.size())) assert(v == 0.0f);
    // Pushed samples reach the smoothed bands.
    std::vector<std::int16_t> stereo(2048);
    for (std::size_t i = 0; i < 1024; ++i) stereo[2 * i] = stereo[2 * i + 1] = std::int16_t(20000 * std::sin(2.0f * 3.14159265f * 80.0f * float(i) / 44100.0f));
    push(stereo.data(), stereo.size());
    assert(bands()[1] > 0.5f && level() > 0.1f);
    return 0;
}
