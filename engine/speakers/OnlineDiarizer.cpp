#include "OnlineDiarizer.hpp"
#include <bit>
#include <mach/mach_time.h>
namespace lvt {
std::vector<SpeechSegment> OnlineDiarizer::process(SpeechSegment segment, std::stop_token stop,
    const std::function<bool(std::span<const float>, std::stop_token, bool)>& isPlayback) {
    const auto begin = std::chrono::steady_clock::now();
    metrics_.embeddingMs = 0; metrics_.overlapMs = 0; metrics_.separationMs = 0; metrics_.overlapPercent = 0;
    struct Timer { double& value; std::chrono::steady_clock::time_point start; ~Timer() { value = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); } } timer{metrics_.latencyMs, begin};
    if (segment.sampleRate != 16000) throw std::runtime_error("Diarizer expects 16 kHz mono");
    if (isPlayback && isPlayback(segment.samples, stop, false)) { ++metrics_.playbackUnitsSuppressed; return {}; }
    auto regions = overlap_.analyze(segment.samples, stop);
    metrics_.overlapMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    if (stop.stop_requested()) return {};
    std::size_t overlapping = 0, speech = 0;
    for (const auto& region : regions) {
        if (region.localMask) speech += region.end - region.start;
        if (std::popcount(region.localMask) > 1) overlapping += region.end - region.start;
    }
    segment.overlap = overlapping >= 1600;
    metrics_.overlapPercent = segment.samples.empty() ? 0 : 100.0 * overlapping / segment.samples.size();
    if (segment.overlap) {
        ++metrics_.overlapWindows;
        // Right-padded previews can turn a single voice into transient overlap.
        // Wait for the final audio before separating or learning source identities.
        if (!segment.final) return {std::move(segment)};
        try {
            if (!separation_) separation_ = std::make_unique<SeparationEngine>(separationBundle_);
            lastSeparation_ = std::chrono::steady_clock::now();
            ++metrics_.separationCalls;
            auto separated = separation_->separate(segment.samples, stop);
            metrics_.separationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lastSeparation_).count();
            if (stop.stop_requested()) return {};
            if (!separated.reliable) return {std::move(segment)};
            std::array<bool, 2> playback{};
            for (std::size_t i = 0; i < 2; ++i) if (isPlayback && isPlayback(separated.sources[i], stop, true)) {
                playback[i] = true; ++metrics_.playbackUnitsSuppressed;
            }
            if (playback[0] || playback[1]) {
                std::vector<SpeechSegment> external;
                for (std::size_t i = 0; i < 2; ++i) if (!playback[i]) {
                    auto embedding = embeddings_.extract(separated.sources[i], stop);
                    const auto speaker = tracker_.identify(embedding, segment.firstFrame, !embedding.empty(), true);
                    external.push_back({std::move(separated.sources[i]), 16000, segment.firstFrame, segment.hostTime, speaker, false});
                }
                return external;
            }
            const auto embeddingStart = std::chrono::steady_clock::now();
            std::array<std::vector<float>, 2> embeddings{embeddings_.extract(separated.sources[0], stop), embeddings_.extract(separated.sources[1], stop)};
            metrics_.embeddingMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - embeddingStart).count();
            if (embeddings[0].empty() || embeddings[1].empty() || SpeakerTracker::similarity(embeddings[0], embeddings[1]) >= .6f) return {std::move(segment)};
            const auto a = tracker_.identify(embeddings[0], segment.firstFrame, true, true);
            const auto b = tracker_.identify(embeddings[1], segment.firstFrame, true, true, a);
            tracker_.observeSimultaneous(a, b);
            SpeechSegment first{std::move(separated.sources[0]), 16000, segment.firstFrame, segment.hostTime, a, false};
            SpeechSegment second{std::move(separated.sources[1]), 16000, segment.firstFrame, segment.hostTime, b, false};
            return {std::move(first), std::move(second)};
        } catch (...) { if (stop.stop_requested()) return {}; return {std::move(segment)}; }
    }
    // Split consecutive local speaker regions only after enough evidence; speaker
    // indices inside each Pyannote window are never used as persistent identities.
    std::vector<std::size_t> cuts{0}; unsigned previous = 0; std::size_t previousWindow = 0;
    for (const auto& region : regions) if (region.localMask && std::popcount(region.localMask) == 1) {
        // Local label permutations at a sliding-window boundary are not speaker changes.
        if (region.start / 80000 != previousWindow) { previous = 0; previousWindow = region.start / 80000; }
        if (previous && previous != region.localMask && region.start - cuts.back() >= 32000 && segment.samples.size() - region.start >= 32000) {
            // Local segmentation labels alone can flip within one person's speech.
            // Confirm an actual identity difference before cutting a clean utterance.
            const auto boundary = region.start;
            auto before = embeddings_.extract(std::span<const float>(segment.samples).subspan(boundary - 32000, 32000), stop);
            auto after = embeddings_.extract(std::span<const float>(segment.samples).subspan(boundary, 32000), stop);
            if (!before.empty() && !after.empty() && SpeakerTracker::similarity(before, after) < .45f) cuts.push_back(boundary);
        }
        previous = region.localMask;
    }
    cuts.push_back(segment.samples.size());
    std::vector<SpeechSegment> output;
    mach_timebase_info_data_t tb{}; mach_timebase_info(&tb);
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        const auto start = cuts[i - 1], end = cuts[i];
        SpeechSegment unit{{segment.samples.begin() + start, segment.samples.begin() + end}, 16000,
            segment.firstFrame + start, segment.hostTime ? segment.hostTime + std::uint64_t(start * 1e9 / 16000 * tb.denom / tb.numer) : 0};
        const auto embeddingStart = std::chrono::steady_clock::now();
        auto speechIn = [&](std::size_t from, std::size_t to) {
            std::size_t count=0;
            for (const auto& region : regions) if (std::popcount(region.localMask)==1 && region.confidence>=.5f) {
                const auto a=std::max(from,region.start), b=std::min(to,region.end);
                if (b>a) count+=b-a;
            }
            return count;
        };
        std::size_t evidenceStart=start, evidenceEnd=end;
        if (trimSpeakerEdges_) {
            evidenceStart=end; evidenceEnd=start;
            for (const auto& region : regions) if (std::popcount(region.localMask)==1 && region.confidence>=.5f) {
                const auto a=std::max(start,region.start), b=std::min(end,region.end);
                if (b>a) { evidenceStart=std::min(evidenceStart,a); evidenceEnd=std::max(evidenceEnd,b); }
            }
        }
        // Only speaker analysis excludes edge silence. ASR retains all captured
        // context. Enrollment still needs disjoint speech and the same thresholds.
        const auto evidence=evidenceEnd>evidenceStart ? std::span<const float>(segment.samples).subspan(evidenceStart,evidenceEnd-evidenceStart) : std::span<const float>{};
        auto embedding = embeddings_.extract(evidence, stop);
        metrics_.embeddingMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - embeddingStart).count();
        if (stop.stop_requested()) return {};
        const bool reliable = (trimSpeakerEdges_ ? speechIn(start,end) : speech) >= 12800 && evidence.size() >= 12800;
        const auto evidenceFirstFrame = segment.firstFrame + evidenceStart;
        const auto evidenceEndFrame = segment.firstFrame + evidenceEnd;
        unit.speaker = tracker_.identify(embedding, evidenceFirstFrame, reliable, false, 0, false, segment.final, evidenceEndFrame);
        if (!unit.speaker && reliable && segment.final && evidence.size() >= 38400) {
            // A single short/outlier embedding must not create a permanent person.
            // Corroborate identity using disjoint speech before admitting a new ID.
            const auto half = evidence.size() / 2;
            const bool disjointSpeech = !trimSpeakerEdges_ || (speechIn(evidenceStart,evidenceStart+half)>=12800 && speechIn(evidenceStart+half,evidenceEnd)>=12800);
            auto left = disjointSpeech ? embeddings_.extract(evidence.first(half), stop) : std::vector<float>{};
            auto right = disjointSpeech ? embeddings_.extract(evidence.subspan(half), stop) : std::vector<float>{};
            if (!left.empty() && !right.empty() && SpeakerTracker::similarity(left, right) >= .68f &&
                SpeakerTracker::similarity(embedding, left) >= .68f && SpeakerTracker::similarity(embedding, right) >= .68f) {
                // Do not create an alias when either half already belongs to a known person.
                auto a = tracker_.identify(left, unit.firstFrame, true, false, 0, false, false);
                auto b = tracker_.identify(right, unit.firstFrame, true, false, 0, false, false);
                if (a && a == b) unit.speaker = a;
                else if (!a && !b) unit.speaker = tracker_.identify(embedding, evidenceFirstFrame, true, false, 0, true, true, evidenceEndFrame, true);
            }
        }
        metrics_.embeddingMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - embeddingStart).count();
        output.push_back(std::move(unit));
    }
    return output;
}
}
