#pragma once
#include "../../engine/common/StreamingPolicy.hpp"
#include <nlohmann/json.hpp>
namespace lvt::test {
inline StreamingPolicy streamingPolicy(const nlohmann::json& run) {
    if (!run.contains("streaming")) return StreamingPolicy::production();
    const auto config = run.value("streaming", nlohmann::json::object());
    StreamingPolicy policy{config.value("partialIntervalMs", 1500u), config.value("agreements", 3u),
        config.value("stableAudioMs", 0u), config.value("adaptive", false), config.value("trimSpeakerEdges", false),
        config.value("minimumBoundaryProbability", 0.f), config.value("consecutiveBoundaryEvidence", true),
        config.value("allowCommaToPeriodBoundary", false)};
    policy.validate(); return policy;
}
inline nlohmann::json streamingJSON(const StreamingPolicy& policy) {
    return {{"partialIntervalMs",policy.partialIntervalMs},{"agreements",policy.agreements},
        {"stableAudioMs",policy.stableAudioMs},{"adaptive",policy.adaptive},{"trimSpeakerEdges",policy.trimSpeakerEdges},
        {"minimumBoundaryProbability",policy.minimumBoundaryProbability},{"consecutiveBoundaryEvidence",policy.consecutiveBoundaryEvidence},
        {"allowCommaToPeriodBoundary",policy.allowCommaToPeriodBoundary}};
}
}
