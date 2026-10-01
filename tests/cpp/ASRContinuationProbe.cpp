#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/common/ModelHash.hpp"
#include <charconv>
#include <chrono>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
std::size_t frameArgument(const char* raw) {
    const std::string_view value(raw);
    std::uint64_t parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size() ||
        parsed > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("Frame arguments must be unsigned decimal integers");
    return std::size_t(parsed);
}

nlohmann::json milliseconds(std::uint64_t start, std::uint64_t end) {
    const auto value = lvt::hostclock::milliseconds(start, end);
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}
}

// Development-only acoustic-context comparison. Frames are relative to the WAV,
// with an exclusive end. No route selection, text repair, translation or TTS.
// Each condition starts with a fresh model and state, without a warmup or prompt.
// This isolates context length from RNG history, but does NOT recreate a live
// session's prior inference history, diarization, resampling or VAD decisions.
int main(int argc, char** argv) {
    if (argc < 6 || argc > 8) {
        std::cerr << "Usage: asr-continuation-probe MODEL SHA256 WAV FIRST_FRAME END_FRAME [CONTEXT_FRAMES [FORCED_LANGUAGE]]\n";
        return 2;
    }
    try {
        const auto first = frameArgument(argv[4]);
        const auto end = frameArgument(argv[5]);
        const auto context = argc >= 7 ? frameArgument(argv[6]) : 0;
        const std::string forced = argc == 8 ? argv[7] : "";
        const auto pcm = lvt::readWav16k(argv[3]);
        if (first >= end || end > pcm.size()) throw std::runtime_error("Slice outside WAV bounds or empty");
        if (end - first < 8000) throw std::runtime_error("Slice is shorter than production ASR's 500 ms minimum");
        if (context > first) throw std::runtime_error("Requested left context extends before WAV start");
        if (end - (first - context) > 480000) throw std::runtime_error("Extended slice exceeds the 30 second probe limit");
        const auto fixtureHash = lvt::sha256File(argv[3]);
        for (unsigned condition = 0; condition < (context ? 2U : 1U); ++condition) {
            const auto actualFirst = condition ? first - context : first;
            const auto loadStarted = std::chrono::steady_clock::now();
            lvt::WhisperASR engine(argv[1], argv[2]);
            const auto loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStarted).count();
            const auto result = engine.recognize(std::span(pcm).subspan(actualFirst, end - actualFirst), {}, {}, {}, false, forced);
            const auto& timing = result.timing;
            std::cout << nlohmann::json{
                {"schemaVersion", 1}, {"probe", "asr-acoustic-continuation"},
                {"condition", condition ? "left-extended-acoustic-context" : "original-slice"},
                {"model", argv[1]}, {"modelSHA256", argv[2]},
                {"fixture", argv[3]}, {"fixtureSHA256", fixtureHash},
                {"sampleRate", 16000}, {"fixtureFrames", pcm.size()},
                {"requestedFirstFrame", first}, {"firstFrame", actualFirst}, {"endFrameExclusive", end},
                {"contextFrames", condition ? context : 0},
                {"backend", "metal"}, {"metalActive", engine.metalActive()}, {"encoderReuse", true},
                {"independentFreshState", true}, {"priorInferenceCount", 0}, {"promptUsed", false},
                {"productionHistoryReproduced", false}, {"fixtureBoundsProvidedByCaller", true},
                {"textIncludesLeftContext", bool(condition)}, {"rawText", result.text},
                {"rawLanguage", result.detectedLanguage}, {"detectedProbability", result.detectedProbability},
                {"languageForced", result.languageForced}, {"languageDetections", engine.languageDetections()},
                {"speechConfidenceMeasured", result.speechConfidenceMeasured}, {"minimumNoSpeechProbability", result.minimumNoSpeechProbability},
                {"meanTextTokenLogProbability", result.meanTextTokenLogProbability}, {"peakBlockRMS", result.peakBlockRMS},
                {"rejectedNoSpeech", result.rejectedNoSpeech},
                {"ptProbability", result.ptProbability}, {"enProbability", result.enProbability},
                {"modelLoadMs", loadMs}, {"asrMs", result.latencyMs}, {"audioDurationMs", result.durationMs},
                {"timing", {{"clock", "mach_absolute_time"},
                    {"inferenceStarted", timing.inferenceStarted}, {"melFinished", timing.melFinished},
                    {"languageFinished", timing.languageFinished}, {"inferenceFinished", timing.inferenceFinished},
                    {"melMs", milliseconds(timing.inferenceStarted, timing.melFinished)},
                    {"languageMs", milliseconds(timing.melFinished, timing.languageFinished)},
                    {"decodeMs", milliseconds(timing.languageFinished, timing.inferenceFinished)}}},
                {"scope", "Isolated ASR experiment; no repaired text, translation, playback or production fidelity claim"}
            }.dump() << std::endl;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << nlohmann::json{{"probe", "asr-acoustic-continuation"}, {"error", error.what()}}.dump() << '\n';
        return 1;
    }
}
