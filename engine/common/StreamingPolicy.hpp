#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lvt {
// Immutable while running. Candidate settings are evaluated by the native WAV
// harness before changing the default used by the application.
struct StreamingPolicy {
    unsigned partialIntervalMs{1500};
    unsigned agreements{3};
    unsigned stableAudioMs{0};
    bool adaptive{false};
    bool trimSpeakerEdges{false};
    float minimumBoundaryProbability{0}; // Experimental until native quality/latency comparison passes.
    bool consecutiveBoundaryEvidence{true};
    bool allowCommaToPeriodBoundary{false}; // Development-only current-evidence experiment.
    static StreamingPolicy production() { return {750, 2, 600, true, true}; }
    void validate() const {
        if ((partialIntervalMs != 750 && partialIntervalMs != 1000 && partialIntervalMs != 1500) ||
            (agreements != 2 && agreements != 3) || stableAudioMs > 3000 ||
            (agreements == 2 && stableAudioMs < 600) || !std::isfinite(minimumBoundaryProbability) ||
            minimumBoundaryProbability < 0 || minimumBoundaryProbability > 1 ||
            (allowCommaToPeriodBoundary && (consecutiveBoundaryEvidence || minimumBoundaryProbability == 0)))
            throw std::invalid_argument("Unsupported streaming experiment");
    }
    unsigned interval(double estimatedDemandMs) const {
        if (!adaptive) return partialIntervalMs;
        // Demand includes all routes sharing Whisper and per-route diarization.
        // Leave 30% headroom. Under load, reduce speculative work instead of
        // increasing pending queues. Finals retain their existing priority.
        const double required = std::isfinite(estimatedDemandMs) ? estimatedDemandMs / .7 : 3000;
        for (unsigned interval : {750u, 1000u, 1500u, 2000u, 3000u})
            if (interval >= partialIntervalMs && interval >= required) return interval;
        return 3000;
    }
};
}
