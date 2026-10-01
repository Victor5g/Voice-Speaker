#include "../../engine/conversation/ConversationPipeline.hpp"
#include "CorpusDrainTracker.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace {
constexpr unsigned rate = 48000;
constexpr std::uint64_t session = 947;
constexpr const char* expectedVoice = "pt_BR-faber-medium";
struct Case { const char* source; const char* term; };
const std::array<Case, 4> cases{{
    {"Today we are doing an unboxing of this product.", "unboxing"},
    {"Thank you for the feedback.", "feedback"},
    {"My coach helped me with my career.", "coach"},
    {"Please do not tell me any spoilers.", "spoilers"}
}};
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void writeWav(const std::filesystem::path& path, const std::vector<float>& pcm) {
    require(!std::filesystem::is_symlink(path), "WAV destination is a symlink");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    require(bool(file), "Cannot create development WAV");
    auto u16 = [&](std::uint16_t value) { file.put(char(value & 255)); file.put(char((value >> 8) & 255)); };
    auto u32 = [&](std::uint32_t value) { u16(value & 65535); u16(value >> 16); };
    file.write("RIFF", 4); u32(36 + std::uint32_t(pcm.size() * 2)); file.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    file.write("data", 4); u32(std::uint32_t(pcm.size() * 2));
    for (float value : pcm) u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lrint(std::clamp(value, -1.f, 1.f) * 32767.f))));
    file.flush(); require(bool(file), "Incomplete development WAV");
}
}

int main(int argc, char** argv) { try {
    const std::string modelRoot = argc > 1 ? argv[1] : "models";
    nlohmann::json seed; std::ifstream("app/Resources/glossary-defaults.json") >> seed;
    const auto destination = std::filesystem::path("artifacts/realtime-improvements");
    require(!std::filesystem::is_symlink("artifacts") && !std::filesystem::is_symlink(destination), "Unsafe artifact folder");
    std::filesystem::create_directories(destination);
    lvt::PCMPlayback playback;
    lvt::ConversationPipeline pipeline(playback);
    lvt::ProcessingOptions options;
    options.sourceLanguage = "en"; options.targetLanguage = "pt";
    options.identifySpeakers = true; options.speechOutput = true;
    options.glossaryJSON = seed.dump();
    std::atomic<std::size_t> allowed{0}, supplied{0};
    pipeline.start(modelRoot, rate, session, [&](lvt::ASRResult& result) {
        const auto index = supplied.load();
        if (index >= allowed.load() || index >= cases.size()) return false;
        result = {};
        result.text = cases[index].source; result.detectedLanguage = "en";
        result.targetLanguage = "pt-BR"; result.speaker = 1; result.utterance = index + 1;
        result.final = true; result.hostTime = lvt::hostclock::now();
        supplied = index + 1;
        return true;
    }, lvt::StreamingPolicy::production(), options);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = pipeline.snapshot();
        if (status.translationState == 3 || status.ttsState == 3) throw std::runtime_error(status.error);
        if (status.translationState == 2 && status.ttsState == 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(pipeline.snapshot().translationState == 2 && pipeline.snapshot().ttsState == 2, "Initialization timed out");
    allowed = 1;
    std::map<std::uint64_t, lvt::ConversationEvent> queued;
    std::set<std::uint64_t> translated, played, started;
    lvt::test::CorpusDrainTracker drain;
    std::array<float, 960> buffer{};
    std::vector<float> captured;
    std::uint64_t active = 0;
    double energy = 0;
    std::size_t totalSamples = 0;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline && played.size() < cases.size()) {
        lvt::ConversationEvent event;
        while (pipeline.pop(event)) {
            const auto& id = event.result.identity;
            drain.observe(id.segment, event.state);
            require(id.session == session && id.speaker == 1, "Crossed session or speaker identity");
            if (event.state == "error" || event.state == "unavailable" || event.state == "dropped" || event.state == "audio-skipped" || event.state == "revision-conflict")
                throw std::runtime_error("Unexpected terminal event: " + event.state + " " + event.error);
            if (event.state == "translated") {
                require(id.sourceSegment % 8 == 0 && id.sourceSegment >= 8 && id.sourceSegment / 8 <= cases.size(), "Unexpected source segment");
                const auto& test = cases[id.sourceSegment / 8 - 1];
                require(event.result.original == test.source && event.result.sourceLanguage == "en" && event.result.targetLanguage == "pt-BR", "Wrong translation route");
                require(event.result.translated.find(test.term) != std::string::npos, "Glossary term missing before TTS");
                require(translated.insert(id.segment).second, "Duplicate translated segment");
            }
            if (event.state == "queued") {
                require(event.voice == expectedVoice && translated.contains(id.segment), "Unexpected queued voice or untranslated audio");
                require(active == 0 && captured.empty(), "More than one utterance admitted before playback completed");
                require(queued.emplace(id.segment, event).second, "Duplicate queued segment");
                active = id.segment; energy = 0;
            }
            if (event.state == "playing") {
                require(active == id.segment && queued.contains(id.segment), "Playback started without matching queued PCM");
                require(started.insert(id.segment).second, "Duplicate playback start");
            }
            if (event.state == "played") {
                require(active == id.segment && queued.contains(id.segment) && started.contains(id.segment), "Playback identity did not match queued utterance");
                const auto& original = queued.at(id.segment);
                require(original.result.identity.sourceSegment == id.sourceSegment && original.result.identity.session == id.session &&
                        original.result.identity.speaker == id.speaker && original.voice == expectedVoice, "Playback source/voice association changed");
                require(pipeline.snapshot().loadedVoice == expectedVoice, "TTS engine changed voice during glossary playback");
                require(energy > 0 && !captured.empty(), "Synthesized playback contained no energy");
                require(played.insert(id.segment).second, "Repeated playback completion");
                const auto wav = destination / ("glossary-voice-" + std::to_string(id.sourceSegment / 8) + ".wav");
                writeWav(wav, captured);
                totalSamples += captured.size();
                std::cout << nlohmann::json{{"state", "played"}, {"source", original.result.original},
                    {"translation", original.result.translated}, {"speaker", id.speaker}, {"session", id.session},
                    {"segment", id.segment}, {"sourceSegment", id.sourceSegment}, {"queuedVoice", original.voice},
                    {"playedVoiceAssociation", original.voice}, {"voiceEvidence", "queued identity matched to played identity and loaded TTS voice"},
                    {"samples", captured.size()}, {"sampleRate", rate}, {"energy", energy}, {"wav", wav.string()},
                    {"intelligibility", "PENDING human listening"}}.dump() << std::endl;
                captured.clear(); active = 0;
                // One final at a time: the next source is admitted only after the
                // previous PCM finished; no test-specific overflow bypass.
                allowed = played.size() + 1;
            }
        }
        if (active) {
            playback.render(buffer.data(), buffer.size(), rate);
            for (float value : buffer) {
                require(std::isfinite(value) && std::abs(value) <= 1, "Invalid playback PCM");
                energy += double(value) * value;
            }
            captured.insert(captured.end(), buffer.begin(), buffer.end());
            require(captured.size() <= rate * 60, "Unbounded PCM capture in voice test");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto metrics = pipeline.snapshot();
    require(played.size() == 4 && queued.size() == 4 && translated == played && started == played && drain.idle(), "Not all four utterances completed");
    require(metrics.translated == 4 && metrics.synthesized == 4 && metrics.audioSkipped == 0 && metrics.eventDrops == 0 && playback.dropped() == 0,
            "Dropped or duplicated production work");
    require(metrics.loadedVoice == expectedVoice && metrics.cachedVoices == expectedVoice, "Unexpected voice loaded in automatic assignment");
    require(metrics.playbackSeconds == 0 && playback.depth() == 0, "PCM remains after completion");
    pipeline.requestStop();
    playback.render(buffer.data(), buffer.size(), rate);
    require(std::all_of(buffer.begin(), buffer.end(), [](float value) { return value == 0; }), "STOP leaked PCM");
    pipeline.stop();
    lvt::ConversationEvent late;
    require(!pipeline.pop(late) && playback.depth() == 0 && playback.pendingSeconds() == 0, "STOP left events or audio queued");
    playback.render(buffer.data(), buffer.size(), rate);
    require(std::all_of(buffer.begin(), buffer.end(), [](float value) { return value == 0; }), "Stopped pipeline rendered residual PCM");
    std::cout << nlohmann::json{{"result", "PASS"}, {"translated", translated.size()}, {"queued", queued.size()},
        {"played", played.size()}, {"speaker", 1}, {"voice", expectedVoice}, {"sampleRate", rate}, {"totalSamples", totalSamples},
        {"stopCleared", true}, {"intelligibility", "PENDING human listening"},
        {"scope", "Scripted ASR finals into production translation/TTS/PCMPlayback; automatic speaker-to-voice assignment; accelerated render; not ASR, diarization, physical speaker output, live latency or pronunciation acceptance"}}.dump() << std::endl;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
