#include "../../engine/audio/AudioCore.hpp"
#include "../../engine/vad/VadSegmenter.hpp"
#include "../../engine/audio/SincResampler.hpp"
#include "../../engine/common/ModelHash.hpp"
#include "../../engine/language/LanguageStabilizer.hpp"
#include "webrtc/common_audio/vad/include/webrtc_vad.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

static int passed = 0;
static void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    ++passed; std::cout << "PASS " << name << '\n';
}
int main() {
    try {
        lvt::RingBuffer<int, 4> queue;
        int value = -1;
        check(!queue.pop(value), "empty queue does not read invalid memory");
        for (int i = 0; i < 4; ++i) queue.push(i);
        check(!queue.push(9) && queue.dropped() == 1 && queue.depth() == 4, "bounded queue drops newest on overflow");
        bool ordered = true;
        for (int i = 0; i < 4; ++i) { ordered &= queue.pop(value) && value == i; }
        check(ordered, "FIFO order preserved");
        bool wrap = true;
        for (int i = 0; i < 10000; ++i) wrap &= queue.push(i) && queue.pop(value) && value == i;
        check(wrap, "ring wraparound");
        queue.push(42); queue.clearStopped();
        check(queue.depth() == 0 && queue.dropped() == 0 && queue.highWater() == 0 && !queue.pop(value), "stop resets queue and counters");
        lvt::RingBuffer<int, 128> concurrent;
        constexpr int count = 100000;
        std::jthread producer([&] { for (int i = 0; i < count; ++i) while (!concurrent.push(i)) std::this_thread::yield(); });
        bool concurrentOrder = true;
        for (int i = 0; i < count; ++i) { while (!concurrent.pop(value)) std::this_thread::yield(); concurrentOrder &= value == i; }
        producer.join();
        check(concurrentOrder, "100000 transfers across real producer and consumer threads");
        lvt::AudioCore core;
        std::vector<float> output(48000, 1);
        core.render(output.data(), (unsigned)output.size(), 48000);
        check(std::all_of(output.begin(), output.end(), [](float x) { return x == 0; }), "playback is silent without queued audio");
        core.requestTestTone(); core.render(output.data(), (unsigned)output.size(), 48000);
        float peak = 0;
        for (float x : output) peak = std::max(peak, std::abs(x));
        check(peak > 0.07f && peak <= 0.081f, "test tone is nonzero and amplitude limited");
        check(std::all_of(output.begin() + 16800, output.end(), [](float x) { return x == 0; }), "test tone ends after 350 ms");
        core.start();
        std::vector<float> input(6000, 0.25f);
        core.capture(input.data(), (unsigned)input.size(), 48000, 0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (core.snapshot().inputLevel == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        check(std::abs(core.snapshot().inputLevel - 0.25) < 1e-6 && core.snapshot().inputFrames == 6000, "capture splits large callbacks and computes measured RMS");
        core.stop();
        check(core.snapshot().inputLevel == 0 && core.snapshot().inputFrames == 0 && core.snapshot().depth == 0, "stop joins worker and clears session audio");
        core.start(); core.stop(); core.stop();
        check(core.snapshot().depth == 0, "start stop restart lifecycle is idempotent");
        for (const auto* configuration : {R"({"sourceLanguage":"pt"})", R"({"targetLanguage":"en"})"}) {
            lvt::AudioCore invalid;
            invalid.setSourceLanguage("en");
            invalid.configureProcessing(lvt::ProcessingOptions::fromJSON(configuration));
            bool rejected = false;
            try { invalid.start(); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "contradictory route configuration fails synchronously before any workers start");
            invalid.capture(input.data(), (unsigned)input.size(), 48000, 0);
            std::fill(output.begin(), output.end(), 1.f);
            invalid.render(output.data(), (unsigned)output.size(), 48000);
            const auto stopped = invalid.snapshot();
            check(stopped.inputFrames == 0 && stopped.depth == 0 && stopped.asr.state == 0 &&
                  stopped.conversation.translationState == 0 && stopped.conversation.ttsState == 0 &&
                  std::all_of(output.begin(), output.end(), [](float x) { return x == 0; }),
                  "rejected start leaves capture, playback and model workers stopped");
        }
        lvt::SelfPlaybackGuard echo;
        std::vector<float> reference(8000), leakage(320), doubleTalk(320), nearEnd(320);
        for (std::size_t i = 0; i < reference.size(); ++i)
            reference[i] = float(0.2 * std::sin(i * 0.131 + i * i * 0.00031) + 0.13 * std::sin(i * 0.077));
        for (std::size_t i = 0; i < leakage.size(); ++i) {
            leakage[i] = reference[7000 + i] * 0.4f;
            nearEnd[i] = float(0.1 * std::sin(i * 0.211));
            doubleTalk[i] = leakage[i] + nearEnd[i];
        }
        check(!echo.isSelfPlayback(leakage), "no suppression without a playback reference");
        echo.appendReference(reference);
        check(echo.isSelfPlayback(leakage), "delayed and attenuated self-playback leakage is detected");
        check(!echo.isSelfPlayback(doubleTalk), "simultaneous near-end voice remains eligible while playback is active");
        check(!echo.isSelfPlayback(nearEnd), "unrelated near-end audio is not blanket-muted");
        std::vector<float> silence(320, 0);
        check(!echo.isSelfPlayback(silence), "silence is left for VAD, not labeled as playback");
        echo.clear();
        check(!echo.isSelfPlayback(leakage), "Stop destroys playback reference history");
        lvt::EchoReferenceSampler sampler;
        std::array<float, 320> resampled{};
        std::vector<float> dc(1920, 0.25f);
        check(sampler.process(dc, 48000, resampled) == 320 && std::abs(resampled[319] - 0.25f) < 1e-6f,
              "echo reference conversion preserves DC and 48k to 8k duration");
        check(sampler.process(dc, 0, resampled) == 0, "echo branch rejects invalid sample rate");
        lvt::BoundedQueue<int, 2> bounded;
        bounded.push(1); bounded.push(2);
        check(!bounded.push(3) && bounded.metrics().currentDepth == 2 && bounded.metrics().droppedItems == 1,
              "worker queue enforces capacity and exposes real drop metrics");
        check(bounded.tryPop(value) && value == 1 && bounded.tryPop(value) && value == 2,
              "worker queue preserves FIFO when congested");
        bool canceled = false;
        std::jthread waiter([&](std::stop_token stop) { int out; canceled = !bounded.waitPop(out, stop); });
        waiter.request_stop(); waiter.join();
        check(canceled, "stop_token cancels empty worker queue wait without a sentinel");
        bounded.clear();
        check(bounded.metrics().currentDepth == 0 && bounded.metrics().droppedItems == 0 && bounded.metrics().oldestItemAgeMs == 0,
              "worker queue reset clears contents and metrics");
        std::ifstream fixture("tests/data/webrtc-test-audio.raw", std::ios::binary | std::ios::ate);
        check(bool(fixture), "licensed upstream real-audio fixture is present");
        const auto bytes = fixture.tellg(); fixture.seekg(0);
        std::vector<std::int16_t> recorded(std::size_t(bytes) / sizeof(std::int16_t));
        fixture.read(reinterpret_cast<char*>(recorded.data()), bytes);
        const std::array<std::string, 4> expectedVAD = {
            "011110111111111111111111111100", "011110111111111111111111111100",
            "000000111111111111111111110000", "000000111111111111111100000000"};
        for (int mode = 0; mode < 4; ++mode) {
            VadInst *raw = nullptr; WebRtcVad_Create(&raw);
            std::unique_ptr<VadInst, decltype(&WebRtcVad_Free)> vad(raw, WebRtcVad_Free);
            if (!vad) throw std::runtime_error("VAD allocation");
            WebRtcVad_Init(vad.get()); WebRtcVad_set_mode(vad.get(), mode);
            std::string actual;
            for (std::size_t i = 0; i + 240 <= recorded.size(); i += 240)
                actual += WebRtcVad_Process(vad.get(), 8000, recorded.data() + i, 240) == 1 ? '1' : '0';
            check(actual == expectedVAD[mode], ("WebRTC actual audio matches upstream golden decisions, mode " + std::to_string(mode)).c_str());
        }
        lvt::VadSegmenter segmenter;
        std::array<float, 320> frame{};
        bool unexpected = false;
        for (int i = 0; i < 50; ++i) unexpected |= segmenter.process(frame, false, 16000, i * 320, 0).has_value();
        check(!unexpected && !segmenter.active(), "VAD segmenter does not create segments from silence");
        for (int i = 0; i < 10; ++i) segmenter.process(frame, true, 16000, (50 + i) * 320, 0);
        std::optional<lvt::SpeechSegment> completed;
        for (int i = 0; i < 15; ++i) if (auto s = segmenter.process(frame, false, 16000, (60 + i) * 320, 0)) completed = std::move(s);
        check(completed && completed->samples.size() == 10400 && !segmenter.active(),
              "VAD segmentation retains 150 ms preroll, speech and 300 ms trailing silence");
        segmenter.clear();
        std::size_t boundedSegments = 0;
        for (int i = 0; i < 410; ++i) if (auto s = segmenter.process(frame, true, 16000, i * 320, 0)) {
            if (s->samples.size() > 128000) throw std::runtime_error("unbounded VAD segment");
            ++boundedSegments;
        }
        check(boundedSegments == 1, "continuous speech is bounded to eight seconds per committed segment");
        segmenter.clear();
        check(!segmenter.active(), "Stop destroys unfinished VAD segment");
        lvt::VadWorker vadWorker;
        vadWorker.start();
        std::vector<float> realAudio(recorded.size() + 4000, 0);
        for (std::size_t i = 0; i < recorded.size(); ++i) realAudio[i] = recorded[i] / 32768.0f;
        for (std::size_t i = 0; i < realAudio.size();) {
            lvt::AudioBlock block;
            block.count = (std::uint32_t)std::min(lvt::audioBlockSize, realAudio.size() - i);
            block.sampleRate = 8000; block.firstFrame = i;
            std::copy_n(realAudio.data() + i, block.count, block.samples.begin());
            vadWorker.push(block); i += block.count;
        }
        const auto vadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (vadWorker.snapshot().frames < realAudio.size() / 160 && std::chrono::steady_clock::now() < vadDeadline) std::this_thread::yield();
        lvt::SpeechSegment speech;
        check(vadWorker.snapshot().errors == 0 && vadWorker.popSegment(speech) && !speech.samples.empty(),
              "real audio crosses bounded VAD worker and emits an actual speech segment");
        vadWorker.stop();
        check(vadWorker.snapshot().frames == 0 && !vadWorker.popSegment(speech), "VAD worker stop clears classifier state and queued audio");
        lvt::SincResampler speechResampler;
        for (const double rate : {48000.0, 44100.0, 8000.0}) {
            std::vector<float> signal(std::size_t(rate), 0.25f);
            auto converted = speechResampler.to16k(signal, rate);
            check(converted.size() == 16000 && std::all_of(converted.begin() + 100, converted.end() - 100,
                  [](float x) { return std::abs(x - 0.25f) < 1e-5f; }), "ASR resampling preserves duration and DC across hardware rates");
        }
        auto toneEnergy = [&](double frequency) {
            std::vector<float> tone(48000);
            for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = float(std::sin(2 * 3.141592653589793 * frequency * i / 48000));
            auto converted = speechResampler.to16k(tone, 48000);
            double energy = 0;
            for (std::size_t i = 100; i + 100 < converted.size(); ++i) energy += converted[i] * converted[i];
            return energy / (converted.size() - 200);
        };
        check(toneEnergy(1000) > 0.49 && toneEnergy(12000) < 0.0001, "ASR downsampling keeps speech band and rejects out-of-band aliasing");
        bool invalidRate = false;
        try { speechResampler.to16k(silence, 0); } catch (const std::exception&) { invalidRate = true; }
        check(invalidRate, "ASR resampler rejects invalid hardware rates");
        bool invalidHash = false;
        try { lvt::verifyModelHash("tests/data/webrtc-test-audio.raw", std::string(64, '0')); }
        catch (const std::exception&) { invalidHash = true; }
        check(invalidHash, "model hash mismatch fails before model parsing");
        lvt::ASRWorker failedASR;
        failedASR.start("tests/data/webrtc-test-audio.raw", std::string(64, '0'), [](lvt::SpeechSegment&) { return false; });
        const auto errorDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (failedASR.snapshot().state == 1 && std::chrono::steady_clock::now() < errorDeadline) std::this_thread::yield();
        check(failedASR.snapshot().state == 3 && !failedASR.snapshot().error.empty(), "ASR worker exposes verified model failure without crashing audio");
        failedASR.stop();
        check(failedASR.snapshot().state == 0 && failedASR.snapshot().error.empty(), "ASR Stop clears failed model state");
        using lvt::Language;
        using lvt::LanguageRouter;
        check(LanguageRouter::route(Language::portuguese, true).target == Language::english, "PT-BR routes only to EN");
        check(LanguageRouter::route(Language::english, true).target == Language::portuguese, "all recognized English routes to PT-BR");
        check(!LanguageRouter::route(LanguageRouter::parse("es"), true).eligible && !LanguageRouter::route(Language::english, false).eligible,
              "unsupported and unstable languages cannot enter translation");
        lvt::LanguageStabilizer languages;
        check(languages.observe(1, {"pt", 0.95, 2000}).stable && languages.established(1) == Language::portuguese,
              "strong PT evidence establishes real speaker history");
        check(!languages.observe(1, {"es", 0.3, 200}).stable && languages.established(1) == Language::portuguese,
              "short uncertain Spanish cannot change PT history or trigger a guessed translation");
        check(languages.observe(2, {"en", 0.97, 2000}).stable && languages.established(1) == Language::portuguese,
              "different actual speaker IDs keep independent language histories");
        check(!languages.observe(1, {"en", 0.85, 1000}).stable && languages.established(1) == Language::portuguese,
              "one short candidate does not oscillate established language");
        check(languages.observe(1, {"en", 0.85, 1000}).stable && languages.established(1) == Language::english,
              "consecutive supported evidence permits a real language change");
        check(languages.observe(1, {"pt", 0.96, 3500}).stable && languages.established(1) == Language::portuguese,
              "long high-confidence speech permits immediate conscious language change");
        languages.clear();
        const auto unknownPT = languages.observe(std::nullopt, {"pt", 0.9, 2000});
        const auto unknownEN = languages.observe(std::nullopt, {"en", 0.9, 2000});
        check(unknownPT.stable && unknownEN.stable && languages.size() == 0,
              "unidentified participants never share an invented speaker history");
        check(!languages.observe(1, {"en", std::nan(""), 2000}).stable && !languages.observe(1, {"pt", 0.99, 100}).stable,
              "non-finite confidence and extremely short windows are rejected");
        for (unsigned i = 0; i < 100; ++i) languages.observe(i, {"en", 0.9, 2000});
        check(languages.size() == 64, "language history has a fixed memory capacity");
        languages.clear();
        check(languages.size() == 0 && !languages.established(1), "session reset destroys per-speaker language history");
        std::cout << "RESULT " << passed << " passed, 0 failed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << "\nRESULT " << passed << " passed, 1 failed\n"; return 1;
    }
}
