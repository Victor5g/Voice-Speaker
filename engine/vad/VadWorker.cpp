#include "VadWorker.hpp"
#include "VadSegmenter.hpp"
#include "webrtc/common_audio/vad/include/webrtc_vad.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <mach/mach_time.h>

namespace lvt {
void VadWorker::start(bool streaming, StreamingPolicy policy, std::function<double()> demand, unsigned endpointSilenceMs) {
    policy.validate();
    if(endpointSilenceMs<200||endpointSilenceMs>1000)throw std::invalid_argument("Invalid VAD pause");
    stop();
    partialIntervalMs_ = policy.partialIntervalMs;
    worker_ = std::jthread([this, streaming, policy, endpointSilenceMs, demand = std::move(demand)](std::stop_token stop) {
        VadInst *raw = nullptr;
        if (WebRtcVad_Create(&raw) != 0) { ++errors_; return; }
        std::unique_ptr<VadInst, decltype(&WebRtcVad_Free)> vad(raw, WebRtcVad_Free);
        WebRtcVad_Init(vad.get()); WebRtcVad_set_mode(vad.get(), 2);
        VadSegmenter segmenter(streaming,endpointSilenceMs);
        std::array<std::int16_t, 960> pcm{};
        std::array<float, 960> floats{};
        std::size_t pending = 0;
        std::uint64_t expected = 0, frameStart = 0, frameTime = 0;
        double lastRate = 0;
        AudioBlock block;
        std::uint64_t utterance = 0;
        std::uint32_t revision = 0;
        std::size_t lastPartial = 0;
        mach_timebase_info_data_t tb{}; mach_timebase_info(&tb);
        while (!stop.stop_requested()) {
            if (!input_.pop(block)) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
            const int rate = int(block.sampleRate);
            if ((rate != 8000 && rate != 16000 && rate != 32000 && rate != 48000) || rate != block.sampleRate) {
                ++errors_; segmenter.clear(); pending = 0; continue;
            }
            if (lastRate != rate || expected != block.firstFrame) {
                segmenter.clear(); pending = 0; WebRtcVad_Init(vad.get()); WebRtcVad_set_mode(vad.get(), 2);
            }
            lastRate = rate; expected = block.firstFrame + block.count;
            const std::size_t frameSize = rate / 50; // 20 ms frames, consolidated across taps.
            for (std::size_t i = 0; i < block.count; ++i) {
                if (pending == 0) { frameStart = block.firstFrame + i; frameTime = block.hostTime ? block.hostTime + std::uint64_t(i * 1e9 / rate * tb.denom / tb.numer) : 0; }
                float sample = std::isfinite(block.samples[i]) ? std::clamp(block.samples[i], -1.0f, 1.0f) : 0;
                floats[pending] = sample; pcm[pending++] = std::int16_t(sample * 32767);
                if (pending != frameSize) continue;
                const auto begin = std::chrono::steady_clock::now();
                const int decision = WebRtcVad_Process(vad.get(), rate, pcm.data(), int(frameSize));
                latencyMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                ++frames_;
                if (decision < 0) { ++errors_; segmenter.clear(); }
                else {
                    partialIntervalMs_ = policy.interval(demand ? demand() : 0);
                    if (decision) ++voicedFrames_;
                    if (!segmenter.active() && decision == 1) { ++utterance; revision = 0; lastPartial = 0; latestUtterance_ = utterance; }
                    if (auto segment = segmenter.process({floats.data(), frameSize}, decision == 1, rate, frameStart, frameTime)) {
                        segment->utterance = utterance; segment->revision = ++revision; segment->final = true;
                        segment->timing.vadPublished = hostclock::now();
                        completedUtterance_ = utterance;
                        ++segmentCount_; segments_.push(std::move(*segment));
                    } else if (streaming && segmenter.active() && segmenter.current().samples.size() - lastPartial >= std::size_t(rate * partialIntervalMs_.load() / 1000.0)) {
                        auto partial = segmenter.current(); lastPartial = partial.samples.size();
                        partial.utterance = utterance; partial.revision = ++revision; partial.final = false;
                        partial.timing.vadPublished = hostclock::now();
                        latestRevision_ = revision;
                        ++partialPublished_;
                        std::lock_guard lock(partialMutex_); if (partial_) ++partialReplaced_; partial_ = std::move(partial);
                    }
                    speaking_ = segmenter.active();
                }
                pending = 0;
            }
            block = {};
        }
        segmenter.clear(); pcm.fill(0); floats.fill(0);
    });
}
void VadWorker::stop() {
    if (worker_.joinable()) { worker_.request_stop(); worker_.join(); }
    input_.clearStopped(); segments_.clear(); speaking_ = false;
    { std::lock_guard lock(partialMutex_); partial_.reset(); }
    completedUtterance_ = 0; latestUtterance_ = 0; latestRevision_ = 0;
    partialPublished_ = 0; partialReplaced_ = 0; obsoletePending_ = 0;
    frames_ = 0; voicedFrames_ = 0; segmentCount_ = 0; errors_ = 0; latencyMs_ = 0;
}
VadMetrics VadWorker::snapshot() const {
    return {speaking_.load(), frames_.load(), voicedFrames_.load(), segmentCount_.load(), errors_.load(), latencyMs_.load(), input_.depth(), input_.dropped(), segments_.metrics(), partialIntervalMs_.load(), partialPublished_.load(), partialReplaced_.load(), obsoletePending_.load()};
}
}
