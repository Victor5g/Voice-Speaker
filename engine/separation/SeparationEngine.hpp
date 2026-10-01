#pragma once
#include "../common/OnnxModel.hpp"
#include <array>
namespace lvt {
struct SeparatedAudio { std::array<std::vector<float>, 2> sources; double consistencyError{}; bool reliable{}; };
class SeparationEngine {
    OnnxModel model_;
public:
    explicit SeparationEngine(const ModelBundle& bundle) : model_(bundle, 2) {}
    SeparatedAudio separate(std::span<const float> pcm16k, std::stop_token stop = {});
};
}
