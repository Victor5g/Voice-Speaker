#pragma once
#include "ConversationTypes.hpp"
#include "SpeakerScheduler.hpp"
#include "../asr/WhisperASR.hpp"
#include "../audio/PCMPlayback.hpp"
#include "../common/BoundedQueue.hpp"
#include "../common/LatencyStatistics.hpp"
#include "../common/StreamingPolicy.hpp"
#include "../common/ProcessingOptions.hpp"
#include <functional>
#include <thread>

namespace lvt {
struct ConversationMetrics {
    int translationState{}, ttsState{};
    std::uint64_t translated{}, synthesized{}, eventDrops{};
    double translationMs{}, ttsMs{};
    QueueMetrics translationQueue, ttsQueue;
    std::string error;
    LatencySummary firstAudio, endToEnd;
    std::string loadedVoice;
    double ttsAudioDurationMs{}, ttsVoiceLoadMs{};
    LatencySummary afterVadEnd{}, endpoint{}, translationWait{}, ttsWait{}, playbackWait{};
    double playbackSeconds{};
    std::string cachedVoices;
    double ttsWarmupMs{};
    std::uint64_t voiceCacheHits{}, voiceCacheMisses{};
    std::uint64_t revisionConflicts{}, recoveredContinuations{};
    // Completion of ASR source units is distinct from translated/spoken units.
    // Read together with pendingEvents to drain the coordinator without mistaking
    // an empty input queue for completion of a result already popped by it.
    std::uint64_t coordinatedUtterances{};
    std::size_t pendingEvents{};
    std::uint64_t audioSkipped{};
    LatencySummary chunkLag;
};
class ConversationPipeline {
    PCMPlayback& playback_;
    BoundedQueue<TranslationResult, 8> translationQueue_;
    SpeakerScheduler ttsQueue_;
    BoundedQueue<ConversationEvent, 256> events_;
    std::jthread coordinator_, translationWorker_, ttsWorker_;
    std::atomic<std::uint64_t> session_{0}, translated_{0}, synthesized_{0};
    std::atomic<int> translationState_{0}, ttsState_{0};
    std::atomic<double> translationMs_{0}, ttsMs_{0};
    std::atomic<double> ttsAudioDurationMs_{0}, ttsVoiceLoadMs_{0};
    std::atomic<double> ttsWarmupMs_{0};
    std::atomic<std::uint64_t> voiceCacheHits_{0}, voiceCacheMisses_{0};
    std::atomic<std::uint64_t> revisionConflicts_{0}, recoveredContinuations_{0};
    std::atomic<std::uint64_t> coordinatedUtterances_{0};
    std::atomic<std::uint64_t> audioSkipped_{0};
    std::atomic<double> deviceLatencyMs_{0};
    LatencyStatistics firstAudio_, endToEnd_;
    LatencyStatistics chunkLag_;
    LatencyStatistics afterVadEnd_, endpoint_, translationWait_, ttsWait_, playbackWait_;
    mutable std::mutex errorMutex_;
    std::string error_;
    std::string loadedVoice_;
    std::string cachedVoices_;
    std::string sourceLanguage_;
    void emit(TranslationResult result, std::string state, std::string error = "");
    void fail(const std::string& error);
public:
    explicit ConversationPipeline(PCMPlayback& playback) : playback_(playback) {}
    ~ConversationPipeline() { stop(); }
    void start(std::string root, double outputRate, std::uint64_t session, std::function<bool(ASRResult&)> input, StreamingPolicy policy = {}, ProcessingOptions options = {});
    void requestStop();
    void stop();
    bool pop(ConversationEvent& event) {
        while (events_.tryPop(event)) {
            const auto active = session_.load(std::memory_order_acquire);
            if (active && event.result.identity.session == active) return true;
        }
        event = {};
        return false;
    }
    ConversationMetrics snapshot() const;
    void setDeviceLatency(double milliseconds) { deviceLatencyMs_ = std::max(0.0, milliseconds); }
    // Configure only while stopped. Empty retains in-person automatic direction.
    void setSourceLanguage(std::string language) { sourceLanguage_ = std::move(language); }
    void validateProcessingOptions(const ProcessingOptions& options) const { options.validateForSourceLanguage(sourceLanguage_); }
};
}
