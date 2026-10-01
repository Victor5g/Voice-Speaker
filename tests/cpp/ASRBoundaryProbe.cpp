#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
// Explicit development fixture diagnostic; never included in the application.
int main(int argc, char** argv) { try {
    if (argc < 6) throw std::runtime_error("Usage: boundary-probe model sha fixture firstFrame endFrame...");
    lvt::WhisperASR asr(argv[1], argv[2]);
    const auto pcm = lvt::readWav16k(argv[3]);
    const auto first = std::stoull(argv[4]);
    for (int i=5; i<argc; ++i) {
        const auto end = std::stoull(argv[i]);
        if (first >= end || end > pcm.size()) throw std::runtime_error("Invalid fixture slice");
        const auto result = asr.recognize(std::span(pcm).subspan(first, end-first), {}, {}, {}, true);
        std::cout << nlohmann::json{{"endFrame",end},{"text",result.text},
            {"boundaryProbability",result.boundaryProbability},{"asrMs",result.latencyMs}}.dump() << std::endl;
    }
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
