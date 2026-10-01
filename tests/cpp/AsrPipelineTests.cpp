#include "../../engine/audio/AudioCore.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    try {
        lvt::AudioCore core;
        core.configureASR(argv[1], argv[2]); core.start();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (core.snapshot().asr.state == 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (core.snapshot().asr.state != 2 || !core.snapshot().asr.metal)
            throw std::runtime_error("verified ASR model did not initialize with Metal: " + core.snapshot().asr.error);
        int passed = 1;
        std::cout << "PASS verified model loaded through production worker with Metal\n";
        for (auto [file, language] : {std::pair{"ptbr", "pt"}, {"enus", "en"}, {"engb", "en"}}) {
            auto samples = lvt::readWav16k(std::string("tests/data/generated/") + file + ".wav");
            samples.resize(samples.size() + 16000, 0);
            const auto framesTarget = (core.snapshot().inputFrames + samples.size()) / 320;
            for (std::size_t i = 0; i < samples.size();) {
                const auto count = std::min<std::size_t>(320, samples.size() - i);
                core.capture(samples.data() + i, (std::uint32_t)count, 16000, 0);
                i += count;
                // Pace fixture input so bounded production queues are exercised without deliberate overflow.
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (core.snapshot().vad.frames < framesTarget && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto segmentsTarget = core.snapshot().vad.segments;
            while (core.snapshot().asr.processed < segmentsTarget && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            lvt::ASRResult result;
            bool matched = false;
            while (core.popTranscript(result)) {
                matched |= result.detectedLanguage == language && !result.text.empty() &&
                    result.targetLanguage == (std::string_view(language) == "pt" ? "en" : "pt-BR");
                std::cout << "FIXTURE " << file << " language=" << result.detectedLanguage << " ASR_ms=" << result.latencyMs << " text=" << result.text << '\n';
            }
            if (!matched || core.snapshot().dropped || core.snapshot().vad.dropped || core.snapshot().vad.errors || core.snapshot().asr.processed != segmentsTarget)
                throw std::runtime_error(std::string("capture/VAD/ASR fixture failed: ") + file);
            ++passed; std::cout << "PASS capture -> real VAD -> real ASR: " << file << '\n';
        }
        core.stop();
        lvt::ASRResult discarded;
        if (core.snapshot().asr.state != 0 || core.snapshot().asr.completed || core.popTranscript(discarded) || core.snapshot().depth)
            throw std::runtime_error("Stop left session state");
        std::cout << "PASS Stop clears actual inference session\nRESULT " << ++passed << " passed, 0 failed\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
