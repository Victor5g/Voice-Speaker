#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <span>
#include <stop_token>
#include <vector>

namespace lvt {
// Worker-side waveform evidence for residual echo after source separation.
// No voice identity or text matching: only PCM that was actually rendered.
class PlaybackReferenceHistory {
    mutable std::mutex mutex_;
    std::vector<float> ring_ = std::vector<float>(60000); // 30 seconds at 2 kHz
    std::size_t written_{};
    float sum_{};
    unsigned count_{};
    static double correlation(std::span<const float> a, std::span<const float> b) {
        double sa = 0, sb = 0, aa = 0, bb = 0, ab = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            sa += a[i]; sb += b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; ab += a[i] * b[i];
        }
        aa -= sa * sa / a.size(); bb -= sb * sb / a.size(); ab -= sa * sb / a.size();
        return aa > 1e-7 && bb > 1e-7 ? ab * ab / (aa * bb) : 0;
    }
public:
    void append8k(std::span<const float> input) {
        std::lock_guard lock(mutex_);
        for (const float sample : input) {
            sum_ += sample;
            if (++count_ == 4) { ring_[written_++ % ring_.size()] = sum_ / 4; count_ = 0; sum_ = 0; }
        }
    }
    void clear() {
        std::lock_guard lock(mutex_);
        std::fill(ring_.begin(), ring_.end(), 0); written_ = 0; sum_ = 0; count_ = 0;
    }
    bool matches16k(std::span<const float> input, std::stop_token stop = {}, bool separated = false) const {
        if (input.size() < 8000 || input.size() > 160000) return false;
        std::vector<float> source; source.reserve(input.size() / 8);
        for (std::size_t i = 0; i + 8 <= input.size(); i += 8) {
            float sum = 0; for (std::size_t j = 0; j < 8; ++j) sum += input[i + j];
            source.push_back(sum / 8);
        }
        constexpr std::size_t window = 1000;
        double energy = 0, bestEnergy = 0; std::size_t peak = 0;
        for (std::size_t i = 0; i < source.size(); ++i) {
            energy += source[i] * source[i];
            if (i >= window) energy -= source[i - window] * source[i - window];
            if (i + 1 >= window && energy > bestEnergy) { bestEnergy = energy; peak = i + 1 - window; }
        }
        if (bestEnergy < .001) return false;
        std::vector<float> reference;
        {
            std::lock_guard lock(mutex_);
            const auto size = std::min(written_, ring_.size()); reference.reserve(size);
            for (std::size_t i = written_ - size; i < written_; ++i) reference.push_back(ring_[i % ring_.size()]);
        }
        if (reference.size() < window) return false;
        if (std::none_of(reference.begin(), reference.end(), [](float x) { return std::abs(x) > .005f; })) return false;
        auto query = std::span<const float>(source).subspan(peak, window);
        for (std::size_t start = 0; start + window <= reference.size(); ++start) {
            if ((start & 255) == 0 && stop.stop_requested()) return false;
            if (correlation(query, std::span<const float>(reference).subspan(start, window)) < (separated ? .70 : .85)) continue;
            // Every material source region must match the SAME playback alignment.
            // A real person's different words elsewhere prevent whole-unit removal.
            bool consistent = true;
            double totalEnergy = 0, explainedEnergy = 0;
            for (std::size_t pos = 0; consistent && pos < source.size(); pos += window / 2) {
                const auto size = std::min(window / 2, source.size() - pos);
                double local = 0; for (std::size_t j = pos; j < pos + size; ++j) local += source[j] * source[j];
                if (local < bestEnergy * .025 || size < 100) continue;
                const auto aligned = std::int64_t(start) + std::int64_t(pos) - std::int64_t(peak);
                const auto explained = aligned >= 0 && std::size_t(aligned) + size <= reference.size()
                    ? correlation(std::span<const float>(source).subspan(pos, size), std::span<const float>(reference).subspan(std::size_t(aligned), size)) : 0;
                totalEnergy += local; explainedEnergy += local * explained;
                // Separator outputs contain attenuation holes and residual leakage.
                // Only for those outputs, admit a dominant aligned playback source;
                // its independent companion remains in the recognition pipeline.
                consistent = separated || explained >= .65;
            }
            if (consistent && totalEnergy > 0 && (!separated || explainedEnergy / totalEnergy >= .70)) return true;
        }
        return false;
    }
};
}
