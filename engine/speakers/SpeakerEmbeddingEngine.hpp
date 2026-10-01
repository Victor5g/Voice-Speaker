#pragma once
#include "../common/OnnxModel.hpp"
namespace lvt {
class SpeakerEmbeddingEngine {
    OnnxModel model_;
public:
    explicit SpeakerEmbeddingEngine(const ModelBundle& bundle) : model_(bundle) {}
    std::vector<float> extract(std::span<const float> mono16k, std::stop_token stop = {});
};
}
