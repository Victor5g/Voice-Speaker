#pragma once
#include "AudioTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <mach/mach_time.h>
namespace lvt {
// Worker-side 64-tap interpolation with history and lookahead across capture taps.
// Unsupported WebRTC rates such as 44.1/96 kHz become a continuous 16 kHz stream.
class StreamingResampler {
    std::array<std::array<float, 64>, 256> filters_{};
    std::vector<float> history_;
    double rate_{}, position_{}, ticksPerSecond_{};
    std::uint64_t expected_{}, firstFrame_{}, firstHost_{}, outputFrames_{};
public:
    StreamingResampler() {
        history_.reserve(audioBlockSize + 128);
        mach_timebase_info_data_t tb{}; mach_timebase_info(&tb); ticksPerSecond_ = 1e9 * tb.denom / tb.numer;
    }
    void clear() { history_.clear(); rate_ = 0; position_ = 0; expected_ = 0; outputFrames_ = 0; }
    void process(const AudioBlock& input, const std::function<void(const AudioBlock&)>& output) {
        if (!std::isfinite(input.sampleRate) || input.sampleRate < 8000 || input.sampleRate > 192000 || input.count > audioBlockSize) return;
        if (rate_ != input.sampleRate || expected_ != input.firstFrame) {
            clear(); rate_ = input.sampleRate; firstFrame_ = std::uint64_t(input.firstFrame * 16000 / rate_); firstHost_ = input.hostTime;
            constexpr double pi = 3.14159265358979323846;
            const double cutoff = .45 * std::min(1.0, 16000 / rate_);
            for (std::size_t phase = 0; phase < 256; ++phase) {
                double sum = 0;
                for (int j = 0; j < 64; ++j) {
                    const double x = j - 31 - phase / 256.0;
                    const double sinc = std::abs(x) < 1e-9 ? 2 * cutoff : std::sin(2 * pi * cutoff * x) / (pi * x);
                    filters_[phase][j] = float(sinc * (.5 + .5 * std::cos(pi * x / 32))); sum += filters_[phase][j];
                }
                for (auto& coefficient : filters_[phase]) coefficient /= float(sum);
            }
        }
        expected_ = input.firstFrame + input.count;
        history_.insert(history_.end(), input.samples.begin(), input.samples.begin() + input.count);
        AudioBlock block;
        auto emit = [&] {
            if (!block.count) return;
            block.sampleRate = 16000; block.firstFrame = firstFrame_ + outputFrames_;
            block.hostTime = firstHost_ ? firstHost_ + std::uint64_t(outputFrames_ * ticksPerSecond_ / 16000) : 0;
            outputFrames_ += block.count; output(block); block = {};
        };
        while (position_ + 32 < history_.size()) {
            const auto center = std::int64_t(position_);
            const auto phase = std::min<std::size_t>(255, std::size_t((position_ - center) * 256));
            double value = 0;
            for (int j = 0; j < 64; ++j) {
                const auto sample = center + j - 31;
                if (sample >= 0 && std::size_t(sample) < history_.size()) value += history_[sample] * filters_[phase][j];
            }
            block.samples[block.count++] = float(value); position_ += rate_ / 16000;
            if (block.count == audioBlockSize) emit();
        }
        emit();
        const auto remove = std::min(history_.size(), std::size_t(std::max(0.0, std::floor(position_) - 32)));
        history_.erase(history_.begin(), history_.begin() + remove); position_ -= remove;
    }
};
}
