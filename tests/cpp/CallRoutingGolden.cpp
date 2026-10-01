#include "../../engine/audio/AudioCore.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include "TimelineJSON.hpp"
#include "StreamingRunConfig.hpp"
#include <set>

// Feed the same bilingual fixture to both endpoints deliberately: each production
// pipeline must reject the opposite direction while producing its own PCM.
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("Usage: call-routing-golden MODEL_ROOT [1800|RUN_CONFIG_JSON]");
        nlohmann::json run = nlohmann::json::object();
        if (argc == 3 && std::string(argv[2]) != "1800") std::ifstream(argv[2]) >> run;
        const auto streaming = lvt::test::streamingPolicy(run);
        const int repeatSeconds = argc == 3 && std::string(argv[2]) == "1800" ? 1800 : run.value("repeatSeconds", 0);
        if (repeatSeconds != 0 && repeatSeconds != 1800) throw std::runtime_error("Continuous acceptance requires 1800 seconds");
        const std::filesystem::path root(argv[1]);
        nlohmann::json selection; std::ifstream(root / "selection.json") >> selection;
        lvt::AudioCore outgoing, incoming;
        lvt::AudioCore* cores[]{&outgoing, &incoming};
        const std::string languages[]{"pt", "en"};
        for (int i = 0; i < 2; ++i) {
            cores[i]->configureStreaming(streaming);
            cores[i]->configureASR((root / selection.at("filename").get<std::string>()).string(), selection.at("sha256"));
            cores[i]->configureASRBackend(run.value("asrBackend", ""));
            cores[i]->configureConversation(root.string(), i ? 44100 : 48000);
            cores[i]->setSourceLanguage(languages[i]); cores[i]->start();
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        bool ready = false;
        while (std::chrono::steady_clock::now() < deadline) {
            ready = true;
            for (auto* core : cores) {
                auto s = core->snapshot();
                if (s.asr.state == 3 || s.conversation.translationState == 3 || s.conversation.ttsState == 3)
                    throw std::runtime_error("Duplex initialization failed: " + s.asr.error + " " + s.conversation.error);
                ready &= s.asr.state == 2 && s.conversation.translationState == 2 && s.conversation.ttsState == 2;
            }
            if (ready) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!ready) throw std::runtime_error("Duplex initialization timed out");
        auto input = lvt::readWav16k("tests/data/conversation_ptbr_en.wav");
        input.resize(input.size() + 16000);
        std::array<float, 320> microphone{};
        std::vector<float> outputs[]{std::vector<float>(960), std::vector<float>(882)};
        std::set<std::uint64_t> translated[2], played[2], sessions;
        std::string originals[2];
        std::map<std::uint64_t, std::string> cycleOriginals[2];
        std::size_t cycles = 1;
        const auto framesPerCycle = ((input.size() + 319) / 320) * 320;
        std::vector<std::pair<double, std::size_t>> memorySamples;
        std::size_t filtered[2]{}, callbacksDuringPlayback[2]{}, peak = 0;
        bool active[2]{};
        double energy[2]{};
        auto next = std::chrono::steady_clock::now();
        const auto started = next;
        deadline = next + std::chrono::seconds(repeatSeconds + 120);
        const auto captureUntil = next + std::chrono::seconds(repeatSeconds);
        auto nextMetric = next;
        double maxCallbackLatenessMs = 0;
        std::size_t cursor = 0;
        bool drained = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto now = std::chrono::steady_clock::now();
            maxCallbackLatenessMs = std::max(maxCallbackLatenessMs, std::chrono::duration<double, std::milli>(now-next).count());
            if (repeatSeconds && maxCallbackLatenessMs > 1000) throw std::runtime_error("Host scheduling paused; continuous duplex test invalid");
            if (cursor == input.size() && repeatSeconds && now < captureUntil) { cursor = 0; ++cycles; }
            microphone.fill(0);
            auto count = std::min(microphone.size(), input.size() - cursor);
            std::copy_n(input.data() + cursor, count, microphone.data()); cursor += count;
            for (int i = 0; i < 2; ++i) {
                const auto host = mach_absolute_time();
                cores[i]->capture(microphone.data(), microphone.size(), 16000, host);
                cores[i]->render(outputs[i].data(), outputs[i].size(), i ? 44100 : 48000, host);
                if (active[i]) ++callbacksDuringPlayback[i];
                for (float sample : outputs[i]) energy[i] += sample * sample;
                lvt::ConversationEvent event;
                while (cores[i]->popConversationEvent(event)) {
                    const auto& r = event.result;
                    std::cout << nlohmann::json{{"event",event.state},{"state",event.state},{"route",i},{"session",r.identity.session},{"language",r.sourceLanguage},{"segment",r.identity.segment},{"sourceSegment",r.identity.sourceSegment},{"timing",lvt::test::timelineJSON(event)}}.dump() << std::endl;
                    if (event.state == "error" || event.state == "unavailable" || event.state == "dropped" || event.state == "revision-conflict" || event.state == "revision-preserved")
                        throw std::runtime_error("Duplex rejected segment: " + event.error);
                    if (event.state == "direction-filtered") ++filtered[i];
                    if (event.state == "translated") {
                        if (r.sourceLanguage != languages[i] || r.targetLanguage != (i ? "pt-BR" : "en") || r.translated.empty())
                            throw std::runtime_error("Translated speech crossed the wrong route");
                        translated[i].insert(r.identity.segment); sessions.insert(r.identity.session);
                        originals[i] += " " + r.original;
                        cycleOriginals[i][(r.identity.firstFrame + 3200) / framesPerCycle] += " " + r.original;
                    }
                    if (event.state == "playing") active[i] = true;
                    if (event.state == "played") {
                        active[i] = false;
                        if (!played[i].insert(r.identity.segment).second) throw std::runtime_error("Duplicate duplex playback");
                    }
                }
                auto s = cores[i]->snapshot();
                if (s.dropped || s.vad.dropped || s.asr.resultsQueue.droppedItems || s.playbackDropped || s.conversation.eventDrops)
                    throw std::runtime_error("Duplex queues lost data");
            }
            peak = std::max(peak, lvt::MemoryBudget::physicalFootprint());
            if (peak > (8ULL << 30)) throw std::runtime_error("Duplex exceeded memory budget");
            if (now >= nextMetric) {
                const auto seconds = std::chrono::duration<double>(now-started).count();
                memorySamples.emplace_back(seconds, lvt::MemoryBudget::physicalFootprint());
                std::cout << nlohmann::json{{"metric",true},{"elapsedSeconds",seconds},{"footprintBytes",memorySamples.back().second},
                    {"outgoingPlayed",played[0].size()},{"incomingPlayed",played[1].size()}}.dump() << std::endl;
                nextMetric = now + std::chrono::seconds(10);
            }
            drained = cursor == input.size() && (!repeatSeconds || now >= captureUntil);
            for (int i = 0; i < 2; ++i) {
                auto s = cores[i]->snapshot();
                drained &= s.asr.processed == s.vad.segments && !translated[i].empty() && translated[i] == played[i] && filtered[i] > 0;
            }
            if (drained) break;
            next += std::chrono::milliseconds(20); std::this_thread::sleep_until(next);
        }
        if (!drained || sessions.size() != 2) throw std::runtime_error("Both isolated duplex sessions did not drain");
        const std::vector<std::string> terms[]{{"reunião", "documentos", "projeto"}, {"meeting", "documents", "project"}};
        for (int i = 0; i < 2; ++i) {
            for (const auto& term : terms[i]) if (originals[i].find(term) == std::string::npos) throw std::runtime_error("Duplex omitted source clause: " + term);
            if (energy[i] < .01 || !callbacksDuringPlayback[i]) throw std::runtime_error("Missing duplex PCM or continuous capture");
        }
        double baselineMemory = 0, finalMemory = 0;
        if (repeatSeconds) {
            std::size_t baselineCount = 0, finalCount = 0;
            for (const auto& [seconds, bytes] : memorySamples) {
                if (seconds >= 300 && seconds < 600) { baselineMemory += bytes; ++baselineCount; }
                if (seconds >= 1500) { finalMemory += bytes; ++finalCount; }
            }
            if (!baselineCount || !finalCount) throw std::runtime_error("Missing continuous duplex memory samples");
            baselineMemory /= baselineCount; finalMemory /= finalCount;
            if (finalMemory > baselineMemory + std::max(256.0 * 1024 * 1024, baselineMemory * .2)) throw std::runtime_error("Duplex memory grew progressively");
            for (int i = 0; i < 2; ++i) {
                if (cores[i]->snapshot().conversation.firstAudio.count < 30 || cycleOriginals[i].size() != cycles)
                    throw std::runtime_error("Missing duplex cycles or latency samples");
                for (std::size_t cycle = 0; cycle < cycles; ++cycle)
                    for (const auto& term : terms[i]) if (cycleOriginals[i][cycle].find(term) == std::string::npos)
                        throw std::runtime_error("Duplex cycle lost a source clause: " + std::to_string(cycle) + " " + languages[i]);
            }
        }
        const auto outgoingMetrics = outgoing.snapshot().conversation, incomingMetrics = incoming.snapshot().conversation;
        if (outgoingMetrics.revisionConflicts || incomingMetrics.revisionConflicts)
            throw std::runtime_error("Duplex revised confirmed text");
        for (auto* core : cores) core->requestStop();
        for (int i = 0; i < 2; ++i) {
            cores[i]->render(outputs[i].data(), outputs[i].size(), i ? 44100 : 48000);
            if (std::any_of(outputs[i].begin(), outputs[i].end(), [](float v) { return v != 0; })) throw std::runtime_error("Duplex STOP leaked PCM");
            cores[i]->stop(); lvt::ConversationEvent late;
            if (cores[i]->popConversationEvent(late)) throw std::runtime_error("Duplex STOP leaked event");
        }
        std::cout << nlohmann::json{{"result","PASS"},{"fixtureType","synthetic WAV; two production cores; no virtual driver or meeting app"},
            {"outgoingPlayed",played[0].size()},{"incomingPlayed",played[1].size()},
            {"outgoingRevisionConflicts",outgoingMetrics.revisionConflicts},{"incomingRevisionConflicts",incomingMetrics.revisionConflicts},
            {"cycles",cycles},{"baselineFootprintBytes",baselineMemory},{"finalFootprintBytes",finalMemory},{"maxCallbackLatenessMs",maxCallbackLatenessMs},
            {"outgoingFirstAudioSamples",outgoingMetrics.firstAudio.count},{"incomingFirstAudioSamples",incomingMetrics.firstAudio.count},
            {"outgoingFirstAudioP50Ms",outgoingMetrics.firstAudio.count >= 30 ? nlohmann::json(outgoingMetrics.firstAudio.p50) : nlohmann::json(nullptr)},
            {"outgoingFirstAudioP95Ms",outgoingMetrics.firstAudio.count >= 30 ? nlohmann::json(outgoingMetrics.firstAudio.p95) : nlohmann::json(nullptr)},
            {"incomingFirstAudioP50Ms",incomingMetrics.firstAudio.count >= 30 ? nlohmann::json(incomingMetrics.firstAudio.p50) : nlohmann::json(nullptr)},
            {"incomingFirstAudioP95Ms",incomingMetrics.firstAudio.count >= 30 ? nlohmann::json(incomingMetrics.firstAudio.p95) : nlohmann::json(nullptr)},
            {"wrongDirectionFiltered",{filtered[0],filtered[1]}},{"captureDuringPlayback",{callbacksDuringPlayback[0],callbacksDuringPlayback[1]}},
            {"peakFootprintBytes",peak},{"durationSeconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()}}.dump() << std::endl;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
