#pragma once
#include "../audio/AudioTypes.hpp"
#include "../common/RingBuffer.hpp"
#include "../common/BoundedQueue.hpp"
#include <atomic>
#include <thread>
#include <optional>
#include <functional>
#include "../common/StreamingPolicy.hpp"

namespace lvt {
struct VadMetrics {
    bool speaking{};
    std::uint64_t frames{}, voicedFrames{}, segments{}, errors{};
    double lastLatencyMs{};
    std::size_t depth{}, dropped{};
    QueueMetrics segmentsQueue;
    unsigned partialIntervalMs{};
    std::uint64_t partialPublished{}, partialReplaced{}, obsoletePending{};
};
class VadWorker {
    RingBuffer<AudioBlock, 64> input_;
    BoundedQueue<SpeechSegment, 8> segments_;
    std::jthread worker_;
    std::atomic<bool> speaking_{false};
    std::atomic<std::uint64_t> frames_{0}, voicedFrames_{0}, segmentCount_{0}, errors_{0};
    std::atomic<double> latencyMs_{0};
    mutable std::mutex partialMutex_;
    std::optional<SpeechSegment> partial_;
    std::atomic<std::uint64_t> completedUtterance_{0}, latestUtterance_{0};
    std::atomic<std::uint32_t> latestRevision_{0};
    std::atomic<unsigned> partialIntervalMs_{1500};
    std::atomic<std::uint64_t> partialPublished_{0}, partialReplaced_{0}, obsoletePending_{0};
public:
    ~VadWorker() { stop(); }
    void start(bool streaming = false, StreamingPolicy policy = {}, std::function<double()> demand = {}, unsigned endpointSilenceMs=300);
    void stop();
    bool push(const AudioBlock& block) { return input_.push(block); }
    bool popSegment(SpeechSegment& segment) {
        if (segments_.tryPop(segment)) return true;
        std::lock_guard lock(partialMutex_);
        if (!partial_) return false;
        segment = std::move(*partial_); partial_.reset();
        if (obsolete(segment)) { ++obsoletePending_; return false; }
        return true;
    }
    bool obsolete(const SpeechSegment& segment) const {
        return !segment.final && (completedUtterance_ >= segment.utterance ||
            latestUtterance_ > segment.utterance || (latestUtterance_ == segment.utterance && latestRevision_ > segment.revision));
    }
    VadMetrics snapshot() const;
};
}
