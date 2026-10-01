#pragma once
#include "../common/OnnxModel.hpp"
namespace lvt {
struct SpeechRegion { std::size_t start{}, end{}; unsigned localMask{}; float confidence{}; };
class OverlapDetector {
    OnnxModel model_;
    int window_{}, field_{}, stride_{};
public:
    explicit OverlapDetector(const ModelBundle& bundle);
    // Sliding 10-second analysis windows, 5-second hop; input is a bounded VAD unit.
    std::vector<SpeechRegion> analyze(std::span<const float> pcm16k, std::stop_token stop = {});
};
}
