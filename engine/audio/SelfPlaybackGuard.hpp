#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace lvt {
// Worker-only, conservative residual-echo detector, secondary to VoiceProcessingIO.
// Both streams must be sampled at 8 kHz. Keeps 2 seconds; searches <= 500 ms delay.
// Never gates all microphone audio just because playback is active.
class SelfPlaybackGuard {
    std::array<float, 16384> history_{};
    std::size_t written_{};
public:
    void appendReference(std::span<const float> samples) noexcept {
        for (float sample : samples) history_[written_++ % history_.size()] = sample;
    }
    bool isSelfPlayback(std::span<const float> input) const noexcept {
        const auto available = std::min(written_, history_.size());
        if (input.size() < 80 || input.size() > available) return false;
        double mean = 0, energy = 0;
        for (float x : input) mean += x;
        mean /= input.size();
        for (float x : input) energy += (x - mean) * (x - mean);
        if (energy < input.size() * 1e-8) return false;
        const auto newestStart = written_ - input.size();
        const auto oldestStart = std::max(written_ - available, newestStart > 4000 ? newestStart - 4000 : 0);
        for (std::size_t start = newestStart;; --start) {
            double refSum = 0, refSquare = 0, cross = 0;
            for (std::size_t i = 0; i < input.size(); ++i) {
                double ref = history_[(start + i) % history_.size()];
                refSum += ref; refSquare += ref * ref;
                cross += (input[i] - mean) * ref;
            }
            const double refEnergy = refSquare - refSum * refSum / input.size();
            if (refEnergy > input.size() * 1e-8) {
                const double explained = cross * cross / (energy * refEnergy);
                // Near-perfect match only; double-talk keeps reaching the pipeline.
                if (explained >= 0.995 && std::abs(cross / refEnergy) <= 4.0) return true;
            }
            if (start == oldestStart) break;
        }
        return false;
    }
    void clear() noexcept { history_.fill(0); written_ = 0; }
};

// Streaming averaging downsampler for the conservative echo-match branch only.
// Recognition keeps its original PCM and will use the ASR backend's resampler.
class EchoReferenceSampler {
    double phase_{}, sum_{};
    unsigned count_{};
public:
    std::size_t process(std::span<const float> input, double rate, std::span<float> output) noexcept {
        if (rate < 8000 || !std::isfinite(rate)) return 0;
        std::size_t n = 0;
        for (float x : input) {
            sum_ += x; ++count_; phase_ += 8000;
            if (phase_ >= rate) {
                phase_ -= rate;
                if (n < output.size()) output[n++] = float(sum_ / count_);
                sum_ = 0; count_ = 0;
            }
        }
        return n;
    }
    void clear() noexcept { phase_ = 0; sum_ = 0; count_ = 0; }
};
}
