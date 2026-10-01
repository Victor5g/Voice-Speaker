#pragma once
#include <atomic>
#include <span>
#include <stop_token>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include "../common/PipelineTiming.hpp"
#include "ASRBackend.hpp"

struct whisper_context;
struct whisper_state;
namespace lvt {
struct ASRResult {
    std::string text, detectedLanguage;
    std::string targetLanguage, languageStatus;
    double ptProbability{}, enProbability{}, detectedProbability{}, latencyMs{}, durationMs{};
    std::uint64_t firstFrame{}, hostTime{};
    std::uint32_t speaker{};
    bool overlap{};
    std::uint64_t utterance{};
    std::uint32_t revision{}, lane{};
    bool final{true};
    PipelineTiming timing{};
    std::vector<float> boundaryProbability;
    bool languageForced{}, rejectedNoSpeech{}, rejectedDigitalSilence{}, speechConfidenceMeasured{};
    double minimumNoSpeechProbability{}, meanTextTokenLogProbability{}, peakBlockRMS{};
};
// Each instance has one mutable inference state and one inference owner. Forks
// share verified model weights but never sampling history, KV caches or results.
// The backend's logger is global.
class WhisperASR {
    struct Weights;
    std::shared_ptr<Weights> weights_;
    whisper_context *context_{};
    whisper_state *state_{};
    std::atomic<bool> metalActive_{false};
    ASRBackend backend_{ASRBackend::metal};
    bool reuseEncoder_{true};
    int threads_{4};
    std::uint64_t languageDetections_{}; // Same exclusive owner as recognize().
    explicit WhisperASR(std::shared_ptr<Weights> weights);
    void initializeState();
public:
    WhisperASR(const std::string& path, const std::string& sha256, const ASRConfiguration& configuration = {});
    ~WhisperASR();
    WhisperASR(const WhisperASR&) = delete;
    WhisperASR& operator=(const WhisperASR&) = delete;
    // Caller must reserve the additional state's memory before using this in
    // a budgeted pipeline. Existing app workers still share one serialized state.
    std::unique_ptr<WhisperASR> forkState() const;
    ASRResult recognize(std::span<const float> mono16k, std::stop_token stop = {},
        std::function<std::string(const std::string&)> context = {}, std::function<bool()> obsolete = {},
        bool collectBoundaryEvidence = false, std::string forcedLanguage = "");
    std::uint64_t languageDetections() const { return languageDetections_; }
    bool metalActive() const { return metalActive_.load(); }
    ASRBackend backend() const { return backend_; }
};
}
