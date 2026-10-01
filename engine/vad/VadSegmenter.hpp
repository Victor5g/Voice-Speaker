#pragma once
#include "../audio/AudioTypes.hpp"
#include <algorithm>
#include <optional>
#include <span>
#include <mach/mach_time.h>

namespace lvt {
class VadSegmenter {
    std::vector<float> preRoll_;
    SpeechSegment current_;
    bool active_{};
    std::size_t silenceFrames_{}, voicedFrames_{};
    double rate_{};
    bool streaming_{};
    unsigned endpointSilenceMs_{300};
public:
    explicit VadSegmenter(bool streaming = false,unsigned endpointSilenceMs=300) : streaming_(streaming),endpointSilenceMs_(endpointSilenceMs) {
        if(endpointSilenceMs<200||endpointSilenceMs>1000)throw std::invalid_argument("Pause must be 200..1000 ms");
    }
    std::optional<SpeechSegment> process(std::span<const float> frame, bool voiced, double rate,
                                         std::uint64_t firstFrame, std::uint64_t hostTime) {
        if (rate_ != rate) { clear(); rate_ = rate; }
        if (!active_) {
            if (!voiced) {
                preRoll_.insert(preRoll_.end(), frame.begin(), frame.end());
                // Keep more of the existing lead-in for streaming inference.
                // A 150 ms crop made a repeated PT utterance change meaning in
                // final decoding; 500 ms preserves onset context without waiting
                // for additional audio or extending the hard segment duration.
                const auto limit = std::size_t(rate * (streaming_ ? 0.5 : 0.15));
                if (preRoll_.size() > limit) preRoll_.erase(preRoll_.begin(), preRoll_.end() - limit);
                return std::nullopt;
            }
            active_ = true;
            current_ = {std::move(preRoll_), rate, firstFrame, hostTime};
            current_.firstFrame = firstFrame >= current_.samples.size() ? firstFrame - current_.samples.size() : 0;
            if (hostTime && !current_.samples.empty()) {
                mach_timebase_info_data_t tb{}; mach_timebase_info(&tb);
                const auto ticks = std::uint64_t(current_.samples.size() * 1e9 / rate * tb.denom / tb.numer);
                current_.hostTime = hostTime >= ticks ? hostTime - ticks : 0;
            }
            current_.samples.reserve(std::size_t(rate * (streaming_ ? 10.0 : 8.1)));
        }
        current_.samples.insert(current_.samples.end(), frame.begin(), frame.end());
        current_.timing.windowEndHostTime = hostclock::advance(hostTime, frame.size() / rate);
        current_.timing.windowEndFrame = std::uint64_t((firstFrame + frame.size()) * 16000.0 / rate);
        if (voiced) {
            voicedFrames_ += frame.size(); silenceFrames_ = 0;
            current_.timing.speechEndHostTime = current_.timing.windowEndHostTime;
            current_.timing.speechEndFrame = std::uint64_t((firstFrame + frame.size()) * 16000.0 / rate);
        }
        else silenceFrames_ += frame.size();
        // Honor the selected pause on long and short turns alike. The previous
        // implicit 120 ms cutoff after six seconds could override this setting.
        if (silenceFrames_ < std::size_t(rate * endpointSilenceMs_ / 1000.0) && current_.samples.size() < std::size_t(rate * (streaming_ ? 9.8 : 8))) return std::nullopt;
        const bool longEnough = voicedFrames_ >= std::size_t(rate * 0.1);
        current_.timing.endpointReason = silenceFrames_ >= std::size_t(rate * endpointSilenceMs_ / 1000.0)
            ? EndpointReason::silence : EndpointReason::windowLimit;
        current_.timing.endpointHostTime = hostclock::now();
        auto result = std::move(current_);
        active_ = false; silenceFrames_ = 0; voicedFrames_ = 0; current_ = {};
        if (longEnough) return result;
        return std::nullopt;
    }
    bool active() const noexcept { return active_; }
    const SpeechSegment& current() const { return current_; }
    void clear() { preRoll_ = {}; current_ = {}; active_ = false; silenceFrames_ = 0; voicedFrames_ = 0; rate_ = 0; }
};
}
