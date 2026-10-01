#pragma once
#include "../common/RingBuffer.hpp"
#include "SelfPlaybackGuard.hpp"
#include "PlaybackReferenceHistory.hpp"
#include "AudioTypes.hpp"
#include "../vad/VadWorker.hpp"
#include "../asr/ASRWorker.hpp"
#include "../conversation/ConversationPipeline.hpp"
#include "../common/ProcessingOptions.hpp"
#include <atomic>
#include <cstdint>
#include <thread>

namespace lvt {
struct AudioSnapshot {
    double inputLevel{}, outputLevel{}, captureAgeMs{};
    std::uint64_t inputFrames{}, outputFrames{}, captureCallbacks{}, playbackCallbacks{};
    std::size_t depth{}, dropped{}, highWater{};
    std::size_t referenceDepth{}, referenceDropped{};
    std::uint64_t selfPlaybackBlocks{}, acceptedBlocks{};
    VadMetrics vad;
    ASRMetrics asr;
    ConversationMetrics conversation;
    std::size_t playbackDepth{};
    std::uint64_t playbackDropped{};
};
class AudioCore {
    RingBuffer<AudioBlock, 64> input_;
    RingBuffer<AudioBlock, 64> reference_;
    SelfPlaybackGuard echoGuard_;
    PlaybackReferenceHistory playbackHistory_;
    EchoReferenceSampler inputSampler_, referenceSampler_;
    VadWorker vad_;
    ASRWorker asr_;
    PCMPlayback playback_;
    ConversationPipeline conversation_{playback_};
    std::string resourcesRoot_;
    double outputRate_{48000};
    double hostTicksPerSecond_{};
    std::string modelPath_, modelHash_;
    std::string asrBackend_;
    StreamingPolicy streamingPolicy_{StreamingPolicy::production()};
    ProcessingOptions processingOptions_;
    std::atomic<std::uint64_t> selfPlaybackBlocks_{0}, acceptedBlocks_{0};
    std::jthread captureWorker_;
    std::atomic<double> inputLevel_{0}, outputLevel_{0}, captureAgeMs_{0};
    std::atomic<std::uint64_t> inputFrames_{0}, outputFrames_{0}, captureCallbacks_{0}, playbackCallbacks_{0};
    std::atomic<bool> toneRequested_{false};
    std::atomic<bool> stopping_{false};
    double phase_{0};
    std::uint64_t toneRemaining_{0}, toneTotal_{0};
public:
    AudioCore();
    ~AudioCore();
    void start();
    void stop();
    void requestStop();
    void capture(const float* samples, std::uint32_t count, double rate, std::uint64_t hostTime) noexcept;
    void render(float* output, std::uint32_t count, double rate, std::uint64_t hostTime = 0) noexcept;
    void requestTestTone() noexcept { toneRequested_ = true; }
    AudioSnapshot snapshot() const noexcept;
    bool popSpeechSegment(SpeechSegment& segment) { return vad_.popSegment(segment); }
    bool popTranscript(ASRResult& result) { return asr_.pop(result); }
    void configureASR(std::string path, std::string hash) { modelPath_ = std::move(path); modelHash_ = std::move(hash); }
    void configureASRBackend(std::string backend) { asrBackend_ = std::move(backend); }
    void configureConversation(std::string resources, double outputRate) { resourcesRoot_ = std::move(resources); outputRate_ = outputRate; }
    bool popConversationEvent(ConversationEvent& event) { return conversation_.pop(event); }
    void setPlaybackDeviceLatency(double milliseconds) { conversation_.setDeviceLatency(milliseconds); }
    void setSourceLanguage(std::string language) { conversation_.setSourceLanguage(std::move(language)); }
    void configureStreaming(StreamingPolicy policy) { policy.validate(); streamingPolicy_ = policy; }
    void configureProcessing(ProcessingOptions options) { options.validate(); processingOptions_=std::move(options); }
};
}
