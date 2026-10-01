#pragma once
#include "LanguageRouter.hpp"
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace lvt {
struct LanguageEvidence { std::string_view detected; double probability{}, durationMs{}; };
struct LanguageDecision { Language language{}; bool stable{}; std::string_view status{"insufficient-evidence"}; };
// State belongs to an actual diarized speaker. Unknown speakers never share history.
class LanguageStabilizer {
    struct History { Language stable{}, candidate{}; unsigned consecutive{}; double duration{}; };
    std::unordered_map<std::uint64_t, History> speakers_;
public:
    LanguageDecision observe(std::optional<std::uint64_t> speaker, LanguageEvidence evidence) {
        const auto language = LanguageRouter::parse(evidence.detected);
        const bool valid = std::isfinite(evidence.probability) && std::isfinite(evidence.durationMs) &&
            evidence.probability >= 0 && evidence.probability <= 1 && evidence.durationMs > 0;
        const bool strong = valid && evidence.probability >= 0.65 && evidence.durationMs >= 700;
        auto found = speaker ? speakers_.find(*speaker) : speakers_.end();
        if (language == Language::unsupported || !strong) {
            // Keep established history, but never guess a translation for an unsupported/uncertain utterance.
            if (found != speakers_.end()) { found->second.candidate = Language::unsupported; found->second.consecutive = 0; found->second.duration = 0; }
            return {language, false, language == Language::unsupported ? "unsupported" : "insufficient-evidence"};
        }
        if (!speaker) return {language, true, "segment-confirmed"};
        if (found == speakers_.end()) {
            if (speakers_.size() >= 64) return {language, true, "speaker-history-capacity"};
            speakers_.emplace(*speaker, History{language, Language::unsupported, 0, 0});
            return {language, true, "speaker-confirmed"};
        }
        auto& history = found->second;
        if (history.stable == language) {
            history.candidate = Language::unsupported; history.consecutive = 0; history.duration = 0;
            return {language, true, "speaker-confirmed"};
        }
        if (history.candidate != language) { history.candidate = language; history.consecutive = 0; history.duration = 0; }
        ++history.consecutive; history.duration += evidence.durationMs;
        if ((history.consecutive >= 2 && history.duration >= 1600) || (evidence.durationMs >= 3000 && evidence.probability >= 0.9)) {
            history.stable = language; history.candidate = Language::unsupported; history.consecutive = 0; history.duration = 0;
            return {language, true, "speaker-language-changed"};
        }
        return {language, false, "language-change-pending"};
    }
    std::optional<Language> established(std::uint64_t speaker) const {
        const auto found = speakers_.find(speaker);
        if (found == speakers_.end()) return std::nullopt;
        return found->second.stable;
    }
    std::size_t size() const { return speakers_.size(); }
    void clear() { speakers_.clear(); }
};
}
