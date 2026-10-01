#include "../../engine/asr/InferenceArbiter.hpp"
#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <atomic>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// Controlled contention using the production arbiter and native Whisper. The
// final is deliberately queued at a cancellation checkpoint, not an acoustic
// endpoint. This proves abort/recovery and measures model waiting only, not E2E.
int main(int argc, char** argv) { try {
    if (argc > 4 || (argc > 2 && std::string(argv[2]) != "reuse" && std::string(argv[2]) != "no-reuse"))
        throw std::runtime_error("Usage: asr-preemption-tests [BACKEND [reuse|no-reuse [RECORDING_LIMIT]]]");
    const std::string backend = argc > 1 ? argv[1] : "metal";
    nlohmann::json selection, corpus;
    std::ifstream("models/selection.json") >> selection;
    std::ifstream("tests/data/human/manifest.json") >> corpus;
    const std::string model = "models/" + selection.at("filename").get<std::string>();
    const std::string hash = selection.at("sha256");
    auto config = lvt::asrConfiguration("models", model, hash, backend);
    config.reuseEncoder = !(argc > 2 && std::string(argv[2]) == "no-reuse");
    std::vector<std::vector<float>> recordings;
    for (const auto& item : corpus.at("recordings")) {
        const std::string file = item.at("filename");
        lvt::verifyModelHash(file, item.at("sha256"));
        recordings.push_back(lvt::readWav16k(file));
    }
    std::size_t cases = 0, aborted = 0, matchedControls = 0, mismatches = 0;
    const auto limit = argc > 3 ? std::min(recordings.size(), std::size_t(std::stoul(argv[3]))) : recordings.size();
    if (!limit) throw std::runtime_error("At least one recording is required");
    for (std::size_t i = 0; i < limit; ++i) {
        const auto next = (i + recordings.size() / 2) % recordings.size();
        const auto previewPCM = std::span(recordings[i]).first(std::min<std::size_t>(96000, recordings[i].size()));
        for (int checkpoint : {2, 6}) {
        lvt::ASRResult expectedFinal, expectedRecovery, expectedPreview;
        for (int run = 0; run < 3; ++run) {
            const bool enabled = run == 2;
            // Same fresh state, warmup PCM and thread ownership in all three
            // runs. First compare A/A before attributing any difference to abort.
            lvt::WhisperASR engine(model, hash, config);
            (void)engine.recognize(previewPCM);
            lvt::InferenceArbiter arbiter;
            auto previewLease = arbiter.acquire(false, {}, {}, enabled);
            std::atomic<bool> request{false};
            lvt::ASRResult finalResult;
            std::exception_ptr finalError;
            double waitMs = 0, finalMs = 0;
            std::jthread final([&](std::stop_token stop) {
                while (!request && !stop.stop_requested()) std::this_thread::sleep_for(1ms);
                if (stop.stop_requested()) return;
                try {
                    const auto begin = Clock::now();
                    auto lease = arbiter.acquire(true, stop);
                    if (!lease) return;
                    const auto admitted = Clock::now();
                    finalResult = engine.recognize(recordings[next], stop);
                    waitMs = ms(begin, admitted); finalMs = ms(begin, Clock::now());
                } catch (...) { finalError = std::current_exception(); }
            });
            int checks = 0;
            bool syncTimedOut = false;
            const auto begin = Clock::now();
            auto preview = engine.recognize(previewPCM, {}, {}, [&] {
                if (++checks == checkpoint) {
                    request = true;
                    const auto deadline = Clock::now() + 3s;
                    while (arbiter.snapshot().finals == 0 || (enabled && !previewLease.shouldYield())) {
                        if (Clock::now() >= deadline) { syncTimedOut = true; return true; }
                        std::this_thread::sleep_for(1ms);
                    }
                }
                return previewLease.shouldYield();
            });
            const double previewMs = ms(begin, Clock::now());
            previewLease.reset();
            // Never strand the final if a backend did not reach the checkpoint.
            request = true; final.join();
            if (finalError) std::rethrow_exception(finalError);
            if (syncTimedOut || checks < checkpoint) throw std::runtime_error("Cancellation checkpoint not reached");
            if (enabled && !preview.text.empty()) throw std::runtime_error("Interrupted preview published text");
            auto recovered = engine.recognize(previewPCM);
            bool finalMatches = true, recoveryMatches = true, previewMatches = true;
            if (run == 0) {
                expectedFinal = finalResult; expectedRecovery = recovered; expectedPreview = preview;
            } else {
                finalMatches = finalResult.text == expectedFinal.text && finalResult.detectedLanguage == expectedFinal.detectedLanguage;
                recoveryMatches = recovered.text == expectedRecovery.text && recovered.detectedLanguage == expectedRecovery.detectedLanguage;
                previewMatches = enabled || (preview.text == expectedPreview.text && preview.detectedLanguage == expectedPreview.detectedLanguage);
                if (!finalMatches || !recoveryMatches || !previewMatches) {
                    std::cerr << nlohmann::json{{"error","Matched-sequence comparison failed"},{"preemption",enabled},
                        {"checkpoint",checkpoint},{"recording",corpus["recordings"][next]["filename"]},
                        {"expectedFinal",expectedFinal.text},{"actualFinal",finalResult.text},
                        {"expectedRecovery",expectedRecovery.text},{"actualRecovery",recovered.text},
                        {"expectedPreview",expectedPreview.text},{"actualPreview",preview.text},
                        {"reuseEncoder",config.reuseEncoder}}.dump() << std::endl;
                    ++mismatches;
                }
                matchedControls += !enabled && finalMatches && recoveryMatches && previewMatches;
            }
            ++cases; aborted += enabled;
            std::cout << nlohmann::json{{"recording",corpus["recordings"][i]["filename"]},
                {"sha256",corpus["recordings"][i]["sha256"]},{"backend",backend},{"preemption",enabled},{"run",run},
                {"checkpoint",checkpoint},{"previewMs",previewMs},{"finalWaitMs",waitMs},
                {"finalRequestToResultMs",finalMs},{"previewEmpty",preview.text.empty()},
                {"matchedFinal",finalMatches},{"matchedRecoveredPreview",recoveryMatches},{"matchedPreview",previewMatches},{"comparison",run ? "compared" : "reference"}}.dump() << std::endl;
        }
    }
    }
    std::cout << nlohmann::json{{"result",mismatches ? "FAIL" : "PASS"},{"mismatches",mismatches},{"cases",cases},{"abortedPreviews",aborted},
        {"backend",backend},{"matchedAAControls",matchedControls},{"scope","Controlled native contention, not live conversation or semantic acceptance"}}.dump() << std::endl;
    return mismatches ? 1 : 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
