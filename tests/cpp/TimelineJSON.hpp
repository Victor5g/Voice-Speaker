#pragma once
#include "../../engine/conversation/ConversationTypes.hpp"
#include <nlohmann/json.hpp>
namespace lvt::test {
inline nlohmann::json timelineJSON(const ConversationEvent& event) {
    const auto& t = event.result.identity.timing;
    nlohmann::json j{{"clock","mach_absolute_time"},{"speechEndKind","last-positive-VAD-frame"},
        {"endpointReason",int(t.endpointReason)},{"speechEndFrame",t.speechEndFrame},
        {"speechEnd",t.speechEndHostTime},{"windowEnd",t.windowEndHostTime},{"windowEndFrame",t.windowEndFrame},
        {"endpoint",t.endpointHostTime},{"vadPublished",t.vadPublished},{"asrDequeued",t.asrDequeued},
        {"diarizationStarted",t.diarizationStarted},{"diarizationFinished",t.diarizationFinished},
        {"inferenceWaiting",t.inferenceWaiting},{"inferenceStarted",t.inferenceStarted},
        {"melFinished",t.melFinished},{"languageFinished",t.languageFinished},{"inferenceFinished",t.inferenceFinished},
        {"confirmed",t.confirmed},{"translationQueued",t.translationQueued},{"translationStarted",t.translationStarted},
        {"translationFinished",t.translationFinished},{"ttsQueued",t.ttsQueued},{"ttsStarted",t.ttsStarted},
        {"voiceReady",t.voiceReady},{"ttsFinished",t.ttsFinished},{"playbackQueued",t.playbackQueued},
        {"playbackEvent",event.playbackHostTime},{"ticksPerSecond",hostclock::ticksPerSecond()}};
    auto ms = [&](const char* name, std::uint64_t begin, std::uint64_t end) {
        auto value = hostclock::milliseconds(begin,end);
        j[name] = value ? nlohmann::json(*value) : nlohmann::json(nullptr);
    };
    ms("endpointEstimatedMs",t.speechEndHostTime,t.endpointHostTime);
    ms("asrPendingWaitMs",t.vadPublished,t.asrDequeued);
    ms("asrModelWaitMs",t.inferenceWaiting,t.inferenceStarted);
    ms("diarizationMs",t.diarizationStarted,t.diarizationFinished);
    ms("melMs",t.inferenceStarted,t.melFinished);
    ms("languageMs",t.melFinished,t.languageFinished);
    ms("decodeMs",t.languageFinished,t.inferenceFinished);
    ms("confirmationAfterDecodeMs",t.inferenceFinished,t.confirmed);
    ms("translationWaitMs",t.translationQueued,t.translationStarted);
    ms("translationMs",t.translationStarted,t.translationFinished);
    ms("ttsWaitMs",t.ttsQueued,t.ttsStarted);
    ms("voiceLoadMs",t.ttsStarted,t.voiceReady);
    ms("ttsMs",t.voiceReady,t.ttsFinished);
    if(event.state=="playing") ms("playbackWaitMs",t.playbackQueued,event.playbackHostTime);
    if(event.state=="audio-onset" && t.endpointReason==EndpointReason::silence)
        ms("afterVadEndEstimatedMs",t.speechEndHostTime,event.playbackHostTime);
    return j;
}
}
