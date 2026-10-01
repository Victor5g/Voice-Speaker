#pragma once
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace lvt {
// Measurement detector, NOT a speech detector or a playback gate. Two complete
// 10 ms windows at RMS >= -50 dBFS; reports the start of the first window.
// Run only on prepared PCM outside the realtime callback. Audio is unmodified.
inline std::size_t audioOnsetFrame(std::span<const float> pcm, double rate) {
    const auto absent = std::numeric_limits<std::size_t>::max();
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000) return absent;
    const auto window = std::size_t(std::ceil(rate * .01));
    bool previous = false;
    for (std::size_t start = 0; start + window <= pcm.size(); start += window) {
        double energy = 0;
        for (std::size_t i = start; i < start + window; ++i) energy += double(pcm[i]) * pcm[i];
        const bool signal = std::isfinite(energy) && energy / window >= 1e-5;
        if (signal && previous) return start - window;
        previous = signal;
    }
    return absent;
}
}
