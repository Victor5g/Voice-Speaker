#include "SeparationEngine.hpp"
#include <cmath>
namespace lvt {
SeparatedAudio SeparationEngine::separate(std::span<const float> input, std::stop_token stop) {
    if (input.size() < 1600 || input.size() > 16000 * 10) throw std::runtime_error("Invalid separation unit");
    // The full bounded speech unit supplies context for Portuguese separation.
    // Two-second inference windows degraded measured SI-SDR on the PT fixture.
    auto admission = MemoryBudget::shared().claim(2048ULL << 20);
    const std::size_t window = input.size(), overlap = std::min<std::size_t>(8000, window / 4), hop = window - overlap;
    SeparatedAudio result; for (auto& source : result.sources) source.assign(input.size(), 0);
    std::vector<float> weights(input.size(), 0);
    for (std::size_t offset = 0; offset < input.size() && !stop.stop_requested(); offset += hop) {
        std::vector<float> chunk(window, 0);
        const auto count = std::min(window, input.size() - offset);
        std::copy_n(input.begin() + offset, count, chunk.begin());
        const std::array<std::int64_t, 2> shape{1, std::int64_t(window)};
        auto prediction = model_.run(chunk, shape, (window + 1024) * 2, stop);
        if (stop.stop_requested()) return {};
        if (prediction.shape.size() != 3 || prediction.shape[0] != 1 || prediction.shape[1] != 2 || prediction.shape[2] < std::int64_t(window))
            throw std::runtime_error("Invalid two-source separator output");
        const std::size_t length = prediction.shape[2];
        // Conv-TasNet trained with SI-SDR has an arbitrary scale and polarity for
        // each output. Restore those two scalar gains against the original mixture
        // before evaluating consistency or stitching chunks; never clip raw output.
        double aa = 0, bb = 0, ab = 0, ax = 0, bx = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const double a = prediction.values[i], b = prediction.values[length + i];
            if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 1e20 || std::abs(b) > 1e20) throw std::runtime_error("Invalid separator tensor");
            aa += a * a; bb += b * b; ab += a * b; ax += a * chunk[i]; bx += b * chunk[i];
        }
        const double determinant = aa * bb - ab * ab;
        if (determinant <= std::max(aa * bb * 1e-8, 1e-20)) return {};
        const double gainA = (ax * bb - bx * ab) / determinant, gainB = (bx * aa - ax * ab) / determinant;
        for (std::size_t i = 0; i < count; ++i) {
            prediction.values[i] *= float(gainA); prediction.values[length + i] *= float(gainB);
        }
        bool swap = false;
        if (offset) {
            double same = 0, reversed = 0;
            for (std::size_t i = 0; i < std::min(count, overlap); ++i) {
                const auto position = offset + i;
                if (weights[position] <= 0) continue;
                const auto a = result.sources[0][position] / weights[position], b = result.sources[1][position] / weights[position];
                const auto c = prediction.values[i], d = prediction.values[length + i];
                same += (a - c) * (a - c) + (b - d) * (b - d);
                reversed += (a - d) * (a - d) + (b - c) * (b - c);
            }
            swap = reversed < same;
        }
        for (std::size_t i = 0; i < count; ++i) {
            float weight = 1;
            if (offset && i < overlap) weight = float(i + 1) / overlap;
            if (offset + count < input.size() && i >= hop) weight = std::min(weight, float(window - i) / overlap);
            weights[offset + i] += weight;
            for (std::size_t source = 0; source < 2; ++source) {
                const float v = prediction.values[(swap ? 1 - source : source) * length + i];
                if (!std::isfinite(v) || std::abs(v) > 4) throw std::runtime_error("Invalid separated PCM");
                result.sources[source][offset + i] += weight * v;
            }
        }
        if (offset + count == input.size()) break;
    }
    if (stop.stop_requested()) return {};
    double mixtureEnergy = 0, error = 0, energyA = 0, energyB = 0, cross = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        for (auto& source : result.sources) source[i] /= std::max(weights[i], 1e-9f);
        const auto a = result.sources[0][i], b = result.sources[1][i];
        mixtureEnergy += input[i] * input[i]; error += (input[i] - a - b) * (input[i] - a - b);
        energyA += a * a; energyB += b * b; cross += a * b;
    }
    result.consistencyError = error / std::max(mixtureEnergy, 1e-12);
    const double correlation = std::abs(cross) / std::sqrt(std::max(energyA * energyB, 1e-12));
    result.reliable = result.consistencyError < .15 && correlation < .8 && std::min(energyA, energyB) > .02 * std::max(energyA, energyB);
    return result;
}
}
