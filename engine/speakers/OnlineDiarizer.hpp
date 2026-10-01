#pragma once
#include "SpeakerEmbeddingEngine.hpp"
#include "SpeakerTracker.hpp"
#include "OverlapDetector.hpp"
#include "../audio/AudioTypes.hpp"
#include "../separation/SeparationEngine.hpp"
#include <chrono>
#include <functional>
namespace lvt {
struct DiarizationMetrics {
    bool loaded{}, separationLoaded{};
    double latencyMs{}, embeddingMs{}, overlapMs{}, overlapPercent{}, separationMs{};
    std::uint64_t overlapWindows{}, separationCalls{}, playbackUnitsSuppressed{};
};
class OnlineDiarizer {
    SpeakerEmbeddingEngine embeddings_;
    OverlapDetector overlap_;
    SpeakerTracker tracker_;
    ModelBundle separationBundle_;
    std::unique_ptr<SeparationEngine> separation_;
    std::chrono::steady_clock::time_point lastSeparation_{};
    DiarizationMetrics metrics_;
    bool trimSpeakerEdges_{};
public:
    OnlineDiarizer(const ModelBundle& embedding, const ModelBundle& overlap, const ModelBundle& separation, bool trimSpeakerEdges = false)
        : embeddings_(embedding), overlap_(overlap), separationBundle_(separation), trimSpeakerEdges_(trimSpeakerEdges) {}
    std::vector<SpeechSegment> process(SpeechSegment mono16k, std::stop_token stop = {},
        const std::function<bool(std::span<const float>, std::stop_token, bool)>& isPlayback = {});
    void releaseIdleModels() {
        if (separation_ && (MemoryBudget::shared().pressure() || std::chrono::steady_clock::now() - lastSeparation_ > std::chrono::seconds(5))) separation_.reset();
    }
    std::size_t speakerCount() const { return tracker_.count(); }
    DiarizationMetrics metrics() const { auto result = metrics_; result.loaded = true; result.separationLoaded = bool(separation_); return result; }
};
}
