#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../common/PipelineTiming.hpp"
namespace lvt {
struct SegmentIdentity {
    std::uint64_t session{}, segment{}, captureHostTime{}, firstFrame{};
    std::uint32_t revision{1}, speaker{};
    std::uint64_t sourceSegment{};
    PipelineTiming timing{};
};
struct TranslationResult {
    SegmentIdentity identity;
    std::string original, translated, sourceLanguage, targetLanguage;
    double captureDurationMs{}, asrLatencyMs{}, translationLatencyMs{};
};
struct SynthesizedAudio {
    TranslationResult translation;
    std::vector<float> pcm;
    int sampleRate{};
    std::string voice;
    double synthesisLatencyMs{};
};
struct ConversationEvent {
    TranslationResult result;
    std::string state, error, voice;
    std::uint64_t playbackHostTime{};
    double synthesisLatencyMs{}, firstAudioMs{}, endToEndMs{};
};
struct PlaybackEvent {
    SegmentIdentity identity;
    std::uint64_t hostTime{};
    bool started{};
    bool audible{};
};
}
