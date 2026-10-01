#include "../../engine/audio/PlaybackReferenceHistory.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
static std::vector<float> expand(const std::string& path, std::size_t factor) {
    const auto bytes = std::filesystem::file_size(path);
    if (bytes % sizeof(float) || bytes > 1000000) throw std::runtime_error("Invalid regression PCM");
    std::vector<float> input(bytes / sizeof(float));
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char*>(input.data()), bytes)) throw std::runtime_error("Cannot read regression PCM");
    std::vector<float> output; output.reserve(input.size() * factor);
    for (const auto sample : input) for (std::size_t n = 0; n < factor; ++n) output.push_back(sample);
    return output;
}
int main() {
    try {
        nlohmann::json manifest;
        std::ifstream("tests/data/echo-regression/manifest.json") >> manifest;
        std::size_t passed = 0;
        for (const auto& example : manifest.at("cases")) {
            lvt::PlaybackReferenceHistory history;
            history.append8k(expand(example["reference"]["filename"], 4));
            const bool matched = history.matches16k(expand(example["source"]["filename"], 8), {}, example["separated"]);
            if (matched != example["expected"]) throw std::runtime_error("Wrong echo classification: " + example["id"].get<std::string>());
            std::cout << "PASS recorded synthetic waveform " << example["id"] << '\n'; ++passed;
        }
        std::cout << "RESULT " << passed << " passed, 0 failed\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
