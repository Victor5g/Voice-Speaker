#pragma once
#include "../common/ModelAssets.hpp"
#include "../common/MemoryBudget.hpp"
#include <stop_token>
struct SherpaOnnxOfflineTts;
namespace lvt {
struct VoiceAudio { std::vector<float> samples; int sampleRate{}; double latencyMs{}; };
class TTSEngine {
    MemoryBudget::Ticket memory_;
    const SherpaOnnxOfflineTts* tts_{};
public:
    explicit TTSEngine(const ModelBundle& model);
    ~TTSEngine();
    TTSEngine(const TTSEngine&) = delete;
    TTSEngine& operator=(const TTSEngine&) = delete;
    VoiceAudio synthesize(const std::string& text, std::stop_token stop = {});
};
}
