#pragma once
#include "../common/RingBuffer.hpp"
#include "../conversation/ConversationTypes.hpp"
#include "SincResampler.hpp"
#include "AudioOnset.hpp"
#include <mach/mach_time.h>

namespace lvt {
// Single worker producer; single realtime consumer. Slot memory is created and
// reclaimed only by the worker. The audio callback does not lock, allocate or free.
class PCMPlayback {
    enum State : int { free, ready, playing, retired };
    struct Slot { std::atomic<int> state{free}; SegmentIdentity id; std::vector<float> pcm; std::size_t onset{}; double rate{}; };
    std::array<Slot, 8> slots_;
    RingBuffer<std::uint32_t, 8> pending_;
    RingBuffer<PlaybackEvent, 32> events_;
    SincResampler resampler_;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> pendingMicroseconds_{0};
    int active_{-1};
    std::size_t position_{};
    double ticksPerSecond_{};
public:
    PCMPlayback() { mach_timebase_info_data_t tb{}; mach_timebase_info(&tb); ticksPerSecond_ = 1e9 * tb.denom / tb.numer; }
    void enable() { enabled_ = true; }
    void invalidate() { enabled_ = false; }
    bool enqueue(SynthesizedAudio& audio, double deviceRate, float gain = .8f) {
        if (!enabled_) return false;
        std::uint32_t index = 0;
        for (; index < slots_.size(); ++index) {
            auto state = slots_[index].state.load(std::memory_order_acquire);
            if (state == free || state == retired) break;
        }
        if (index == slots_.size()) { ++dropped_; return false; }
        auto pcm = resampler_.convert(audio.pcm, audio.sampleRate, deviceRate);
        if (pcm.empty()) return false;
        const double fade = deviceRate * .01;
        for (std::size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = std::clamp(pcm[i] * gain * float(std::min({1.0, i / fade, (pcm.size() - 1 - i) / fade})), -1.f, 1.f);
        auto& slot = slots_[index];
        slot.onset = audioOnsetFrame(pcm, deviceRate); slot.rate = deviceRate;
        const auto duration = std::uint64_t(pcm.size() * 1e6 / deviceRate);
        slot.pcm = std::move(pcm); slot.id = audio.translation.identity;
        slot.id.timing.playbackQueued = hostclock::now();
        pendingMicroseconds_.fetch_add(duration);
        slot.state.store(ready, std::memory_order_release);
        if (!enabled_ || !pending_.push(index)) { pendingMicroseconds_.fetch_sub(duration); slot.state.store(retired, std::memory_order_release); ++dropped_; return false; }
        return true;
    }
    void render(float* output, std::uint32_t count, double rate, std::uint64_t hostTime = 0) noexcept {
        std::fill_n(output, count, 0);
        if (!enabled_.load(std::memory_order_acquire)) return;
        if (!hostTime) hostTime = mach_absolute_time();
        std::uint64_t consumedMicroseconds = 0;
        std::uint64_t consumedStart = active_ >= 0 ? std::uint64_t(position_ * 1e6 / slots_[active_].rate) : 0;
        // Hardware timestamp identifies the beginning of this buffer.
        for (std::uint32_t i = 0; i < count; ++i) {
            if (active_ < 0) {
                std::uint32_t index;
                if (!pending_.pop(index)) break;
                active_ = int(index); position_ = 0;
                consumedStart = 0;
                auto& slot = slots_[index];
                slot.state.store(playing, std::memory_order_release);
                events_.push({slot.id, hostTime + std::uint64_t(i * ticksPerSecond_ / rate), true});
            }
            auto& slot = slots_[active_];
            if (position_ == slot.onset) events_.push({slot.id, hostTime + std::uint64_t(i * ticksPerSecond_ / rate), false, true});
            // Integer differences telescope to the exact reserved duration,
            // avoiding underflow/drift across callbacks and sample rates.
            output[i] = slot.pcm[position_++];
            if (position_ == slot.pcm.size() || i + 1 == count)
                consumedMicroseconds += std::uint64_t(position_ * 1e6 / slot.rate) - consumedStart;
            if (position_ == slot.pcm.size()) {
                events_.push({slot.id, hostTime + std::uint64_t((i + 1) * ticksPerSecond_ / rate), false});
                slot.state.store(retired, std::memory_order_release); active_ = -1;
            }
        }
        pendingMicroseconds_.fetch_sub(consumedMicroseconds);
    }
    bool popEvent(PlaybackEvent& event) { return events_.pop(event); }
    std::size_t depth() const { return pending_.depth(); }
    std::uint64_t dropped() const { return dropped_; }
    double pendingSeconds() const { return enabled_ ? pendingMicroseconds_.load() / 1e6 : 0; }
    void releaseRetired() {
        for (auto& slot : slots_) if (slot.state.load(std::memory_order_acquire) == retired) {
            std::fill(slot.pcm.begin(), slot.pcm.end(), 0); slot.pcm.clear(); slot.pcm.shrink_to_fit(); slot.state.store(free, std::memory_order_release);
        }
    }
    void clearStopped() {
        enabled_ = false; pending_.clearStopped(); events_.clearStopped(); active_ = -1; position_ = 0; dropped_ = 0;
        pendingMicroseconds_ = 0;
        for (auto& slot : slots_) { std::fill(slot.pcm.begin(), slot.pcm.end(), 0); slot.pcm.clear(); slot.pcm.shrink_to_fit(); slot.id = {}; slot.state = free; }
    }
};
}
