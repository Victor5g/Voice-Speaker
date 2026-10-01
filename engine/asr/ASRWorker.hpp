#pragma once
#include "WhisperASR.hpp"
#include "../audio/AudioTypes.hpp"
#include "../common/BoundedQueue.hpp"
#include "../speakers/OnlineDiarizer.hpp"
#include <functional>
#include <thread>
#include "../common/LatencyStatistics.hpp"

namespace lvt {
struct ASRMetrics { int state{}; bool metal{}; std::uint64_t completed{}; double lastLatencyMs{}, rtf{}; std::string error; QueueMetrics resultsQueue; std::uint64_t processed{}; std::size_t speakers{}; DiarizationMetrics diarization; LatencySummary pendingWait{}, inferenceWait{}, mel{}, language{}, decode{}; double estimatedDemandMs{}; std::uint64_t dequeuedJobs{}, obsoleteJobs{}, emptyUnits{}; int backend{}; std::uint64_t languageDetections{}, rejectedNoSpeech{}, rejectedDigitalSilence{}; };
class ASRWorker {
    std::jthread worker_;
    BoundedQueue<ASRResult, 32> results_;
    std::atomic<int> state_{0};
    std::atomic<int> backend_{0};
    std::atomic<bool> metal_{false};
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint64_t> processed_{0};
    std::atomic<std::size_t> speakers_{0};
    std::atomic<double> latency_{0}, rtf_{0};
    std::atomic<double> estimatedDemandMs_{600};
    std::atomic<std::uint64_t> dequeuedJobs_{0}, obsoleteJobs_{0}, emptyUnits_{0};
    std::atomic<std::uint64_t> languageDetections_{0}, rejectedNoSpeech_{0}, rejectedDigitalSilence_{0};
    mutable std::mutex errorMutex_;
    std::string error_;
    DiarizationMetrics diarization_;
    LatencyStatistics pendingWait_, inferenceWait_, mel_, language_, decode_;
public:
    ~ASRWorker() { stop(); }
    void start(std::string path, std::string hash, std::function<bool(SpeechSegment&)> input, std::string resources = "",
        std::function<bool(const SpeechSegment&)> obsolete = {},
        std::function<bool(std::span<const float>, std::stop_token, bool)> isPlayback = {}, bool trimSpeakerEdges = false,
        std::string backend = "", bool collectBoundaryEvidence = false,
        std::string sourceLanguage = "", bool identifySpeakers = true);
    void stop();
    void requestStop() { worker_.request_stop(); }
    bool pop(ASRResult& result) { return results_.tryPop(result); }
    ASRMetrics snapshot() const;
    double estimatedDemandMs() const { return estimatedDemandMs_.load(); }
};
}
