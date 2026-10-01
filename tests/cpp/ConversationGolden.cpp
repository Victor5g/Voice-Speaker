#include "../../engine/audio/AudioCore.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <sherpa-onnx/c-api/c-api.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include "TimelineJSON.hpp"
#include "StreamingRunConfig.hpp"
#include "CorpusDrainTracker.hpp"
#include <set>
#include <sys/resource.h>
#include <mach/mach.h>
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("Usage: conversation-golden MODEL_ROOT [RUN_CONFIG_JSON]");
        const std::filesystem::path root(argv[1]);
        nlohmann::json run = nlohmann::json::object();
        if (argc == 3) std::ifstream(argv[2]) >> run;
        nlohmann::json selection;
        std::ifstream(root / "selection.json") >> selection;
        if (run.contains("asr")) selection = run["asr"];
        lvt::AudioCore core;
        const auto streaming = lvt::test::streamingPolicy(run);
        core.configureStreaming(streaming);
        const auto processing = lvt::ProcessingOptions::fromJSON(run.value("processing", nlohmann::json::object()).dump());
        core.configureProcessing(processing);
        core.configureASR((root / selection.at("filename").get<std::string>()).string(), selection.at("sha256"));
        core.configureASRBackend(run.value("asrBackend", ""));
        core.configureConversation(root.string(), 48000); core.start();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while ((core.snapshot().asr.state == 1 || core.snapshot().conversation.translationState == 1 || core.snapshot().conversation.ttsState == 1) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto initial = core.snapshot();
        if (initial.asr.state != 2 || initial.conversation.translationState != 2 ||
            initial.conversation.ttsState != (processing.speechOutput ? 2 : 0))
            throw std::runtime_error("Production conversation models failed to initialize");
        auto input = lvt::readWav16k(run.value("fixture", "tests/data/conversation_ptbr_en.wav"));
        input.resize(input.size() + 16000, 0);
        std::vector<float> output, block(960), silence(320, 0);
        std::deque<std::vector<float>> echoDelay(3, std::vector<float>(960, 0));
        std::vector<float> microphone48k(960);
        const bool injectEcho = run.value("injectEcho", false);
        auto lastPlayed = std::chrono::steady_clock::now();
        std::set<std::string> languages;
        std::map<std::string, std::string> originals;
        std::map<std::uint64_t, std::map<std::string, std::string>> cycleOriginals;
        std::size_t preservedRevisions = 0;
        std::set<std::uint64_t> played, translated;
        lvt::test::CorpusDrainTracker drain;
        std::map<std::uint64_t, std::uint64_t> captured;
        std::size_t cursor = 0;
        auto next = std::chrono::steady_clock::now();
        const auto started = next;
        const int repeatSeconds = run.value("repeatSeconds", 0);
        const bool expectNoSpeech = run.value("expectNoSpeech", false);
        const std::size_t minimumPlayed = run.value("minimumPlayed", expectNoSpeech ? 0 : 2);
        const auto captureUntil = started + std::chrono::seconds(repeatSeconds);
        deadline = next + std::chrono::seconds(repeatSeconds + 120 + (input.size()+15999)/16000);
        auto nextMetric = next;
        std::vector<std::pair<double, double>> memorySamples;
        std::size_t cycles = 0;
        double maxCallbackLatenessMs = 0;
        mach_timebase_info_data_t tb{}; mach_timebase_info(&tb);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto now = std::chrono::steady_clock::now();
            maxCallbackLatenessMs = std::max(maxCallbackLatenessMs, std::chrono::duration<double, std::milli>(now - next).count());
            if (repeatSeconds >= 1800 && maxCallbackLatenessMs > 1000)
                throw std::runtime_error("Host callback scheduling paused for over one second; the continuous test is invalid (check sleep/load)");
            if (cursor == input.size() && repeatSeconds && now < captureUntil) { cursor = 0; ++cycles; }
            const auto count = std::min<std::size_t>(320, input.size() - cursor);
            const auto host = mach_absolute_time();
            if (injectEcho) {
                for (std::size_t i = 0; i < 960; ++i) microphone48k[i] = (i / 3 < count ? input[cursor + i / 3] : 0) + .6f * echoDelay.front()[i];
                core.capture(microphone48k.data(), microphone48k.size(), 48000, host); cursor += count;
                echoDelay.pop_front();
            } else if (count) { core.capture(input.data() + cursor, count, 16000, host); cursor += count; }
            else core.capture(silence.data(), silence.size(), 16000, host);
            core.render(block.data(), block.size(), 48000, host);
            if (injectEcho) echoDelay.push_back(block);
            if (!repeatSeconds) output.insert(output.end(), block.begin(), block.end());
            lvt::ConversationEvent event;
            while (core.popConversationEvent(event)) {
                const auto& result = event.result;
                drain.observe(result.identity.segment, event.state);
                if (event.state == "error" || event.state == "dropped" || event.state == "revision-conflict" || event.state == "unavailable") throw std::runtime_error("Pipeline rejected golden segment: " + event.error);
                if (event.state == "translated" && result.original.find("Vamos começar a reunião") != std::string::npos &&
                    result.translated.find("meeting") == std::string::npos) throw std::runtime_error("Translation omitted the opening meeting sentence");
                if (event.state == "revision-preserved") ++preservedRevisions;
                if (event.state == "queued" && !result.identity.speaker && result.identity.timing.endpointReason == lvt::EndpointReason::none)
                    throw std::runtime_error("Provisional speech used an unidentified participant voice");
                if (event.state == "translated") {
                    languages.insert(result.sourceLanguage); translated.insert(result.identity.segment); originals[result.sourceLanguage] += " " + result.original;
                    // VAD may validly split one fixture sentence into several source
                    // IDs. Group by its captured cycle, allowing the 150 ms preroll.
                    const auto capturedCycle = (result.identity.firstFrame + 3200) / input.size();
                    cycleOriginals[capturedCycle][result.sourceLanguage] += " " + result.original;
                }
                if (result.identity.captureHostTime) captured[result.identity.segment] = result.identity.captureHostTime;
                if (event.state == "played") {
                    lastPlayed = now;
                    if (!played.insert(result.identity.segment).second) throw std::runtime_error("Duplicate playback");
                }
                nlohmann::json record{{"session",result.identity.session},{"segment",result.identity.segment},{"sourceSegment",result.identity.sourceSegment},
                    {"firstFrame",result.identity.firstFrame},
                    {"speaker",result.identity.speaker},{"revision",result.identity.revision},{"state",event.state},
                    {"original",result.original},{"translation",result.translated},{"language",result.sourceLanguage},{"voice",event.voice}};
                if (event.playbackHostTime && captured.contains(result.identity.segment))
                    record["captureToPlaybackMs"] = (event.playbackHostTime - captured[result.identity.segment]) * double(tb.numer) / tb.denom / 1e6;
                record["timing"] = lvt::test::timelineJSON(event);
                std::cout << record.dump() << std::endl;
            }
            auto s = core.snapshot();
            if (now >= nextMetric) {
                task_vm_info_data_t vm{}; mach_msg_type_number_t size = TASK_VM_INFO_COUNT;
                if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &size) != KERN_SUCCESS)
                    throw std::runtime_error("Cannot measure process memory");
                const double elapsed = std::chrono::duration<double>(now - started).count();
                memorySamples.emplace_back(elapsed, double(vm.phys_footprint));
                if (vm.phys_footprint > 8ULL * 1024 * 1024 * 1024) throw std::runtime_error("Memory budget exceeded");
                rusage cpu{}; getrusage(RUSAGE_SELF, &cpu);
                std::cout << nlohmann::json{{"metric",true},{"elapsedSeconds",elapsed},{"footprintBytes",vm.phys_footprint},
                    {"cpuSeconds",cpu.ru_utime.tv_sec + cpu.ru_utime.tv_usec / 1e6 + cpu.ru_stime.tv_sec + cpu.ru_stime.tv_usec / 1e6},
                    {"captureQueue",s.depth},{"asrQueue",s.asr.resultsQueue.currentDepth},{"translationQueue",s.conversation.translationQueue.currentDepth},
                    {"ttsQueue",s.conversation.ttsQueue.currentDepth},{"playbackQueue",s.playbackDepth},{"speakers",s.asr.speakers},
                    {"partialIntervalMs",s.vad.partialIntervalMs},{"estimatedDemandMs",s.asr.estimatedDemandMs}}.dump() << std::endl;
                nextMetric = now + std::chrono::seconds(10);
            }
            if (cursor == input.size() && (!repeatSeconds || now >= captureUntil) && (!injectEcho || now - lastPlayed > std::chrono::seconds(5)) &&
                s.asr.processed == s.vad.segments && s.conversation.coordinatedUtterances == s.asr.processed &&
                !s.conversation.pendingEvents && drain.idle() && !s.depth && !s.asr.resultsQueue.currentDepth &&
                !s.conversation.translationQueue.currentDepth && !s.conversation.ttsQueue.currentDepth &&
                !s.playbackDepth && played.size() >= minimumPlayed && played == translated) break;
            next += std::chrono::milliseconds(20); std::this_thread::sleep_until(next);
        }
        auto snapshot = core.snapshot();
        std::cout << nlohmann::json{{"validationSnapshot",true},{"captureDropped",snapshot.dropped},{"vadDropped",snapshot.vad.dropped},
            {"vadErrors",snapshot.vad.errors},{"playbackDropped",snapshot.playbackDropped},{"eventDrops",snapshot.conversation.eventDrops},
            {"processedSegments",snapshot.asr.processed},{"vadSegments",snapshot.vad.segments},{"maxCallbackLatenessMs",maxCallbackLatenessMs}}.dump() << std::endl;
        std::cout << nlohmann::json{{"stabilityValidation",true},{"revisionConflicts",snapshot.conversation.revisionConflicts},
            {"recoveredContinuations",snapshot.conversation.recoveredContinuations},{"preservedRevisionEvents",preservedRevisions}}.dump() << std::endl;
        // Recovering a suffix prevents losing more speech, but does not repair an
        // already committed wrong word. It must never turn a quality failure into PASS.
        if (snapshot.conversation.revisionConflicts || preservedRevisions)
            throw std::runtime_error("ASR revised confirmed text; recovered continuation does not establish stable speech");
        const auto expectedLanguages = run.value("expectedLanguages", expectNoSpeech ? std::vector<std::string>{} : std::vector<std::string>{"pt","en"});
        if (std::any_of(expectedLanguages.begin(), expectedLanguages.end(), [&](const auto& language) { return !languages.contains(language); }) || translated.size() < minimumPlayed || played != translated ||
            snapshot.dropped || snapshot.vad.dropped || snapshot.vad.errors || snapshot.playbackDropped || snapshot.conversation.eventDrops)
            throw std::runtime_error("Incomplete or congested production conversation");
        if (expectNoSpeech && !translated.empty()) throw std::runtime_error("Nonspeech fixture produced translated speech");
        if (run.value("requireNoVadSegments", false) && snapshot.vad.segments) throw std::runtime_error("Silence produced a VAD segment");
        if (injectEcho && (languages != std::set<std::string>(expectedLanguages.begin(), expectedLanguages.end()) || !snapshot.selfPlaybackBlocks)) throw std::runtime_error("Digital playback reference did not prevent recursive translation");
        auto requiredTerms = run.value("requiredOriginalTerms", nlohmann::json::object());
        if (!run.contains("fixture")) requiredTerms = {{"pt",{"reunião", "documentos", "conversar", "projeto"}}, {"en",{"meeting", "documents", "project"}}};
        for (auto it = requiredTerms.begin(); it != requiredTerms.end(); ++it)
            for (const auto& term : it.value()) if (originals[it.key()].find(term.get<std::string>()) == std::string::npos)
                throw std::runtime_error("A required source clause was omitted before translation");
        // Repetition must not hide a lost clause in one cycle behind an earlier success.
        if (repeatSeconds && !run.contains("fixture")) {
            if (cycleOriginals.size() != cycles + 1) throw std::runtime_error("A repeated capture cycle was lost");
            for (std::uint64_t cycle = 0; cycle <= cycles; ++cycle)
                for (auto it = requiredTerms.begin(); it != requiredTerms.end(); ++it)
                    for (const auto& term : it.value()) if (cycleOriginals[cycle][it.key()].find(term.get<std::string>()) == std::string::npos)
                        throw std::runtime_error("A repeated capture cycle lost a required clause: " + std::to_string(cycle));
        }
        if (snapshot.asr.speakers < run.value("minimumSpeakers", std::size_t(0)) ||
            snapshot.asr.diarization.overlapWindows < run.value("minimumOverlapWindows", std::uint64_t(0)) ||
            snapshot.asr.diarization.separationCalls < run.value("minimumSeparationCalls", std::uint64_t(0))) throw std::runtime_error("Speaker or overlap scenario expectations failed");
        const auto waveFile = run.value("output", "artifacts/conversation-translated.wav");
        if (!repeatSeconds && !SherpaOnnxWriteWave(output.data(), output.size(), 48000, waveFile.c_str())) throw std::runtime_error("Cannot write golden output");
        double baseline = 0, finalMemory = 0; std::size_t baselineCount = 0, finalCount = 0;
        if (repeatSeconds >= 1800) {
            for (const auto& [seconds, bytes] : memorySamples) {
                if (seconds >= 300 && seconds < 600) { baseline += bytes; ++baselineCount; }
                if (seconds >= repeatSeconds - 300) { finalMemory += bytes; ++finalCount; }
            }
            if (!baselineCount || !finalCount) throw std::runtime_error("Insufficient memory samples");
            baseline /= baselineCount; finalMemory /= finalCount;
            if (finalMemory > baseline + std::max(256.0 * 1024 * 1024, baseline * .2)) throw std::runtime_error("Progressive memory growth detected");
        }
        core.requestStop();
        core.render(block.data(), block.size(), 48000);
        if (std::any_of(block.begin(), block.end(), [](float x) { return x != 0; })) throw std::runtime_error("Stop did not silence output");
        core.stop();
        lvt::ConversationEvent late;
        if (core.popConversationEvent(late)) throw std::runtime_error("Late event escaped Stop");
        rusage usage{}; getrusage(RUSAGE_SELF, &usage);
        std::cout << nlohmann::json{{"result","PASS"},{"translatedSegments",translated.size()},{"playedSegments",played.size()},
            {"streaming",lvt::test::streamingJSON(streaming)},
            {"partialPublished",snapshot.vad.partialPublished},{"partialReplaced",snapshot.vad.partialReplaced},
            {"obsoletePending",snapshot.vad.obsoletePending},{"dequeuedJobs",snapshot.asr.dequeuedJobs},
            {"obsoleteJobs",snapshot.asr.obsoleteJobs},{"emptyUnits",snapshot.asr.emptyUnits},
            {"peakRSSBytes",usage.ru_maxrss},{"captureCallbacks",snapshot.captureCallbacks},{"playbackCallbacks",snapshot.playbackCallbacks},
            {"firstAudioSamples",snapshot.conversation.firstAudio.count},{"lastFirstAudioMs",snapshot.conversation.firstAudio.last},
            {"ttsWarmupMs",snapshot.conversation.ttsWarmupMs},{"ttsCacheHits",snapshot.conversation.voiceCacheHits},
            {"ttsCacheMisses",snapshot.conversation.voiceCacheMisses},{"ttsCachedVoices",snapshot.conversation.cachedVoices},
            {"firstAudioP50Ms",snapshot.conversation.firstAudio.count >= 30 ? nlohmann::json(snapshot.conversation.firstAudio.p50) : nlohmann::json(nullptr)},
            {"firstAudioP95Ms",snapshot.conversation.firstAudio.count >= 30 ? nlohmann::json(snapshot.conversation.firstAudio.p95) : nlohmann::json(nullptr)},
            {"e2eP50Ms",snapshot.conversation.endToEnd.count >= 30 ? nlohmann::json(snapshot.conversation.endToEnd.p50) : nlohmann::json(nullptr)},
            {"e2eP95Ms",snapshot.conversation.endToEnd.count >= 30 ? nlohmann::json(snapshot.conversation.endToEnd.p95) : nlohmann::json(nullptr)},
            {"overlapWindows",snapshot.asr.diarization.overlapWindows},{"separationCalls",snapshot.asr.diarization.separationCalls},
            {"selfPlaybackBlocks",snapshot.selfPlaybackBlocks},
            {"playbackUnitsSuppressed",snapshot.asr.diarization.playbackUnitsSuppressed},
            {"preservedRevisions",preservedRevisions},
            {"vadSegments",snapshot.vad.segments},
            {"durationSeconds",std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()},
            {"cycles",cycles + 1},{"baselineFootprintBytes",baseline},{"finalFootprintBytes",finalMemory},
            {"maxCallbackLatenessMs",maxCallbackLatenessMs},
            {"asr",selection.at("filename")},{"asrBackend",snapshot.asr.backend},{"speakers",snapshot.asr.speakers},
            {"fixtureType",run.value("fixtureType", "synthetic developer WAV; hardware and human quality are separate gates")}}.dump() << std::endl;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
