#import <Foundation/Foundation.h>
#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/asr/InferenceArbiter.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/common/MemoryBudget.hpp"
#include <barrier>
#include <iostream>
#include <thread>

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
struct Sample { lvt::ASRResult result; double wait{}, elapsed{}; std::exception_ptr error; };
static bool same(const lvt::ASRResult& a, const lvt::ASRResult& b) {
    return a.text == b.text && a.detectedLanguage == b.detectedLanguage;
}

// Two private states, one set of weights. Serial/concurrent conditions preserve
// each route's PCM and RNG history; no change of sampling policy is hidden here.
// This is an isolated ASR test, not end-to-end call or acoustic validation.
int main(int argc, char** argv) { @autoreleasepool { try {
    if (argc > 3) throw std::runtime_error("Usage: asr-private-state-tests [BACKEND [REPEATS 1..5]]");
    const std::string backend = argc > 1 ? argv[1] : "metal";
    const int repeats = argc > 2 ? std::stoi(argv[2]) : 5;
    if (repeats < 1 || repeats > 5) throw std::runtime_error("Invalid repeat count");
    nlohmann::json selection, manifest;
    std::ifstream("models/selection.json") >> selection;
    std::ifstream("tests/data/human/manifest.json") >> manifest;
    const std::string model = "models/" + selection.at("filename").get<std::string>();
    const std::string hash = selection.at("sha256");
    const auto config = lvt::asrConfiguration("models", model, hash, backend);
    std::vector<std::vector<float>> pcm[2];
    std::vector<nlohmann::json> fixtures[2];
    for (const auto& recording : manifest.at("recordings")) {
        const std::string file = recording.at("filename");
        lvt::verifyModelHash(file, recording.at("sha256"));
        const int route = recording.at("language") == "pt_br" ? 0 : 1;
        pcm[route].push_back(lvt::readWav16k(file)); fixtures[route].push_back(recording);
    }
    if (pcm[0].size() != 6 || pcm[1].size() != 6) throw std::runtime_error("Expected six verified fixtures per language");
    const std::size_t count = pcm[0].size() * repeats;
    std::vector<Sample> reference[2];
    lvt::ASRResult referenceAfterCancellation, referenceAfterParentDestruction;
    std::size_t mismatches = 0;
    for (bool concurrent : {false, true}) {
        const auto loadStart = Clock::now();
        auto original = std::make_unique<lvt::WhisperASR>(model, hash, config);
        const auto primaryFootprint = lvt::MemoryBudget::physicalFootprint();
        auto fork = original->forkState();
        const auto forkFootprint = lvt::MemoryBudget::physicalFootprint();
        const double loadMs = ms(loadStart, Clock::now());
        lvt::WhisperASR* engines[]{original.get(), fork.get()};
        for (int route = 0; route < 2; ++route) {
            if (!engines[route]->metalActive()) throw std::runtime_error("State did not initialize Metal");
            (void)engines[route]->recognize(pcm[route][0]);
        }
        std::vector<Sample> results[2];
        for (auto& list : results) list.resize(count);
        lvt::InferenceArbiter serial;
        std::barrier barrier(3);
        std::jthread workers[2];
        for (int route = 0; route < 2; ++route) workers[route] = std::jthread([&, route](std::stop_token stop) {
            @autoreleasepool {
                for (std::size_t i = 0; i < count; ++i) {
                    barrier.arrive_and_wait();
                    auto& sample = results[route][i];
                    try {
                        const auto start = Clock::now();
                        auto lease = concurrent ? lvt::InferenceArbiter::Lease{} : serial.acquire(true, stop);
                        sample.wait = ms(start, Clock::now());
                        sample.result = engines[route]->recognize(pcm[route][i % pcm[route].size()], stop);
                        sample.elapsed = ms(start, Clock::now());
                    } catch (...) { sample.error = std::current_exception(); }
                    barrier.arrive_and_wait();
                }
            }
        });
        std::size_t peak = forkFootprint;
        for (std::size_t i = 0; i < count; ++i) {
            const auto start = Clock::now();
            barrier.arrive_and_wait(); barrier.arrive_and_wait();
            const double pairMs = ms(start, Clock::now());
            peak = std::max(peak, lvt::MemoryBudget::physicalFootprint());
            // Defer errors until the threads finish all barrier rounds.
            for (int route = 0; route < 2; ++route) {
                const auto& sample = results[route][i];
                if (sample.error) continue;
                const bool matched = !concurrent || same(sample.result, reference[route][i].result);
                mismatches += !matched;
                const auto& fixture = fixtures[route][i % fixtures[route].size()];
                std::cout << nlohmann::json{{"mode",concurrent ? "concurrent-private" : "serial-private"},
                    {"backend",backend},{"route",route},{"iteration",i},{"fixture",fixture["filename"]},
                    {"sha256",fixture["sha256"]},{"reference",fixture["reference"]},
                    {"original",sample.result.text},{"language",sample.result.detectedLanguage},
                    {"matchedSerial",matched},{"modelWaitMs",sample.wait},{"asrMs",sample.result.latencyMs},
                    {"requestToResultMs",sample.elapsed},{"pairMs",pairMs},{"footprintBytes",peak},
                    {"thermalState",int(NSProcessInfo.processInfo.thermalState)}}.dump() << std::endl;
            }
        }
        for (auto& worker : workers) worker.join();
        for (const auto& list : results) for (const auto& sample : list)
            if (sample.error) std::rethrow_exception(sample.error);
        if (!concurrent) for (int route = 0; route < 2; ++route) reference[route] = results[route];
        int checks = 0;
        const auto cancelled = original->recognize(pcm[0][0], {}, {}, [&] { return concurrent && ++checks >= 6; });
        if (concurrent && (!cancelled.text.empty() || checks < 6)) throw std::runtime_error("Native cancellation did not execute");
        const auto afterCancellation = fork->recognize(pcm[1][0]);
        original.reset(); // Fork retains weights and Core ML encoder path storage.
        const auto afterParentDestruction = fork->recognize(pcm[1][1]);
        if (!concurrent) {
            referenceAfterCancellation = afterCancellation;
            referenceAfterParentDestruction = afterParentDestruction;
        } else {
            if (!same(afterCancellation, referenceAfterCancellation)) throw std::runtime_error("Cancellation crossed route state");
            if (!same(afterParentDestruction, referenceAfterParentDestruction)) throw std::runtime_error("Weights did not outlive original wrapper");
        }
        std::cout << nlohmann::json{{"modeSummary",true},{"mode",concurrent ? "concurrent-private" : "serial-private"},
            {"loadMs",loadMs},{"primaryFootprintBytes",primaryFootprint},{"forkFootprintBytes",forkFootprint},
            {"peakFootprintBytes",peak},{"samplesPerRoute",count},{"cancellationIsolation",true},
            {"parentLifetime",true}}.dump() << std::endl;
    }
    std::cout << nlohmann::json{{"result",mismatches ? "FAIL" : "PASS"},{"mismatches",mismatches},
        {"matchedComparisons",2 * count - mismatches},{"backend",backend},
        {"scope","Private-state serial versus concurrent ASR only; no complete-call or quality acceptance"}}.dump() << std::endl;
    return mismatches ? 1 : 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
