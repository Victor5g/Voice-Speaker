#pragma once
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace lvt {
// Session-scoped, owned by the diarization worker. Never uses pitch, volume or gender.
class SpeakerTracker {
    struct Identity { std::vector<float> centroid; unsigned observations{1}; std::uint64_t lastFrame{}; std::bitset<64> simultaneous; std::uint64_t learnedUntil{}; };
    struct Candidate { std::vector<float> centroid; std::uint64_t endFrame{}; };
    std::vector<Identity> identities_;
    std::vector<Candidate> candidates_;
    float match_{.68f}, createBelow_{.45f}, margin_{.08f};
    static bool normalize(std::vector<float>& values) {
        double norm = 0; for (float v : values) { if (!std::isfinite(v)) return false; norm += v * v; }
        if (norm < 1e-12) return false;
        for (auto& v : values) v /= float(std::sqrt(norm)); return true;
    }
public:
    static float similarity(std::span<const float> a, std::span<const float> b) {
        if (a.size() != b.size() || a.empty()) return -1;
        double value = 0; for (std::size_t i = 0; i < a.size(); ++i) value += a[i] * b[i];
        return float(std::clamp(value, -1.0, 1.0));
    }
    std::uint32_t identify(std::span<const float> embedding, std::uint64_t frame, bool reliable, bool overlap = false,
                           std::uint32_t excluded = 0, bool allowCreate = true, bool allowUpdate = true,
                           std::uint64_t evidenceEndFrame = 0, bool initialEvidenceCorroborated = false) {
        if (embedding.empty() || !reliable) return 0;
        std::vector<float> normalized(embedding.begin(), embedding.end());
        if (!normalize(normalized)) return 0;
        if (!identities_.empty() && normalized.size() != identities_.front().centroid.size()) return 0;
        float best = -1, second = -1; std::uint32_t id = 0;
        for (std::size_t i = 0; i < identities_.size(); ++i) {
            if (i + 1 == excluded) continue;
            float score = similarity(normalized, identities_[i].centroid);
            if (score > best) { second = best; best = score; id = std::uint32_t(i + 1); }
            else second = std::max(second, score);
        }
        if (id && best >= match_ && best - second >= margin_) {
            auto& known = identities_[id - 1]; known.lastFrame = frame;
            if (allowUpdate && !overlap && best >= .8f && frame >= known.learnedUntil) {
                for (std::size_t i = 0; i < normalized.size(); ++i) known.centroid[i] = .9f * known.centroid[i] + .1f * normalized[i];
                normalize(known.centroid); ++known.observations;
                known.learnedUntil = evidenceEndFrame > frame ? evidenceEndFrame : frame + (frame != UINT64_MAX);
            }
            return id;
        }
        // Previews and separated/overlapping speech cannot enroll an identity.
        // allowUpdate is the caller's final-evidence flag, not merely permission
        // to move an established centroid. Previously previews could create IDs.
        if (!allowCreate || !allowUpdate || overlap || best >= createBelow_ || identities_.size() >= 64 || frame == UINT64_MAX) return 0;
        const auto evidenceEnd = evidenceEndFrame > frame ? evidenceEndFrame : frame + 1;
        // Bootstrap only the first person, when the caller has independently
        // checked two disjoint speech portions of this final. No existing person
        // can be aliased here. Subsequent identities still need separate finals.
        if (identities_.empty() && initialEvidenceCorroborated) {
            identities_.push_back({std::move(normalized), 2, frame, {}, evidenceEnd});
            candidates_.clear();
            return 1;
        }
        std::erase_if(candidates_, [&](const auto& candidate) {
            return frame > candidate.endFrame && frame - candidate.endFrame > 16000ULL * 60;
        });
        float candidateBest = -1, candidateSecond = -1;
        std::size_t selected = candidates_.size();
        for (std::size_t i = 0; i < candidates_.size(); ++i) {
            const auto score = similarity(normalized, candidates_[i].centroid);
            if (score > candidateBest) { candidateSecond = candidateBest; candidateBest = score; selected = i; }
            else candidateSecond = std::max(candidateSecond, score);
        }
        if (selected < candidates_.size() && candidateBest >= .8f) {
            auto& candidate = candidates_[selected];
            // Repeated hypotheses, duplicated finals and overlapping PCM are
            // one observation. They cannot satisfy independent confirmation.
            if (frame < candidate.endFrame || candidateBest - candidateSecond < margin_) return 0;
            for (std::size_t i = 0; i < normalized.size(); ++i) normalized[i] += candidate.centroid[i];
            normalize(normalized);
            identities_.push_back({std::move(normalized), 2, frame, {}, evidenceEnd});
            candidates_.erase(candidates_.begin() + selected);
            return std::uint32_t(identities_.size());
        }
        if (candidates_.size() < 64) candidates_.push_back({std::move(normalized), evidenceEnd});
        return 0;
    }
    void observeSimultaneous(std::uint32_t a, std::uint32_t b) {
        if (!a || !b || a == b || a > identities_.size() || b > identities_.size()) return;
        identities_[a - 1].simultaneous.set(b - 1); identities_[b - 1].simultaneous.set(a - 1);
    }
    // No automatic merge of established identities. New ambiguous observations
    // remain unknown; this preserves IDs and prevents overlap-induced merges.
    std::size_t count() const { return identities_.size(); }
    std::size_t provisionalCount() const { return candidates_.size(); }
    void clear() { identities_.clear(); candidates_.clear(); }
};
}
