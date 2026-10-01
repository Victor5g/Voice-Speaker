#pragma once
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include "../common/StreamingPolicy.hpp"
namespace lvt {
struct StableUpdate { std::string confirmed, remaining; bool conflict{}, recovered{}; };
class SegmentStabilityManager {
    std::vector<std::string> previous_, committed_;
    std::vector<std::string> continuationAnchor_;
    std::vector<unsigned> agreement_;
    std::vector<std::uint64_t> firstSeenFrame_;
    std::uint64_t lastAudioFrame_{};
    StreamingPolicy policy_;
    static std::vector<std::string> words(const std::string& text) {
        std::istringstream stream(text); std::vector<std::string> result; std::string word;
        while (stream >> word) result.push_back(std::move(word)); return result;
    }
    static std::string key(std::string word) {
        for (auto& c : word) c = char(std::tolower(static_cast<unsigned char>(c)));
        while (!word.empty() && (word.back() == '.' || word.back() == ',' || word.back() == '?' || word.back() == '!' || word.back() == ';' || word.back() == ':')) word.pop_back();
        return word;
    }
    static std::string join(const std::vector<std::string>& words, std::size_t first, std::size_t last) {
        std::string text; for (auto i = first; i < last; ++i) { if (!text.empty()) text += ' '; text += words[i]; } return text;
    }
public:
    explicit SegmentStabilityManager(StreamingPolicy policy = {}) : policy_(policy) { policy_.validate(); }
    StableUpdate observe(const std::string& hypothesis, bool final, std::uint64_t audioEndFrame = 0, bool mayCommit = true,
                         const std::vector<float>& boundaryProbability = {}) {
        auto current = words(hypothesis);
        if (current.size() > 512) return {"", hypothesis, true};
        // A repeated decode of the same audio is not independent evidence. Final
        // silence may still confirm the remainder, even without fresh samples.
        if (!final && audioEndFrame && lastAudioFrame_ && audioEndFrame <= lastAudioFrame_)
            return {"", join(current, std::min(committed_.size(), current.size()), current.size()), false};
        lastAudioFrame_ = std::max(lastAudioFrame_, audioEndFrame);
        // A provisional hypothesis may temporarily regress. Hold it without
        // changing spoken text; only the final hypothesis establishes a conflict.
        auto regression = [&](std::string remaining) { previous_.clear(); agreement_.clear(); firstSeenFrame_.clear(); return StableUpdate{"", std::move(remaining), final}; };
        if (current.size() < committed_.size()) return regression("");
        std::size_t differences = 0;
        for (std::size_t i = 0; i < committed_.size(); ++i) differences += key(current[i]) != key(committed_[i]);
        if (differences) {
            // A final decode can revise one word inside an already spoken clause.
            // Recover only an unchanged clause boundary and an anchored suffix;
            // insertions, deletions and broader rewrites remain explicit conflicts.
            const auto boundary = committed_.size();
            const auto clauseEnd = [](const std::string& word) {
                return !word.empty() && (word.back() == '.' || word.back() == '!' || word.back() == '?' || word.back() == ';');
            };
            bool anchored = final && differences == 1 && boundary >= 4 && current.size() >= boundary + 2 &&
                continuationAnchor_.size() == 2 && clauseEnd(current[boundary - 1]);
            for (std::size_t i = 0; anchored && i < boundary; ++i)
                anchored = clauseEnd(current[i]) == clauseEnd(committed_[i]);
            for (std::size_t i = boundary; anchored && i < boundary + 2; ++i)
                anchored = key(current[i]) == key(continuationAnchor_[i - boundary]);
            if (anchored) {
                auto suffix = join(current, boundary, current.size());
                committed_.insert(committed_.end(), current.begin() + boundary, current.end());
                continuationAnchor_.clear();
                previous_ = std::move(current); agreement_.assign(previous_.size(), 0);
                firstSeenFrame_.assign(previous_.size(), 0);
                return {std::move(suffix), "", true, true};
            }
            return regression(join(current, boundary, current.size()));
        }
        std::size_t stable = 0;
        while (stable < current.size() && stable < previous_.size() && key(current[stable]) == key(previous_[stable])) ++stable;
        std::vector<unsigned> agreement(current.size(), 0);
        std::vector<std::uint64_t> firstSeenFrame(current.size(), 0);
        for (std::size_t i = 0; i + 3 <= current.size(); ++i) {
            const auto& word = current[i];
            const bool boundary = !word.empty() && (word.back() == '.' || word.back() == '!' || word.back() == '?' || word.back() == ';');
            // Repeating an uncertain punctuation token does not prove a clause
            // boundary. It must meet the threshold on consecutive observations.
            if (policy_.minimumBoundaryProbability > 0 && policy_.consecutiveBoundaryEvidence &&
                (!boundary || boundaryProbability.size() != current.size() || !std::isfinite(boundaryProbability[i]) ||
                 boundaryProbability[i] < policy_.minimumBoundaryProbability || boundaryProbability[i] > 1)) continue;
            agreement[i] = i < stable ? std::min(3u, agreement_[i] + 1) : 1;
            firstSeenFrame[i] = i < stable && agreement_[i] ? firstSeenFrame_[i] : audioEndFrame;
        }
        std::size_t limit = committed_.size();
        if (final) limit = current.size();
        else if (mayCommit) for (std::size_t i = committed_.size(); i < stable; ++i) {
            const auto& word = current[i];
            // Ellipses are incomplete clauses, not a promise to speak the fragment.
            // Whisper can add punctuation at the edge of an unfinished audio window.
            // Require two repeated following words before speaking a partial clause.
            const bool stableInTime = !policy_.stableAudioMs || (firstSeenFrame[i] && audioEndFrame >= firstSeenFrame[i] &&
                audioEndFrame - firstSeenFrame[i] >= std::uint64_t(policy_.stableAudioMs) * 16);
            const bool clauseEnd = !word.empty() && (word.back() == '!' || word.back() == '?' || word.back() == ';' || (word.back() == '.' && !word.ends_with("...")));
            // In the opt-in current-evidence experiment, a comma may become a
            // confident period without restarting lexical agreement. Questions,
            // ellipses and absent punctuation do not qualify for this exception.
            const bool repeatedDelimiter = i < previous_.size() && !previous_[i].empty() && !previous_[i].ends_with("...") && !word.empty() &&
                (previous_[i].back() == word.back() || (policy_.allowCommaToPeriodBoundary &&
                 previous_[i].back() == ',' && word.back() == '.'));
            const bool boundarySupported = policy_.minimumBoundaryProbability == 0 ||
                (boundaryProbability.size() == current.size() && std::isfinite(boundaryProbability[i]) &&
                 boundaryProbability[i] >= policy_.minimumBoundaryProbability && boundaryProbability[i] <= 1 &&
                 repeatedDelimiter);
            const bool ready = boundarySupported && agreement[i] >= policy_.agreements && stableInTime && i + 3 <= stable;
            // A later strong boundary must not carry an earlier weak boundary
            // into the same confirmed prefix.
            if (policy_.minimumBoundaryProbability > 0 && clauseEnd && !ready) break;
            if (ready && clauseEnd) limit = i + 1;
        }
        StableUpdate result{join(current, committed_.size(), limit), join(current, limit, current.size()), false};
        if (limit > committed_.size()) {
            continuationAnchor_.clear();
            if (limit + 2 <= current.size()) continuationAnchor_.assign(current.begin() + limit, current.begin() + limit + 2);
        }
        committed_.assign(current.begin(), current.begin() + limit); previous_ = std::move(current); agreement_ = std::move(agreement); firstSeenFrame_ = std::move(firstSeenFrame);
        return result;
    }
};
}
