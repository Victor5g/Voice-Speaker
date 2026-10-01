#pragma once
#include <cstdint>
#include <mach/mach_time.h>
#include <optional>

namespace lvt {
namespace hostclock {
inline double ticksPerSecond() {
    static const double value = [] { mach_timebase_info_data_t tb{}; mach_timebase_info(&tb); return 1e9 * tb.denom / tb.numer; }();
    return value;
}
inline std::uint64_t now() { return mach_absolute_time(); }
inline std::uint64_t advance(std::uint64_t start, double seconds) {
    return start && seconds >= 0 ? start + std::uint64_t(seconds * ticksPerSecond()) : 0;
}
inline std::optional<double> milliseconds(std::uint64_t start, std::uint64_t end) {
    if (!start || !end || end < start) return std::nullopt;
    return (end - start) * 1000.0 / ticksPerSecond();
}
}
enum class EndpointReason : std::uint8_t { none, silence, windowLimit };
// Plain timestamps carried with an immutable revision, never wall clock or text.
// speechEnd is the LAST POSITIVE VAD frame, not an acoustic reference annotation.
// speechEndFrame is always on the canonical 16 kHz capture timeline.
struct PipelineTiming {
    std::uint64_t speechEndHostTime{}, speechEndFrame{}, windowEndHostTime{}, windowEndFrame{};
    EndpointReason endpointReason{EndpointReason::none};
    std::uint64_t endpointHostTime{}, vadPublished{}, asrDequeued{};
    std::uint64_t diarizationStarted{}, diarizationFinished{};
    std::uint64_t inferenceWaiting{}, inferenceStarted{}, melFinished{}, languageFinished{}, inferenceFinished{};
    std::uint64_t confirmed{}, translationQueued{}, translationStarted{}, translationFinished{};
    std::uint64_t ttsQueued{}, ttsStarted{}, voiceReady{}, ttsFinished{}, playbackQueued{};
};
}
