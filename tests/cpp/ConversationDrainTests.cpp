#include "../../engine/conversation/ConversationPipeline.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace {
template<class Predicate> void await(Predicate predicate, const char* error) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(error);
        std::this_thread::sleep_for(1ms);
    }
}
struct Gates {
    std::atomic<unsigned> popped{0}, permit{0};
    bool input(lvt::ASRResult& result) {
        const auto index = popped.load();
        if (index >= 2) return false;
        result = {};
        result.utterance = 7; result.final = true;
        if (!index) {
            result.text = "Uncertain fixture";
            result.detectedLanguage = "pt";
            result.languageStatus = "insufficient-evidence";
            // An empty target produces an uncertain display event without
            // translation/inference, regardless of model initialization state.
        } else result.languageStatus = "utterance-end";
        popped = index + 1; // Source item has left the scripted input queue.
        while (permit.load() <= index) std::this_thread::sleep_for(1ms);
        return true;
    }
};
struct ReleaseOnExit { Gates& gates; ~ReleaseOnExit() { gates.permit = 2; } };
}

// Exercises the actual coordinator, output queue and snapshot. Models deliberately
// cannot initialize: no native inference is needed to reproduce input-pop races.
int main() { try {
    const std::string missing = "build/coordinator-drain-intentionally-absent-model-root";
    if (std::filesystem::exists(missing)) throw std::runtime_error("Test requires its model root to be absent");
    unsigned passed = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
        ++passed;
    };
    for (bool consumeBeforeMarker : {false, true}) {
        Gates gates;
        lvt::PCMPlayback playback;
        lvt::ConversationPipeline pipeline(playback);
        ReleaseOnExit release{gates}; // Release a blocked input before pipeline destruction on failure.
        pipeline.start(missing, 48000, 1, [&](lvt::ASRResult& result) { return gates.input(result); });
        await([&] { return gates.popped == 1; }, "Coordinator never popped scripted result");
        auto state = pipeline.snapshot();
        check(state.coordinatedUtterances == 0 && state.pendingEvents == 0,
            "Popped result with no output must remain unacknowledged");
        // Hold substantially beyond the former 500 ms drain quiet interval.
        std::this_thread::sleep_for(550ms);
        state = pipeline.snapshot();
        check(state.coordinatedUtterances == 0, "Elapsed quiet cannot acknowledge a popped result");
        gates.permit = 1;
        await([&] { return gates.popped == 2; }, "Coordinator never popped end marker");
        state = pipeline.snapshot();
        check(state.coordinatedUtterances == 0 && state.pendingEvents == 1,
            "Result publication alone must not acknowledge a marker still held by input");
        lvt::ConversationEvent event;
        if (consumeBeforeMarker) {
            check(pipeline.pop(event) && event.state == "uncertain", "Expected real coordinator display event");
            state = pipeline.snapshot();
            check(state.pendingEvents == 0 && state.coordinatedUtterances == 0,
                "Empty output after consumption still cannot acknowledge unprocessed marker");
        }
        gates.permit = 2;
        await([&] { return pipeline.snapshot().coordinatedUtterances == 1; }, "End marker was not acknowledged");
        state = pipeline.snapshot();
        check(state.pendingEvents == (consumeBeforeMarker ? 0U : 1U),
            "Acknowledgment must not hide a preceding event pending consumption");
        if (!consumeBeforeMarker)
            check(pipeline.pop(event) && event.state == "uncertain", "Pending display event was lost");
        state = pipeline.snapshot();
        check(state.coordinatedUtterances == 1 && state.pendingEvents == 0,
            "Processed end marker and consumed events establish coordinator completion");
        pipeline.stop();
        state = pipeline.snapshot();
        check(state.coordinatedUtterances == 0 && state.pendingEvents == 0,
            "Stop must reset coordinator acknowledgment and output queue");
    }
    {
        Gates gates; gates.permit = 2;
        lvt::PCMPlayback playback;
        lvt::ConversationPipeline pipeline(playback);
        ReleaseOnExit release{gates};
        pipeline.start(missing, 48000, 19, [&](lvt::ASRResult& result) { return gates.input(result); });
        await([&] { return pipeline.snapshot().pendingEvents == 1; }, "Expected queued event before interruption");
        pipeline.requestStop();
        lvt::ConversationEvent event;
        check(!pipeline.pop(event), "RequestStop invalidates already queued events before worker joins");
        check(event.result.original.empty() && !event.result.identity.session,
            "Discarding stale events does not expose their content to the caller");
        pipeline.stop();
        check(!pipeline.snapshot().pendingEvents, "Interrupted session has no residual events after teardown");
    }
    std::cout << "RESULT " << passed << " passed, 0 failed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
} }
