#include "../../engine/separation/SeparationEngine.hpp"
#include "../../engine/speakers/OverlapDetector.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <sherpa-onnx/c-api/c-api.h>
#include <bit>
#include <chrono>
#include <iostream>
#include <sys/resource.h>
double sisdr(std::span<const float> estimate, std::span<const float> reference) {
    double dot = 0, energy = 0, signal = 0, noise = 0;
    for (std::size_t i = 0; i < estimate.size(); ++i) { dot += estimate[i] * reference[i]; energy += reference[i] * reference[i]; }
    const double scale = dot / std::max(energy, 1e-12);
    for (std::size_t i = 0; i < estimate.size(); ++i) { const auto target = scale * reference[i]; signal += target * target; noise += (estimate[i] - target) * (estimate[i] - target); }
    return 10 * std::log10(std::max(signal, 1e-12) / std::max(noise, 1e-12));
}
int main(int argc, char** argv) {
    try {
        nlohmann::json entries; std::ifstream("models/speaker-assets.json") >> entries;
        auto bundle = [&](const char* type) { return lvt::ModelBundle::fromJSON("models", *std::find_if(entries.begin(), entries.end(), [&](const auto& m) { return m["type"] == type; })); };
        lvt::OverlapDetector detector(bundle("overlap"));
        lvt::SeparationEngine separator(bundle("separation"));
        std::filesystem::create_directories("artifacts/separation");
        const bool human = argc == 3;
        std::vector<std::pair<std::string,std::string>> pairs = human ? std::vector<std::pair<std::string,std::string>>{{argv[1], argv[2]}} :
            std::vector<std::pair<std::string,std::string>>{{"tests/data/generated/enus.wav", "tests/data/generated/engb.wav"}, {"tests/data/generated/ptbr.wav", "tests/data/generated/enus.wav"}};
        for (const auto& [pathA, pathB] : pairs) {
            const auto nameA = std::filesystem::path(pathA).stem().string(), nameB = std::filesystem::path(pathB).stem().string();
            auto a = lvt::readWav16k(pathA);
            auto b = lvt::readWav16k(pathB);
            const auto count = std::min({a.size(), b.size(), std::size_t(human ? 128000 : 64000)});
            a.resize(count); b.resize(count);
            if (human) {
                // Known-source evaluation at equal RMS; this is fixture preparation,
                // never speaker identification or runtime access to unmixed sources.
                for (auto* source : {&a, &b}) {
                    double energy = 0; for (float v : *source) energy += v * v;
                    const double gain = .1 / std::max(std::sqrt(energy / count), 1e-9);
                    for (float& v : *source) v *= gain;
                }
            }
            std::vector<float> mix(count); for (std::size_t i = 0; i < count; ++i) mix[i] = .5f * (a[i] + b[i]);
            {
                lvt::OnnxModel diagnostic(bundle("separation"), 2, false);
                std::vector<float> chunk(mix.begin(), mix.begin() + std::min<std::size_t>(32000, mix.size()));
                const std::array<std::int64_t, 2> shape{1, std::int64_t(chunk.size())};
                auto raw = diagnostic.run(chunk, shape, 320000);
                float maximum = 0; std::size_t nonfinite = 0;
                for (float v : raw.values) { if (!std::isfinite(v)) ++nonfinite; else maximum = std::max(maximum, std::abs(v)); }
                std::cout << nlohmann::json{{"diagnosticShape",raw.shape},{"maxOutput",maximum},{"nonfinite",nonfinite},{"maxInput",*std::max_element(chunk.begin(),chunk.end())}}.dump() << std::endl;
            }
            auto regions = detector.analyze(mix);
            std::size_t overlapping = 0; for (const auto& r : regions) if (std::popcount(r.localMask) == 2) overlapping += r.end - r.start;
            const auto begin = std::chrono::steady_clock::now();
            auto result = separator.separate(mix);
            const auto direct = sisdr(result.sources[0], a) + sisdr(result.sources[1], b);
            const auto swapped = sisdr(result.sources[1], a) + sisdr(result.sources[0], b);
            if (swapped > direct) std::swap(result.sources[0], result.sources[1]);
            rusage usage{}; getrusage(RUSAGE_SELF, &usage);
            const auto prefix = std::string("artifacts/separation/") + nameA + "-" + nameB;
            for (std::size_t i = 0; i < 2; ++i) SherpaOnnxWriteWave(result.sources[i].data(), count, 16000, (prefix + "-" + std::to_string(i) + ".wav").c_str());
            std::cout << nlohmann::json{{"sourceA",nameA},{"sourceB",nameB},{"overlapFraction",double(overlapping)/count},
                {"sourceASI_SDRImprovementDb",sisdr(result.sources[0],a)-sisdr(mix,a)},
                {"sourceBSI_SDRImprovementDb",sisdr(result.sources[1],b)-sisdr(mix,b)},
                {"consistencyError",result.consistencyError},{"admittedAsReliable",result.reliable},
                {"separationMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()},
                {"peakRSSBytes",usage.ru_maxrss},{"fixtureType",human ? "human CC-BY-4.0 FLEURS, known unmixed sources normalized to equal RMS" : "synthetic speech, known unmixed sources"}}.dump() << std::endl;
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
