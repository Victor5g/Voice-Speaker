#pragma once
#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>

namespace lvt {
struct SpeechEvidence {
    double peakBlockRMS{};
    bool silent{true};
    static SpeechEvidence analyze(std::span<const float> samples) {
        SpeechEvidence result;
        // Inspect short blocks so a legitimate brief word is not diluted by its
        // surrounding silence. DC-only capture is not speech.
        for (std::size_t first = 0; first < samples.size(); first += 320) {
            const auto block = samples.subspan(first, std::min<std::size_t>(320, samples.size() - first));
            double mean = 0;
            for (float sample : block) {
                if (!std::isfinite(sample)) throw std::runtime_error("Invalid recognition PCM");
                mean += sample / block.size();
            }
            double energy = 0;
            for (float sample : block) energy += (sample - mean) * (sample - mean);
            result.peakBlockRMS = std::max(result.peakBlockRMS, std::sqrt(energy / block.size()));
        }
        result.silent = result.peakBlockRMS <= 1e-7;
        return result;
    }
    bool rejectsWeakNoSpeech(double minimumNoSpeechProbability, double meanTextTokenLogProbability,
                             std::size_t textTokens, double noSpeechThreshold,
                             double textLogProbabilityThreshold) const {
        // Additional lexical-evidence guard. Reuse the backend's configured
        // thresholds, excluding timestamp/EOT confidence from the text mean.
        // This is a policy extension, not a calibrated semantic confidence:
        // require no-speech evidence in every segment AND weak lexical tokens.
        // Loudness, words, and minimum word count never determine rejection.
        return textTokens && std::isfinite(minimumNoSpeechProbability) &&
            std::isfinite(meanTextTokenLogProbability) && std::isfinite(noSpeechThreshold) &&
            std::isfinite(textLogProbabilityThreshold) && noSpeechThreshold >= 0 && noSpeechThreshold <= 1 &&
            textLogProbabilityThreshold <= 0 && minimumNoSpeechProbability > noSpeechThreshold &&
            minimumNoSpeechProbability <= 1 && meanTextTokenLogProbability < textLogProbabilityThreshold;
    }
};
}
