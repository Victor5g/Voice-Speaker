#include "OverlapDetector.hpp"
#include <array>
#include <cmath>
namespace lvt {
OverlapDetector::OverlapDetector(const ModelBundle& bundle) : model_(bundle) {
    if (model_.metadata("sample_rate") != "16000" || model_.metadata("num_classes") != "7" || model_.metadata("powerset_max_classes") != "2")
        throw std::runtime_error("Unsupported Pyannote segmentation contract");
    window_ = std::stoi(model_.metadata("window_size")); field_ = std::stoi(model_.metadata("receptive_field_size"));
    stride_ = std::stoi(model_.metadata("receptive_field_shift"));
    if (window_ != 160000 || field_ <= 0 || stride_ <= 0) throw std::runtime_error("Unsupported overlap frame geometry");
}
std::vector<SpeechRegion> OverlapDetector::analyze(std::span<const float> pcm, std::stop_token stop) {
    if (pcm.empty() || pcm.size() > 16000 * 30) throw std::runtime_error("Invalid overlap window");
    std::vector<SpeechRegion> regions;
    constexpr unsigned masks[] = {0, 1, 2, 4, 3, 5, 6};
    for (std::size_t offset = 0; offset < pcm.size() && !stop.stop_requested(); offset += window_ / 2) {
        std::vector<float> window(window_, 0);
        std::copy_n(pcm.begin() + offset, std::min<std::size_t>(window_, pcm.size() - offset), window.begin());
        const std::array<std::int64_t, 3> shape{1, 1, window_};
        auto result = model_.run(window, shape, std::size_t(window_) * 7, stop);
        if (stop.stop_requested()) return {};
        if (result.shape.size() != 3 || result.shape[0] != 1 || result.shape[2] != 7) throw std::runtime_error("Invalid overlap output");
        const auto validEnd = std::min(pcm.size(), offset + std::size_t(window_ / 2));
        for (std::size_t t = 0; t < std::size_t(result.shape[1]); ++t) {
            const std::size_t start = offset + t * stride_;
            if (start >= validEnd) break;
            const auto begin = result.values.begin() + t * 7;
            const auto best = std::max_element(begin, begin + 7);
            double total = 0; for (auto v = begin; v != begin + 7; ++v) total += std::exp(*v - *best);
            const float confidence = float(1 / total);
            const auto mask = confidence >= .5f ? masks[best - begin] : 0;
            const std::size_t end = std::min(validEnd, start + std::size_t(stride_));
            if (!regions.empty() && regions.back().localMask == mask && regions.back().end == start && start != offset) {
                regions.back().end = end; regions.back().confidence = std::min(regions.back().confidence, confidence);
            } else regions.push_back({start, end, mask, confidence});
        }
        // Padding is only inference context; no fabricated speech is emitted past valid input.
    }
    return regions;
}
}
