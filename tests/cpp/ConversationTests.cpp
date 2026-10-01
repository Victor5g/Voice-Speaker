#include "../../engine/audio/PCMPlayback.hpp"
#include "../../engine/common/ModelAssets.hpp"
#include "../../engine/common/MemoryBudget.hpp"
#include "../../engine/speakers/SpeakerTracker.hpp"
#include "../../engine/language/TextUnits.hpp"
#include "../../engine/conversation/SegmentStabilityManager.hpp"
#include "../../engine/common/LatencyStatistics.hpp"
#include "../../engine/conversation/SpeakerScheduler.hpp"
#include "../../engine/audio/StreamingResampler.hpp"
#include "../../engine/audio/PlaybackReferenceHistory.hpp"
#include "../../engine/vad/VadSegmenter.hpp"
#include <iostream>
void require(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); std::cout << "PASS " << label << '\n'; }
int main() {
    try {
        lvt::PCMPlayback playback;
        playback.enable();
        lvt::SynthesizedAudio audio;
        audio.sampleRate = 16000; audio.pcm.assign(320, .5f); audio.translation.identity.session = 7;
        for (int i = 1; i <= 8; ++i) { audio.translation.identity.segment = i; if (!playback.enqueue(audio, 48000)) throw std::runtime_error("playback admission failed"); }
        require(!playback.enqueue(audio, 48000) && playback.dropped() == 1, "Congestion rejects a complete new utterance");
        std::vector<float> rendered(960 * 8);
        playback.render(rendered.data(), rendered.size(), 48000);
        require(rendered.front() == 0 && rendered.back() == 0 && rendered[480] > .39f && rendered[480] < .41f, "Worker resamples PCM and applies gain and fades");
        lvt::PlaybackEvent event;
        for (int i = 1; i <= 8; ++i) {
            if (!playback.popEvent(event) || !event.started || event.identity.segment != std::uint64_t(i)) throw std::runtime_error("Bad playback start order");
            const auto start = event.hostTime;
            if (!playback.popEvent(event) || !event.audible || event.started || event.identity.segment != std::uint64_t(i) || event.hostTime < start) throw std::runtime_error("Bad audio onset order");
            if (!playback.popEvent(event) || event.started || event.audible || event.identity.segment != std::uint64_t(i) || event.hostTime <= start) throw std::runtime_error("Bad playback end order");
        }
        require(!playback.popEvent(event), "Playback emits exactly one ordered start, audio onset and end per utterance");
        require(playback.enqueue(audio, 48000), "Worker reclaims retired audio storage");
        playback.invalidate();
        playback.render(rendered.data(), rendered.size(), 48000);
        require(std::all_of(rendered.begin(), rendered.end(), [](float v) { return v == 0; }), "Stop silences pending and active PCM immediately");
        playback.clearStopped();
        require(playback.depth() == 0 && !playback.popEvent(event), "Stop removes queued audio and playback events");
        lvt::ModelBundle unsafe{std::filesystem::current_path(), {}, "../outside.bin"};
        bool rejected = false;
        try { unsafe.path(unsafe.primary); } catch (...) { rejected = true; }
        require(rejected, "Model asset traversal is rejected");
        rejected = false; try { unsafe.verify(); } catch (...) { rejected = true; }
        require(rejected, "Unverified model groups are rejected");
        lvt::SpeakerTracker tracker;
        std::vector<float> identity(65, 0); identity[0] = 1;
        require(tracker.identify(identity, 0, true) == 0 && tracker.identify(identity, 1, true) == 1, "Two independent final embeddings establish session identity");
        identity[0] = 7;
        require(tracker.identify(identity, 16000ULL * 3600, true) == 1, "Returning normalized embedding preserves identity after inactivity");
        for (std::uint32_t i = 1; i < 64; ++i) {
            std::fill(identity.begin(), identity.end(), 0); identity[i] = 1;
            if (tracker.identify(identity, i * 16000, true) != 0 || tracker.identify(identity, i * 16000 + 1, true) != i + 1) throw std::runtime_error("Speaker capacity created unstable IDs");
        }
        std::fill(identity.begin(), identity.end(), 0); identity[64] = 1;
        require(tracker.identify(identity, 0, true) == 0 && tracker.count() == 64, "Session capacity is 64 identities with overflow unknown");
        identity[64] = 0; identity[0] = 1;
        tracker.observeSimultaneous(1, 2);
        require(tracker.identify(identity, 0, true, true) == 1 && tracker.count() == 64, "Overlap preserves established participant identities");
        require(tracker.identify(identity, 0, false) == 0, "Unreliable speech cannot create or update an identity");
        lvt::MemoryBudget budget(1000, [] { return 100; });
        auto reservation = budget.claim(600);
        rejected = false; try { auto excess = budget.claim(301); } catch (...) { rejected = true; }
        require(rejected && budget.snapshot().reserved == 600, "Heavy inference admission accounts for concurrent reservations");
        reservation.reset(); auto reuse = budget.claim(850);
        require(budget.snapshot().reserved == 850, "Finishing work releases its memory reservation");
        reuse.reset(); budget.setPressure(2);
        rejected = false; try { auto excess = budget.claim(1); } catch (...) { rejected = true; }
        require(rejected, "Critical pressure rejects new heavy inference");
        auto sentences = lvt::sentenceUnits("Dr. João pagou 12.50 reais. Não foi ontem! Foi hoje?");
        require(sentences.size() == 3 && sentences.front() == "Dr. João pagou 12.50 reais.", "Translation sentence boundaries preserve decimals and abbreviations");
        lvt::SegmentStabilityManager stability;
        require(stability.observe("Não autorizei o pagamento. Vou conferir", false).confirmed.empty(), "First ASR hypothesis is never spoken immediately");
        require(stability.observe("Não autorizei o pagamento. Vou conferir.", false).confirmed.empty(), "A partial clause waits for three consecutive agreements");
        require(stability.observe("Não autorizei o pagamento. Vou conferir.", false).confirmed == "Não autorizei o pagamento.", "Repeated complete prefix becomes a confirmed translation unit");
        require(stability.observe("Não autorizei o pagamento. Vou conferir.", true).confirmed == "Vou conferir.", "Final ASR emits only the unspoken suffix");
        require(stability.observe("Eu autorizei o pagamento.", true).conflict, "A revision cannot replace already confirmed speech");
        lvt::SegmentStabilityManager provisional;
        provisional.observe("Vamos começar a reunião. Eu preciso", false);
        provisional.observe("Vamos começar a reunião. Eu preciso", false);
        provisional.observe("Vamos começar a reunião. Eu preciso", false);
        auto regressed = provisional.observe("Vamos começar a reunir. Eu preciso de tempo.", false);
        require(regressed.confirmed.empty() && !regressed.conflict, "A regressing partial neither changes spoken text nor declares a final conflict");
        require(provisional.observe("Vamos começar a reunião. Eu preciso de tempo.", true).confirmed == "Eu preciso de tempo.", "A final hypothesis recovers after provisional regression without duplicate speech");
        auto committedOpening = [] {
            lvt::SegmentStabilityManager manager;
            for (int i = 0; i < 3; ++i) manager.observe("Vamos começar a reunião. Eu preciso de tempo.", false);
            return manager;
        };
        auto recoveredManager = committedOpening();
        recoveredManager.observe("Vamos começar a reunir. Eu preciso de tempo.", false);
        auto recovered = recoveredManager.observe("Vamos começar a reunir. Eu preciso de tempo.", true);
        require(recovered.conflict && recovered.recovered && recovered.confirmed == "Eu preciso de tempo.", "A final one-word revision preserves spoken audio and recovers an anchored continuation");
        require(recoveredManager.observe("Vamos começar a reunir. Eu preciso de tempo.", true).confirmed.empty(), "Recovered continuation is never spoken twice");
        auto rewritten = committedOpening().observe("Devemos encerrar esta reunião. Eu preciso de tempo.", true);
        require(rewritten.conflict && !rewritten.recovered && rewritten.confirmed.empty(), "An ambiguous rewritten prefix cannot silently skip or invent a continuation");
        auto unanchored = committedOpening().observe("Vamos começar a reunir. Você enviou documentos.", true);
        require(unanchored.conflict && !unanchored.recovered && unanchored.confirmed.empty(), "A changed continuation anchor requires an explicit unresolved conflict");
        lvt::SegmentStabilityManager truncated;
        truncated.observe("Vamos começar a reunir.", false);
        truncated.observe("Vamos começar a reunir. Eu preciso", false);
        require(truncated.observe("Vamos começar a reunir. Eu preciso de tempo.", false).confirmed.empty(), "A window-edge guess without following words does not count toward confirmation");
        auto corrected = truncated.observe("Vamos começar a reunião. Eu preciso de tempo.", true);
        require(!corrected.conflict && corrected.confirmed == "Vamos começar a reunião. Eu preciso de tempo.", "Final audio context corrects the unfinished word before any speech is committed");
        lvt::SegmentStabilityManager boundary;
        boundary.observe("Preciso de vinte e três", false);
        require(boundary.observe("Preciso de vinte e três ingressos.", false).confirmed.empty(), "Stable words wait for a complete clause or final silence");
        lvt::LatencyStatistics latencies;
        for (int i = 1; i <= 29; ++i) latencies.add(i);
        require(latencies.snapshot().p95 == 0 && latencies.snapshot().count == 29, "Percentiles stay unavailable before thirty samples");
        latencies.add(30);
        require(latencies.snapshot().p50 == 15 && latencies.snapshot().p95 == 29, "Measured latency percentiles use at least thirty samples");
        lvt::SpeakerScheduler scheduler;
        lvt::TranslationResult utterance;
        utterance.identity.speaker = 1; utterance.identity.captureHostTime = 20; utterance.identity.segment = 2;
        scheduler.push(utterance); utterance.identity.speaker = 2; utterance.identity.captureHostTime = 10; utterance.identity.segment = 1; scheduler.push(utterance);
        require(scheduler.waitPop(utterance, {}) && utterance.identity.segment == 1, "Speaker scheduler selects the earliest captured queue head");
        require(scheduler.waitPop(utterance, {}) && utterance.identity.segment == 2, "Speaker scheduler retains each participant's utterance");
        for (int i = 0; i < 4; ++i) scheduler.push(utterance);
        require(!scheduler.push(utterance), "A saturated participant leaves admission space for others");
        utterance.identity.speaker = 3;
        require(scheduler.push(utterance), "Another participant is admitted while one participant is saturated");
        lvt::StreamingResampler streaming;
        std::vector<float> source(44100), resampled;
        for (std::size_t i = 0; i < source.size(); ++i) source[i] = float(.5 * std::sin(i * 2 * 3.141592653589793 * 440 / 44100));
        std::uint64_t nextFrame = 0;
        for (std::size_t start = 0; start < source.size(); start += 997) {
            lvt::AudioBlock block; block.sampleRate = 44100; block.firstFrame = start; block.count = std::min<std::size_t>(997, source.size() - start);
            std::copy_n(source.begin() + start, block.count, block.samples.begin());
            streaming.process(block, [&](const auto& output) {
                if (output.firstFrame != nextFrame || output.sampleRate != 16000) throw std::runtime_error("Resampling timestamp discontinuity");
                nextFrame += output.count; resampled.insert(resampled.end(), output.samples.begin(), output.samples.begin() + output.count);
            });
        }
        lvt::SincResampler offline; auto expected = offline.to16k(source, 44100); double error = 0;
        for (std::size_t i = 64; i < resampled.size(); ++i) error += (expected[i] - resampled[i]) * (expected[i] - resampled[i]);
        require(resampled.size() > 15900 && std::sqrt(error / resampled.size()) < .001, "44.1 kHz capture resampling preserves continuity across tap boundaries");
        lvt::VadSegmenter clauseSegmenter(true);
        std::array<float, 320> frame{}; frame.fill(.1f);
        bool premature = false;
        for (int i = 0; i < 400; ++i) premature |= bool(clauseSegmenter.process(frame, true, 16000, i * 320, 0));
        require(!premature, "Streaming keeps a continuous eight-second clause intact");
        std::optional<lvt::SpeechSegment> clause;
        for (int i = 400; i < 415; ++i) clause = clauseSegmenter.process(frame, false, 16000, i * 320, 0);
        require(clause && clause->samples.size() == 415 * 320, "Streaming honors the configured default 300 ms pause for long clauses");
        lvt::VadSegmenter bounded(true); std::optional<lvt::SpeechSegment> boundedUnit;
        for (int i = 0; i < 490; ++i) boundedUnit = bounded.process(frame, true, 16000, i * 320, 0);
        require(boundedUnit && boundedUnit->samples.size() <= 160000, "Continuous speech remains within the separator memory contract");
        lvt::PlaybackReferenceHistory referenceHistory;
        std::vector<float> actualPCM(48000), reference8k(24000), residual(16000), other(16000);
        for (std::size_t i = 0; i < actualPCM.size(); ++i) actualPCM[i] = float(.2 * std::sin(i * .037 + i * i * .000001));
        for (std::size_t i = 0; i < reference8k.size(); ++i) reference8k[i] = (actualPCM[2 * i] + actualPCM[2 * i + 1]) / 2;
        for (std::size_t i = 0; i < residual.size(); ++i) { residual[i] = actualPCM[i + 8000] * .6f; other[i] = float(.1 * std::sin(i * .029 + i * i * .000003)); }
        require(!referenceHistory.matches16k(residual), "Separated audio is never discarded without actual playback PCM");
        referenceHistory.append8k(reference8k);
        require(referenceHistory.matches16k(residual), "Delayed attenuated separated playback is recognized by waveform evidence");
        require(!referenceHistory.matches16k(other), "Another voice is retained even while playback history exists");
        auto distorted = residual;
        for (std::size_t i = 0; i < distorted.size(); ++i) distorted[i] += .6f * other[i];
        require(!referenceHistory.matches16k(distorted), "Mixed microphone audio does not use the separated-source echo tolerance");
        require(referenceHistory.matches16k(distorted, {}, true), "A separated playback source tolerates residual separation distortion");
        std::copy_n(other.begin(), 6000, residual.begin());
        require(!referenceHistory.matches16k(residual), "A unit containing unmatched near-end speech is never removed wholesale");
        referenceHistory.clear();
        require(!referenceHistory.matches16k(std::span<const float>(actualPCM).subspan(8000, 16000)), "Stop erases the longer separated-echo reference");
        std::cout << "RESULT 48 passed, 0 failed\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
