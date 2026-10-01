#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include "../common/PipelineTiming.hpp"

namespace lvt {
constexpr std::size_t audioBlockSize = 2048;
struct AudioBlock {
    std::array<float, audioBlockSize> samples{};
    std::uint32_t count{};
    double sampleRate{};
    std::uint64_t hostTime{}, firstFrame{};
};
struct SpeechSegment {
    std::vector<float> samples;
    double sampleRate{};
    std::uint64_t firstFrame{}, hostTime{};
    std::uint32_t speaker{};
    bool overlap{};
    std::uint64_t utterance{};
    std::uint32_t revision{};
    bool final{true};
    PipelineTiming timing{};
};
}
