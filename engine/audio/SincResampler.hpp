#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <vector>

namespace lvt {
// Worker-side band-limited segment resampling; no hardware/global rate changes.
class SincResampler {
    std::array<std::array<float, 64>, 256> filters_{};
    double rate_{}, outputRate_{};
public:
    std::vector<float> to16k(std::span<const float> input, double rate) {
        return convert(input, rate, 16000);
    }
    std::vector<float> convert(std::span<const float> input, double rate, double outputRate) {
        if (!std::isfinite(rate) || rate < 8000 || rate > 192000 || input.size() > rate * 30 ||
            !std::isfinite(outputRate) || outputRate < 8000 || outputRate > 192000)
            throw std::runtime_error("Invalid recognition audio rate or segment length");
        if (rate == outputRate) return {input.begin(), input.end()};
        constexpr double pi = 3.14159265358979323846;
        if (rate_ != rate || outputRate_ != outputRate) {
            rate_ = rate; outputRate_ = outputRate;
            const double cutoff = 0.45 * std::min(1.0, outputRate / rate);
            for (std::size_t phase = 0; phase < filters_.size(); ++phase) {
                double sum = 0;
                for (int j = 0; j < 64; ++j) {
                    const double x = j - 31 - phase / 256.0;
                    const double sinc = std::abs(x) < 1e-9 ? 2 * cutoff : std::sin(2 * pi * cutoff * x) / (pi * x);
                    filters_[phase][j] = float(sinc * (0.5 + 0.5 * std::cos(pi * x / 32)));
                    sum += filters_[phase][j];
                }
                for (auto& c : filters_[phase]) c = float(c / sum);
            }
        }
        std::vector<float> output(std::size_t(std::llround(input.size() * outputRate / rate)));
        for (std::size_t i = 0; i < output.size(); ++i) {
            const double position = i * rate / outputRate;
            const auto center = std::int64_t(position);
            const auto phase = std::min<std::size_t>(255, std::size_t((position - center) * 256));
            double value = 0;
            for (int j = 0; j < 64; ++j) {
                const auto sample = center + j - 31;
                if (sample >= 0 && std::size_t(sample) < input.size()) value += input[std::size_t(sample)] * filters_[phase][j];
            }
            output[i] = float(value);
        }
        return output;
    }
};
}
