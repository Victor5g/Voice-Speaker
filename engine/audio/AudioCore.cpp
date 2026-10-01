#include "AudioCore.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mach/mach_time.h>
#include "StreamingResampler.hpp"

namespace lvt {
AudioCore::AudioCore() { mach_timebase_info_data_t tb{}; mach_timebase_info(&tb); hostTicksPerSecond_ = 1e9 * tb.denom / tb.numer; }
AudioCore::~AudioCore() { stop(); }
void AudioCore::start() {
    stop();
    conversation_.validateProcessingOptions(processingOptions_);
    stopping_ = false;
    vad_.start(!resourcesRoot_.empty(), streamingPolicy_, [this] { return asr_.estimatedDemandMs(); }, processingOptions_.endpointSilenceMs);
    asr_.start(modelPath_, modelHash_, [this](SpeechSegment& segment) { return vad_.popSegment(segment); }, resourcesRoot_,
        [this](const SpeechSegment& segment) { return vad_.obsolete(segment); },
        [this](std::span<const float> samples, std::stop_token stop, bool separated) { return playbackHistory_.matches16k(samples, stop, separated); }, streamingPolicy_.trimSpeakerEdges, asrBackend_, streamingPolicy_.minimumBoundaryProbability > 0, processingOptions_.sourceLanguage, processingOptions_.identifySpeakers);
    if (!resourcesRoot_.empty()) {
        static std::atomic<std::uint64_t> sessions{0};
        conversation_.start(resourcesRoot_, outputRate_, ++sessions, [this](ASRResult& result) { return asr_.pop(result); }, streamingPolicy_, processingOptions_);
    }
    captureWorker_ = std::jthread([this](std::stop_token stop) {
        mach_timebase_info_data_t timebase{};
        mach_timebase_info(&timebase);
        AudioBlock block;
        AudioBlock reference;
        StreamingResampler vadResampler;
        std::array<float, audioBlockSize> downsampled{};
        while (!stop.stop_requested()) {
            while (reference_.pop(reference)) {
                const auto n = referenceSampler_.process({reference.samples.data(), reference.count}, reference.sampleRate, downsampled);
                echoGuard_.appendReference({downsampled.data(), n});
                playbackHistory_.append8k({downsampled.data(), n});
                reference = {};
            }
            if (!input_.pop(block)) {
                // Idle worker wait, never used to simulate inference or on the audio thread.
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            double energy = 0;
            for (std::size_t i = 0; i < block.count; ++i) energy += block.samples[i] * block.samples[i];
            inputLevel_ = block.count ? std::sqrt(energy / block.count) : 0;
            const auto n = inputSampler_.process({block.samples.data(), block.count}, block.sampleRate, downsampled);
            if (echoGuard_.isSelfPlayback({downsampled.data(), n})) { ++selfPlaybackBlocks_; block.samples.fill(0); }
            else ++acceptedBlocks_;
            if (block.sampleRate == 8000 || block.sampleRate == 16000 || block.sampleRate == 32000 || block.sampleRate == 48000) vad_.push(block);
            else vadResampler.process(block, [this](const AudioBlock& converted) { vad_.push(converted); });
            if (block.hostTime) captureAgeMs_ = (mach_absolute_time() - block.hostTime) * double(timebase.numer) / timebase.denom / 1e6;
            block = {};
        }
    });
}
void AudioCore::stop() {
    requestStop();
    if (captureWorker_.joinable()) { captureWorker_.request_stop(); captureWorker_.join(); }
    vad_.stop();
    asr_.stop();
    conversation_.stop(); playback_.clearStopped();
    input_.clearStopped();
    reference_.clearStopped(); echoGuard_.clear(); inputSampler_.clear(); referenceSampler_.clear();
    playbackHistory_.clear();
    selfPlaybackBlocks_ = 0; acceptedBlocks_ = 0;
    inputLevel_ = 0; outputLevel_ = 0; captureAgeMs_ = 0;
    inputFrames_ = 0; outputFrames_ = 0; captureCallbacks_ = 0; playbackCallbacks_ = 0;
    toneRequested_ = false; toneRemaining_ = 0; toneTotal_ = 0; phase_ = 0;
}
void AudioCore::requestStop() { stopping_ = true; toneRequested_ = false; playback_.invalidate(); captureWorker_.request_stop(); asr_.requestStop(); conversation_.requestStop(); }
void AudioCore::capture(const float* samples, std::uint32_t count, double rate, std::uint64_t hostTime) noexcept {
    if (!samples || rate <= 0 || stopping_) return;
    ++captureCallbacks_; const auto firstFrame = inputFrames_.fetch_add(count);
    for (std::uint32_t offset = 0; offset < count;) {
        AudioBlock block;
        block.count = std::min<std::uint32_t>(audioBlockSize, count - offset);
        block.sampleRate = rate; block.hostTime = hostTime ? hostTime + std::uint64_t(offset * hostTicksPerSecond_ / rate) : 0;
        block.firstFrame = firstFrame + offset;
        std::copy_n(samples + offset, block.count, block.samples.begin());
        input_.push(block); // Drop newest if full; never block the audio callback.
        offset += block.count;
    }
}
void AudioCore::render(float* output, std::uint32_t count, double rate, std::uint64_t hostTime) noexcept {
    if (!output || rate <= 0) return;
    if (stopping_) { std::fill_n(output, count, 0); return; }
    ++playbackCallbacks_; outputFrames_ += count;
    playback_.render(output, count, rate, hostTime);
    if (toneRequested_.exchange(false)) { toneTotal_ = toneRemaining_ = std::uint64_t(rate * 0.35); phase_ = 0; }
    double energy = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        float sample = output[i];
        if (toneRemaining_) {
            const double elapsed = double(toneTotal_ - toneRemaining_) / rate;
            const double remaining = double(toneRemaining_) / rate;
            const double envelope = std::min({1.0, elapsed / 0.01, remaining / 0.01});
            sample = std::clamp(sample + float(std::sin(phase_) * 0.08 * envelope), -1.f, 1.f);
            phase_ += 2 * 3.14159265358979323846 * 440 / rate;
            if (phase_ > 2 * 3.14159265358979323846) phase_ -= 2 * 3.14159265358979323846;
            --toneRemaining_;
        }
        output[i] = sample; energy += sample * sample;
    }
    outputLevel_ = count ? std::sqrt(energy / count) : 0;
    for (std::uint32_t offset = 0; offset < count;) {
        AudioBlock block;
        block.count = std::min<std::uint32_t>(audioBlockSize, count - offset);
        block.sampleRate = rate;
        std::copy_n(output + offset, block.count, block.samples.begin());
        reference_.push(block);
        offset += block.count;
    }
}
AudioSnapshot AudioCore::snapshot() const noexcept {
    return {inputLevel_.load(), outputLevel_.load(), captureAgeMs_.load(), inputFrames_.load(), outputFrames_.load(), captureCallbacks_.load(), playbackCallbacks_.load(), input_.depth(), input_.dropped(), input_.highWater(), reference_.depth(), reference_.dropped(), selfPlaybackBlocks_.load(), acceptedBlocks_.load(), vad_.snapshot(), asr_.snapshot(), conversation_.snapshot(), playback_.depth(), playback_.dropped()};
}
}
