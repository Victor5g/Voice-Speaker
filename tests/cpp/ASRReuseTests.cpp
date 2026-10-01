#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <iostream>
#include <sstream>
#include <cmath>

int main() {
    try {
        const auto root = std::filesystem::current_path();
        nlohmann::json selection, recordings;
        std::ifstream(root / "models/selection.json") >> selection;
        std::ifstream(root / "tests/data/human/manifest.json") >> recordings;
        const auto model = (root / "models" / selection.at("filename").get<std::string>()).string();
        const std::string hash = selection.at("sha256");
        auto configuration = lvt::asrConfiguration((root / "models").string(), model, hash, "metal");
        configuration.reuseEncoder = false;
        lvt::WhisperASR control(model, hash, configuration);
        configuration.reuseEncoder = true;
        lvt::WhisperASR candidate(model, hash, configuration);
        std::size_t comparisons = 0;
        // Alternate languages, lengths and PCM on the same mutable context.
        // A stale encoder result would leak text between windows or speakers.
        for (const auto& recording : recordings.at("recordings")) {
            const auto samples = lvt::readWav16k((root / recording.at("filename").get<std::string>()).string());
            for (auto count : {std::size_t(24000), std::size_t(48000), samples.size()}) {
                const auto pcm = std::span(samples).first(std::min(count, samples.size()));
                auto expected = control.recognize(pcm);
                auto actual = candidate.recognize(pcm, {}, {}, {}, true);
                if (actual.text != expected.text || actual.detectedLanguage != expected.detectedLanguage)
                    throw std::runtime_error("Encoder reuse changed transcription for " + recording.at("filename").get<std::string>() + " length " + std::to_string(pcm.size()));
                if (!expected.boundaryProbability.empty()) throw std::runtime_error("Disabled boundary collection allocated evidence");
                std::istringstream words(actual.text); std::string word; std::size_t wordCount = 0;
                while (words >> word) ++wordCount;
                if (wordCount != actual.boundaryProbability.size() ||
                    std::any_of(actual.boundaryProbability.begin(), actual.boundaryProbability.end(), [](float p) { return !std::isfinite(p) || p < 0 || p > 1; }))
                    throw std::runtime_error("Invalid native word-boundary alignment");
                ++comparisons;
            }
            std::stop_source stopped; stopped.request_stop();
            if (!candidate.recognize(samples, stopped.get_token()).text.empty()) throw std::runtime_error("STOP produced text");
            int checks = 0;
            if (!candidate.recognize(samples, {}, {}, [&] { return ++checks > 1; }).text.empty()) throw std::runtime_error("Obsolete revision produced text");
            if (candidate.recognize(samples).text != control.recognize(samples).text) throw std::runtime_error("Cancellation left stale encoder state");
        }
        std::cout << "PASS " << comparisons << " matched growing-window transcriptions and boundary alignments, 12 STOP and 12 obsolete-revision recovery cases\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
