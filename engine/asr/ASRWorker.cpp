#include "ASRWorker.hpp"
#include "InferenceArbiter.hpp"
#include "ASRContext.hpp"
#include "../audio/SincResampler.hpp"
#include "../language/LanguageStabilizer.hpp"
#include "../speakers/OnlineDiarizer.hpp"
#include "../common/ModelHash.hpp"
#include <map>

namespace lvt {
namespace {
// Context is mutable in whisper.cpp. Share weights and working buffers across
// routes, and hold this lock for the complete mel/LID/decode transaction.
struct SharedRecognizer {
    MemoryBudget::Ticket memory;
    WhisperASR engine;
    InferenceArbiter inference;
    std::atomic<unsigned> clients{0};
    std::atomic<double> serviceMs{600};
    SharedRecognizer(const std::string& path, const std::string& hash, bool budgeted, const ASRConfiguration& configuration)
        : memory(budgeted ? MemoryBudget::shared().claim((configuration.backend == ASRBackend::metal ? 1800ULL : 2300ULL) << 20) : nullptr), engine(path, hash, configuration) {}
};
struct RecognizerClient {
    SharedRecognizer& shared;
    explicit RecognizerClient(SharedRecognizer& recognizer) : shared(recognizer) { ++shared.clients; }
    ~RecognizerClient() { --shared.clients; }
};
std::shared_ptr<SharedRecognizer> recognizer(const std::string& path, const std::string& hash, bool budgeted, const ASRConfiguration& configuration) {
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<SharedRecognizer>> loaded;
    std::lock_guard lock(mutex);
    for (auto it = loaded.begin(); it != loaded.end();) {
        if (it->second.expired()) it = loaded.erase(it); else ++it;
    }
    const auto key = path + "\n" + hash + "\n" + std::to_string(int(configuration.backend)) + "\n" + configuration.encoderDirectory + (budgeted ? "\nbudgeted" : "\ndiagnostic");
    if (auto existing = loaded[key].lock()) {
        configuration.verify();
        verifyModelHash(path, hash); // Reusing weights never bypasses new-session integrity checks.
        return existing;
    }
    auto result = std::make_shared<SharedRecognizer>(path, hash, budgeted, configuration);
    loaded[key] = result;
    return result;
}
}
void ASRWorker::start(std::string path, std::string hash, std::function<bool(SpeechSegment&)> input, std::string resources,
    std::function<bool(const SpeechSegment&)> obsolete,
    std::function<bool(std::span<const float>, std::stop_token, bool)> isPlayback, bool trimSpeakerEdges, std::string backend, bool collectBoundaryEvidence, std::string sourceLanguage, bool identifySpeakers) {
    stop();
    if (path.empty()) return;
    state_ = 1;
    worker_ = std::jthread([this, path = std::move(path), hash = std::move(hash), input = std::move(input), resources = std::move(resources), obsolete = std::move(obsolete), isPlayback = std::move(isPlayback), trimSpeakerEdges, backend = std::move(backend), collectBoundaryEvidence, sourceLanguage = std::move(sourceLanguage), identifySpeakers](std::stop_token stop) mutable {
        try {
            if (sourceLanguage == "pt-BR") sourceLanguage = "pt";
            if (!sourceLanguage.empty() && sourceLanguage != "pt" && sourceLanguage != "en")
                throw std::runtime_error("Manual recognition language must be pt or en");
            const auto configuration = asrConfiguration(resources, path, hash, backend);
            auto shared = recognizer(path, hash, !resources.empty(), configuration);
            RecognizerClient client(*shared);
            auto& engine = shared->engine;
            if (stop.stop_requested()) return;
            SincResampler resampler;
            LanguageStabilizer languages;
            ASRContext context;
            std::unique_ptr<OnlineDiarizer> diarizer;
            if (!resources.empty() && identifySpeakers) {
                nlohmann::json manifest; std::ifstream(std::filesystem::path(resources) / "manifest.json") >> manifest;
                auto find = [&](const char* type) {
                    auto it = std::find_if(manifest["models"].begin(), manifest["models"].end(), [&](const auto& e) { return e["type"] == type; });
                    if (it == manifest["models"].end()) throw std::runtime_error("Speaker model missing");
                    return ModelBundle::fromJSON(resources, *it);
                };
                diarizer = std::make_unique<OnlineDiarizer>(find("embedding"), find("overlap"), find("separation"), trimSpeakerEdges);
                std::lock_guard lock(errorMutex_); diarization_ = diarizer->metrics();
            }
            estimatedDemandMs_ = shared->serviceMs.load() * shared->clients.load();
            metal_ = engine.metalActive(); backend_ = int(engine.backend()); state_ = 2;
            while (!stop.stop_requested()) {
                SpeechSegment segment;
                if (!input(segment)) {
                    if (diarizer) { diarizer->releaseIdleModels(); std::lock_guard lock(errorMutex_); diarization_ = diarizer->metrics(); }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue;
                }
                // A final segment/newer preview may already supersede this work.
                segment.timing.asrDequeued = hostclock::now();
                ++dequeuedJobs_;
                if (auto ms = hostclock::milliseconds(segment.timing.vadPublished, segment.timing.asrDequeued)) pendingWait_.add(*ms);
                // Check before expensive diarization, not only before Whisper.
                if (obsolete && obsolete(segment)) { ++obsoleteJobs_; continue; }
                MemoryBudget::Ticket workingMemory;
                if (!resources.empty()) workingMemory = MemoryBudget::shared().claim(64ULL << 20);
                auto pcm = resampler.to16k(segment.samples, segment.sampleRate);
                const auto segmentEndFrame = std::uint64_t(segment.firstFrame * 16000 / segment.sampleRate) + pcm.size();
                ASRContext nextContext;
                std::vector<SpeechSegment> units;
                if (diarizer) {
                    segment.timing.diarizationStarted = hostclock::now();
                    SpeechSegment analysis{std::move(pcm), 16000, std::uint64_t(segment.firstFrame * 16000 / segment.sampleRate), segment.hostTime};
                    analysis.utterance = segment.utterance;
                    analysis.revision = segment.revision;
                    analysis.final = segment.final;
                    units = diarizer->process(std::move(analysis), stop, isPlayback);
                    segment.timing.diarizationFinished = hostclock::now();
                    speakers_ = diarizer->speakerCount();
                    std::lock_guard lock(errorMutex_); diarization_ = diarizer->metrics();
                } else if (!identifySpeakers && isPlayback && isPlayback(pcm, stop, false)) {
                    // Identity controls never disable PCM playback-recursion protection.
                    std::lock_guard lock(errorMutex_); ++diarization_.playbackUnitsSuppressed;
                } else {
                    units.push_back({std::move(pcm), 16000, std::uint64_t(segment.firstFrame * 16000 / segment.sampleRate), segment.hostTime,
                        identifySpeakers ? 0U : 1U});
                    if (!identifySpeakers) speakers_ = 1;
                }
                std::fill(segment.samples.begin(), segment.samples.end(), 0);
                std::uint32_t lane = 0;
                for (auto& unit : units) {
                auto outdated = [&] { return obsolete && obsolete(segment); };
                if (outdated()) { ++obsoleteJobs_; break; }
                ASRResult result;
                const auto inferenceWaiting = hostclock::now();
                const auto unitEndFrame = unit.firstFrame + unit.samples.size();
                if (!unit.overlap) {
                auto inference = shared->inference.acquire(segment.final, stop, outdated);
                // Include time spent waiting even when a revision/STOP cancels
                // admission. Reporting only successful text hides congestion.
                if (auto ms = hostclock::milliseconds(inferenceWaiting, hostclock::now())) inferenceWait_.add(*ms);
                if (!inference || stop.stop_requested() || outdated()) { ++obsoleteJobs_; break; }
                const auto serviceStart = hostclock::now();
                const auto detectionsBefore = engine.languageDetections();
                result = engine.recognize(unit.samples, stop, [&](const std::string& language) {
                    return context.prompt(unit.speaker, language, unit.firstFrame);
                }, outdated, collectBoundaryEvidence && !segment.final, sourceLanguage);
                languageDetections_ += engine.languageDetections() - detectionsBefore;
                if (result.rejectedNoSpeech) ++rejectedNoSpeech_;
                if (result.rejectedDigitalSilence) ++rejectedDigitalSilence_;
                if (auto ms = hostclock::milliseconds(result.timing.inferenceStarted, result.timing.melFinished)) mel_.add(*ms);
                if (!result.languageForced) if (auto ms = hostclock::milliseconds(result.timing.melFinished, result.timing.languageFinished)) language_.add(*ms);
                if (auto ms = hostclock::milliseconds(result.timing.languageFinished, result.timing.inferenceFinished)) decode_.add(*ms);
                const double service = hostclock::milliseconds(serviceStart, hostclock::now()).value_or(0) +
                    hostclock::milliseconds(segment.timing.diarizationStarted, segment.timing.diarizationFinished).value_or(0);
                auto previous = shared->serviceMs.load();
                while (!shared->serviceMs.compare_exchange_weak(previous, std::max(service, previous * .9 + service * .1))) {}
                estimatedDemandMs_ = shared->serviceMs.load() * shared->clients.load();
                }
                std::fill(unit.samples.begin(), unit.samples.end(), 0);
                if (stop.stop_requested() || outdated()) { ++obsoleteJobs_; break; }
                if (result.text.empty() && !unit.overlap) { ++emptyUnits_; continue; }
                const auto recognitionTiming = result.timing;
                result.timing = segment.timing;
                // A later speaker lane can grow while this lane's audio is fixed.
                // Only this unit's actual new samples count as fresh ASR evidence.
                result.timing.windowEndFrame = unitEndFrame;
                result.timing.windowEndHostTime = hostclock::advance(unit.hostTime, (unitEndFrame - unit.firstFrame) / 16000.0);
                if (result.timing.speechEndFrame > unitEndFrame) result.timing.speechEndHostTime = 0;
                if (!unit.overlap) {
                    result.timing.inferenceWaiting = inferenceWaiting;
                    result.timing.inferenceStarted = recognitionTiming.inferenceStarted;
                    result.timing.melFinished = recognitionTiming.melFinished;
                    result.timing.languageFinished = recognitionTiming.languageFinished;
                    result.timing.inferenceFinished = recognitionTiming.inferenceFinished;
                }
                result.utterance = segment.utterance; result.revision = segment.revision; result.final = segment.final; result.lane = lane++;
                result.speaker = unit.speaker; result.overlap = unit.overlap;
                auto previewLanguages = languages;
                const auto decision = result.languageForced
                    ? LanguageDecision{LanguageRouter::parse(result.detectedLanguage), true, "manual-language"}
                    : (segment.final ? languages : previewLanguages).observe(unit.speaker ? std::optional<std::uint32_t>(unit.speaker) : std::nullopt,
                        {result.detectedLanguage, result.detectedProbability, result.durationMs});
                const auto direction = LanguageRouter::route(decision.language, decision.stable);
                result.targetLanguage = direction.eligible ? LanguageRouter::code(direction.target) : "";
                result.languageStatus = decision.status;
                result.firstFrame = unit.firstFrame; result.hostTime = unit.hostTime;
                if (segment.final && unit.speaker && direction.eligible) {
                    nextContext.retainFinal(context, unit.speaker, result.detectedLanguage, result.text,
                        unit.firstFrame, unitEndFrame, segmentEndFrame, segment.timing.endpointReason, unit.overlap);
                }
                latency_ = result.latencyMs; rtf_ = result.durationMs > 0 ? result.latencyMs / result.durationMs : 0;
                ++completed_; results_.push(std::move(result));
                }
                if (segment.final) {
                    context = std::move(nextContext);
                    if (!resources.empty() && !stop.stop_requested()) {
                        ASRResult end; end.utterance = segment.utterance; end.final = true; end.languageStatus = "utterance-end";
                        results_.push(std::move(end));
                    }
                    ++processed_;
                }
            }
        } catch (const std::exception& error) {
            if (!stop.stop_requested()) {
                std::lock_guard lock(errorMutex_); error_ = error.what(); state_ = 3;
            }
        }
        metal_ = false; backend_ = 0;
        std::lock_guard lock(errorMutex_); diarization_.loaded = false; diarization_.separationLoaded = false;
    });
}
void ASRWorker::stop() {
    if (worker_.joinable()) { worker_.request_stop(); worker_.join(); }
    results_.clear(); state_ = 0; metal_ = false; backend_ = 0; completed_ = 0; processed_ = 0; latency_ = 0; rtf_ = 0;
    speakers_ = 0;
    estimatedDemandMs_ = 600; dequeuedJobs_ = 0; obsoleteJobs_ = 0; emptyUnits_ = 0;
    languageDetections_ = 0; rejectedNoSpeech_ = 0; rejectedDigitalSilence_ = 0;
    pendingWait_.clear(); inferenceWait_.clear(); mel_.clear(); language_.clear(); decode_.clear();
    std::lock_guard lock(errorMutex_); error_.clear(); diarization_ = {};
}
ASRMetrics ASRWorker::snapshot() const {
    std::lock_guard lock(errorMutex_);
    return {state_.load(), metal_.load(), completed_.load(), latency_.load(), rtf_.load(), error_, results_.metrics(), processed_.load(), speakers_.load(), diarization_, pendingWait_.snapshot(), inferenceWait_.snapshot(), mel_.snapshot(), language_.snapshot(), decode_.snapshot(), estimatedDemandMs_.load(), dequeuedJobs_.load(), obsoleteJobs_.load(), emptyUnits_.load(), backend_.load(), languageDetections_.load(), rejectedNoSpeech_.load(), rejectedDigitalSilence_.load()};
}
}
