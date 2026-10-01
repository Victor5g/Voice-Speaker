#include "../../engine/asr/ASRWorker.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/common/ModelHash.hpp"
#include "../../engine/tts/TTSEngine.hpp"
#include "../../engine/audio/SincResampler.hpp"
#include "../../engine/vad/VadWorker.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <cctype>

static std::string compactWords(std::string text) {
    // Explicit spelling normalization for this five-expression regression only.
    // This is test comparison, never a production recognition/translation rule.
    for (std::size_t at = 0; (at = text.find("Ã", at)) != std::string::npos; at += 2) text.replace(at, 2, "ã");
    std::string normalized;
    for (unsigned char byte : text) if (byte >= 128 || std::isalnum(byte)) normalized += char(byte < 128 ? std::tolower(byte) : byte);
    return normalized;
}

int main(int argc, char** argv) { try {
    if (argc != 2) throw std::runtime_error("Usage: asr-controls-native MODEL_ROOT");
    const std::string root = argv[1];
    nlohmann::json selection, manifest, models;
    std::ifstream(root + "/selection.json") >> selection;
    std::ifstream(root + "/manifest.json") >> models;
    std::ifstream("tests/data/human/manifest.json") >> manifest;
    const std::string model = root + "/" + selection.at("filename").get<std::string>();
    const std::string hash = selection.at("sha256");
    unsigned passed = 0, failed = 0, rawASRNonSpeechFailures = 0;
    auto check = [&](bool value, const char* message) {
        if (value) ++passed;
        else { ++failed; std::cerr << "CHECK FAILED " << message << '\n'; }
    };
    {
        lvt::WhisperASR engine(model, hash);
        const auto silence = engine.recognize(std::vector<float>(16000, 0));
        check(silence.text.empty() && silence.rejectedDigitalSilence && engine.languageDetections() == 0,
            "Digital silence is suppressed before automatic language inference");
        const auto dc = engine.recognize(std::vector<float>(16000, .2f));
        check(dc.text.empty() && dc.rejectedDigitalSilence && engine.languageDetections() == 0,
            "DC-only input cannot generate an invented phrase");
        for (const std::string language : {"pt", "en"}) {
            const auto found = std::find_if(manifest.at("recordings").begin(), manifest.at("recordings").end(), [&](const auto& record) {
                return record.at("language") == (language == "pt" ? "pt_br" : "en_us");
            });
            if (found == manifest.at("recordings").end()) throw std::runtime_error("Missing human fixture language");
            const auto file = found->at("filename").get<std::string>();
            lvt::verifyModelHash(file, found->at("sha256"));
            const auto pcm = lvt::readWav16k(file);
            const auto before = engine.languageDetections();
            const auto manual = engine.recognize(pcm, {}, {}, {}, false, language);
            check(engine.languageDetections() == before && manual.languageForced && manual.detectedLanguage == language && !manual.text.empty(),
                "Manual language recognizes real human audio without invoking auto-detection");
            check(manual.detectedProbability == 0 && manual.ptProbability == 0 && manual.enProbability == 0,
                "Manual language does not fabricate detector probabilities");
            const auto automatic = engine.recognize(pcm);
            check(engine.languageDetections() == before + 1 && !automatic.languageForced && !automatic.text.empty(),
                "Automatic language still invokes detection on real human speech");
            std::cout << nlohmann::json{{"fixture", file}, {"mode", "manual-vs-auto"}, {"language", language},
                {"manualMs", manual.latencyMs}, {"automaticMs", automatic.latencyMs},
                {"manualText", manual.text}, {"automaticText", automatic.text},
                {"scope", "Functionality only; two samples are not a latency comparison or semantic acceptance"}}.dump() << '\n';
        }
        auto noise = lvt::readWav16k("tests/data/scenarios/noise-only.wav");
        if (noise.size() > 16000 * 10) noise.resize(16000 * 10);
        const auto noiseResult = engine.recognize(noise, {}, {}, {}, false, "pt");
        std::cout << nlohmann::json{{"fixture", "tests/data/scenarios/noise-only.wav"}, {"frames", noise.size()},
            {"sourceType", "synthetic non-speech fixture"}, {"text", noiseResult.text},
            {"rejectedNoSpeech", noiseResult.rejectedNoSpeech}, {"confidenceMeasured", noiseResult.speechConfidenceMeasured},
            {"noSpeechProbability", noiseResult.minimumNoSpeechProbability}, {"meanTokenLogProbability", noiseResult.meanTextTokenLogProbability}}.dump() << '\n';
        rawASRNonSpeechFailures += !noiseResult.text.empty();
        std::cout << nlohmann::json{{"diagnostic", "raw-ASR-noise-bypasses-production-VAD"},
            {"noInventedText", noiseResult.text.empty()},
            {"limitation", "Raw Whisper can hallucinate on broadband noise despite low no-speech probability. Do not treat this confidence as calibrated or this ASR guard as sufficient."}}.dump() << '\n';
        unsigned sub500ms = 0;
        for (const std::string language : {"pt", "en"}) {
            const std::string voice = language == "pt" ? "pt_BR-faber-medium" : "en_US-lessac-medium";
            const auto bundle = std::find_if(models.at("models").begin(), models.at("models").end(), [&](const auto& entry) {
                return entry.value("type", "") == "tts" && entry.value("voice", "") == voice;
            });
            if (bundle == models.at("models").end()) throw std::runtime_error("Missing synthetic-fixture development voice");
            lvt::TTSEngine tts(lvt::ModelBundle::fromJSON(root, *bundle));
            const std::vector<std::string> words = language == "pt" ? std::vector<std::string>{"Obrigado.", "Sim.", "Não.", "Ok."} : std::vector<std::string>{"Thanks."};
            for (const auto& word : words) {
                const auto audio = tts.synthesize(word);
                lvt::SincResampler resampler;
                auto samples = resampler.to16k(audio.samples, audio.sampleRate);
                // Trim only leading/trailing samples below a declared amplitude;
                // preserve 20 ms context. No time compression or arbitrary cut.
                auto first = std::find_if(samples.begin(), samples.end(), [](float sample) { return std::abs(sample) > .001f; });
                auto last = std::find_if(samples.rbegin(), samples.rend(), [](float sample) { return std::abs(sample) > .001f; });
                if (first == samples.end()) throw std::runtime_error("Synthetic fixture contains no signal");
                const auto start = std::size_t(first - samples.begin()) > 320 ? std::size_t(first - samples.begin()) - 320 : 0;
                const auto end = std::min(samples.size(), samples.size() - std::size_t(last - samples.rbegin()) + 320);
                const auto slice = std::span(samples).subspan(start, end - start);
                auto privateState = engine.forkState();
                const auto result = privateState->recognize(slice, {}, {}, {}, false, language);
                const std::vector<std::string> expected = word == "Thanks." ? std::vector<std::string>{"thanks", "thankyou"}
                    : word == "Não." ? std::vector<std::string>{"não", "nao"}
                    : word == "Ok." ? std::vector<std::string>{"ok", "okay"}
                    : std::vector<std::string>{compactWords(word)};
                const auto hypothesis = compactWords(result.text);
                const bool meaningMatched = std::find(expected.begin(), expected.end(), hypothesis) != expected.end();
                std::cout << nlohmann::json{{"sourceType", "synthetic local TTS; development regression only, not human quality acceptance"},
                    {"voice", voice}, {"input", word}, {"frames", slice.size()}, {"durationMs", result.durationMs},
                    {"text", result.text}, {"rejectedNoSpeech", result.rejectedNoSpeech},
                    {"expectedNormalizedForms", expected}, {"normalizedHypothesis", hypothesis}, {"expressionMatched", meaningMatched},
                    {"confidenceMeasured", result.speechConfidenceMeasured}, {"noSpeechProbability", result.minimumNoSpeechProbability},
                    {"meanTokenLogProbability", result.meanTextTokenLogProbability}, {"asrMs", result.latencyMs}}.dump() << '\n';
                check(!result.text.empty() && !result.rejectedNoSpeech, "Legitimate short expression must not be rejected by silence protection");
                check(meaningMatched, "Short expression must retain its expected words, not merely emit any text");
                if (slice.size() < 8000) {
                    ++sub500ms;
                    check(result.durationMs < 500 && result.timing.inferenceFinished != 0,
                        "Sub-500 ms speech reaches actual native decoding without changing capture duration");
                }
            }
        }
        check(sub500ms > 0, "At least one actual synthesized short fixture exercised sub-500 ms decoding");
    }
    // Non-speech admission belongs to the production VAD boundary, which the
    // raw model probe above deliberately bypasses. Run that actual worker rather
    // than infer its decisions from an unrelated amplitude threshold.
    nlohmann::json scenarios;
    std::ifstream("tests/data/scenarios/manifest.json") >> scenarios;
    for (const std::string name : {"silence", "noise-only"}) {
        const auto& fixture = scenarios.at("fixtures").at(name);
        const auto file = fixture.at("filename").get<std::string>();
        lvt::verifyModelHash(file, fixture.at("sha256"));
        auto pcm = lvt::readWav16k(file); pcm.resize(pcm.size() + 16000, 0);
        lvt::VadWorker vad;
        vad.start(true, lvt::StreamingPolicy::production(), {}, 300);
        std::size_t delivered = 0;
        for (std::size_t offset = 0; offset < pcm.size(); offset += 320) {
            lvt::AudioBlock block;
            block.count = std::min<std::size_t>(320, pcm.size() - offset);
            block.sampleRate = 16000; block.firstFrame = offset;
            std::copy_n(pcm.data() + offset, block.count, block.samples.data());
            if (!vad.push(block)) throw std::runtime_error("Non-speech worker fixture overflowed input queue");
            lvt::SpeechSegment segment;
            while (vad.popSegment(segment)) ++delivered;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (vad.snapshot().frames < pcm.size() / 320 && std::chrono::steady_clock::now() < deadline) {
            lvt::SpeechSegment segment; while (vad.popSegment(segment)) ++delivered;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        lvt::SpeechSegment segment; while (vad.popSegment(segment)) ++delivered;
        const auto snapshot = vad.snapshot();
        check(snapshot.frames == pcm.size() / 320 && !snapshot.dropped && !snapshot.errors,
            "Production VAD consumes every non-speech fixture frame without queue loss");
        check(snapshot.segments == 0 && snapshot.partialPublished == 0 && delivered == 0,
            "Production VAD must not admit synthetic non-speech into ASR");
        std::cout << nlohmann::json{{"fixture", file}, {"boundary", "production-VadWorker"},
            {"voicedFrames", snapshot.voicedFrames}, {"finals", snapshot.segments}, {"partials", snapshot.partialPublished},
            {"delivered", delivered}, {"scope", "Synthetic silence/broadband-noise VAD admission only; music/tone requires downstream pipeline test, pacing is not a latency benchmark"}}.dump() << '\n';
        vad.stop();
    }
    // The actual ASR worker must bypass every diarization model, not merely omit
    // speaker labels after paying its inference cost. Keep playback matching on.
    for (const std::string language : {"pt", "en"}) {
    const auto record = std::find_if(manifest.at("recordings").begin(), manifest.at("recordings").end(), [&](const auto& item) {
        return item.at("language") == (language == "pt" ? "pt_br" : "en_us");
    });
    if (record == manifest.at("recordings").end()) throw std::runtime_error("Missing route fixture");
    auto pcm = lvt::readWav16k(record->at("filename"));
    std::atomic<bool> sent{false};
    std::atomic<unsigned> playbackChecks{0};
    lvt::ASRWorker worker;
    worker.start(model, hash, [&](lvt::SpeechSegment& segment) {
        if (sent.exchange(true)) return false;
        segment = {pcm, 16000, 0, 0}; segment.utterance = 1; segment.final = true;
        return true;
    }, root, {}, [&](std::span<const float>, std::stop_token, bool) { ++playbackChecks; return false; },
        false, "metal", false, language, false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    bool gotText = false, gotEnd = false;
    while (std::chrono::steady_clock::now() < deadline && !gotEnd) {
        lvt::ASRResult result;
        while (worker.pop(result)) {
            if (result.languageStatus == "utterance-end") gotEnd = true;
            else {
                gotText |= !result.text.empty();
                check(result.speaker == 1 && result.languageForced && result.detectedLanguage == language &&
                    result.targetLanguage == (language == "pt" ? "en" : "pt-BR") && result.languageStatus == "manual-language",
                    "Bypassed worker fixes route speaker and uses configured language directly");
            }
        }
        const auto state = worker.snapshot();
        if (state.state == 3) throw std::runtime_error(state.error);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto state = worker.snapshot();
    check(gotText && gotEnd, "Bypassed worker completes speech and end marker");
    check(state.languageDetections == 0 && !state.diarization.loaded && !state.diarization.separationLoaded && state.diarization.latencyMs == 0 && state.diarization.embeddingMs == 0 && state.diarization.overlapMs == 0,
        "No language detection, embedding, overlap or separator model ran in manual bypass mode");
    check(playbackChecks == 1, "Disabling speaker identification retains the playback PCM check");
    worker.stop();
    check(worker.snapshot().languageDetections == 0 && worker.snapshot().speakers == 0, "Worker stop clears route counters");
    lvt::ASRResult late;
    check(!worker.pop(late), "STOP clears pending ASR output");
    }
    std::cout << nlohmann::json{{"result", failed ? "FAIL" : "PASS"}, {"passed", passed}, {"failed", failed},
        {"rawASRNonSpeechFailures", rawASRNonSpeechFailures},
        {"scope", "Manual/automatic controls, short synthetic speech sensitivity, real VAD non-speech admission, worker bypass and STOP. Raw ASR noise hallucination remains separately reported; no broad non-speech quality claim."}}.dump() << '\n';
    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
