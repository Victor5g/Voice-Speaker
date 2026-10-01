#include "ConversationPipeline.hpp"
#include "../translation/TranslationEngine.hpp"
#include "../tts/TTSEngine.hpp"
#include "../tts/VoiceCache.hpp"
#include "../tts/SessionVoices.hpp"
#include "SegmentStabilityManager.hpp"
#include <map>
#include <set>

namespace lvt {
void ConversationPipeline::emit(TranslationResult result, std::string state, std::string error) {
    if (result.identity.session == session_) events_.push({std::move(result), std::move(state), std::move(error), ""});
}
void ConversationPipeline::fail(const std::string& error) { std::lock_guard lock(errorMutex_); error_ = error; }
void ConversationPipeline::start(std::string root, double rate, std::uint64_t session, std::function<bool(ASRResult&)> input, StreamingPolicy policy, ProcessingOptions options) {
    policy.validate(); validateProcessingOptions(options);
    stop(); session_ = session; playback_.enable();
    translationState_ = 1; ttsState_ = options.speechOutput ? 1 : 0;
    translationWorker_ = std::jthread([this, root, options](std::stop_token stop) {
        try {
            nlohmann::json manifest, selection;
            std::ifstream(std::filesystem::path(root) / "manifest.json") >> manifest;
            std::ifstream(std::filesystem::path(root) / "translation-selection.json") >> selection;
            std::map<std::string, std::unique_ptr<TranslationEngine>> engines;
            for (const std::string direction : {"pt-en", "en-pt"}) {
                if (!sourceLanguage_.empty() && direction != sourceLanguage_ + (sourceLanguage_ == "pt" ? "-en" : "-pt")) continue;
                if (!options.sourceLanguage.empty() && direction.substr(0,2)!=options.sourceLanguage) continue;
                if (!options.targetLanguage.empty() && direction.substr(3,2)!=options.targetLanguage) continue;
                auto precision = selection.at(direction).get<std::string>();
                auto found = std::find_if(manifest["models"].begin(), manifest["models"].end(), [&](const auto& m) {
                    return m.value("type", "") == "translation" && m.value("direction", "") == direction && m.value("quantization", "") == precision;
                });
                if (found == manifest["models"].end()) throw std::runtime_error("Missing translation model");
                engines[direction] = std::make_unique<TranslationEngine>(ModelBundle::fromJSON(root, *found), direction, precision);
                if(!options.glossaryJSON.empty())engines[direction]->setGlossaryJSON(options.glossaryJSON);
                if (stop.stop_requested()) return;
            }
            if (engines.empty()) throw std::runtime_error("No translation model matches the configured direction");
            translationState_ = 2;
            TranslationResult item;
            while (!stop.stop_requested()) {
                if (!translationQueue_.waitPopFor(item, stop, std::chrono::milliseconds(250))) {
                    if (MemoryBudget::shared().pressure()) for (const auto& [direction, engine] : engines) engine->releaseCaches();
                    continue;
                }
                if (item.identity.session != session_) continue;
                item.identity.timing.translationStarted = hostclock::now();
                if (auto ms = hostclock::milliseconds(item.identity.timing.translationQueued, item.identity.timing.translationStarted)) translationWait_.add(*ms);
                emit(item, "translating");
                try {
                    auto result = engines.at(item.targetLanguage == "en" ? "pt-en" : "en-pt")->translate(item.original, stop);
                    item.identity.timing.translationFinished = hostclock::now();
                    if (stop.stop_requested() || item.identity.session != session_) break;
                    item.translated = std::move(result.text); item.translationLatencyMs = result.latencyMs;
                    translationMs_ = result.latencyMs; ++translated_; emit(item, "translated");
                    if(!options.speechOutput) emit(item,"text-only");
                    else if (ttsState_ == 3) emit(item, "error", "Síntese indisponível: verifique os modelos de voz.");
                    else {
                        item.identity.timing.ttsQueued = hostclock::now();
                        if (!ttsQueue_.push(item)) emit(item, "dropped", "Fila de voz cheia; este trecho não será reproduzido.");
                    }
                } catch (const MemoryBudget::Unavailable& error) { emit(item, "unavailable", error.what()); }
                catch (...) { emit(item, "error", "Falha na tradução local deste trecho."); }
            }
        } catch (const MemoryBudget::Unavailable& error) {
            if (!stop.stop_requested()) { translationState_ = 3; fail(error.what()); }
        } catch (...) {
            if (!stop.stop_requested()) { translationState_ = 3; fail("Modelo de tradução ausente, corrompido ou incompatível."); }
        }
    });
    if(options.speechOutput)ttsWorker_ = std::jthread([this, root, rate, options](std::stop_token stop) {
        try {
            nlohmann::json manifest;
            std::ifstream(std::filesystem::path(root) / "manifest.json") >> manifest;
            std::map<std::string, ModelBundle> bundles;
            for (const auto& entry : manifest.at("models")) if (entry.at("type") == "tts") bundles.emplace(entry.at("voice"), ModelBundle::fromJSON(root, entry));
            constexpr const char* voices[][2] = {{"pt_BR-faber-medium", "en_US-lessac-medium"}, {"pt_BR-jeff-medium", "en_US-ryan-medium"}, {"pt_BR-edresson-low", "en_US-amy-medium"}};
            for (const auto& pair : voices) for (const auto* name : pair) if (!bundles.contains(name)) throw std::runtime_error("Missing paired voice");
            VoiceCache<TTSEngine> cache;
            SessionVoices assignments;
            auto publishCache = [&] {
                std::string names;
                for (const auto& name : cache.voices()) { if (!names.empty()) names += ", "; names += name; }
                std::lock_guard lock(errorMutex_); cachedVoices_ = std::move(names);
            };
            // Prepare only the first voice pair (one target voice in call mode).
            // Startup is measured separately; there is no generated test speech.
            const auto warmupStart = std::chrono::steady_clock::now();
            for (int language = 0; language < 2 && !stop.stop_requested(); ++language) {
                if (sourceLanguage_ == "pt" && language == 0) continue;
                if (sourceLanguage_ == "en" && language == 1) continue;
                if (!options.targetLanguage.empty() && options.targetLanguage!=(language?"en":"pt"))continue;
                if (!options.sourceLanguage.empty() && options.sourceLanguage==(language?"en":"pt"))continue;
                const std::string voice = voices[options.identifySpeakers?0:options.voiceIndex][language];
                try { cache.get(voice, [&] { return std::make_unique<TTSEngine>(bundles.at(voice)); }, MemoryBudget::shared().pressure()); }
                catch (const MemoryBudget::Unavailable&) { break; } // Optional preparation; requests still report denial.
            }
            ttsWarmupMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - warmupStart).count();
            publishCache();
            if (stop.stop_requested()) return;
            ttsState_ = 2;
            TranslationResult item;
            while (!stop.stop_requested()) {
                if (!ttsQueue_.waitPopFor(item, stop, std::chrono::milliseconds(250))) {
                    playback_.releaseRetired();
                    if (MemoryBudget::shared().pressure()>=2) { cache.clear(); publishCache(); std::lock_guard lock(errorMutex_); loadedVoice_.clear(); }
                    else if(MemoryBudget::shared().pressure()) { cache.trimIdle(); publishCache(); }
                    continue;
                }
                if (item.identity.session != session_) continue;
                if(playback_.pendingSeconds()>=options.maxQueuedSpeechSeconds) {
                    ++audioSkipped_;emit(item,"audio-skipped","Áudio omitido para evitar atraso acumulado; tradução mantida em texto.");continue;
                }
                try {
                    item.identity.timing.ttsStarted = hostclock::now();
                    if (auto ms = hostclock::milliseconds(item.identity.timing.ttsQueued, item.identity.timing.ttsStarted)) ttsWait_.add(*ms);
                    const std::string voice = assignments.voice(item.identity.speaker,item.targetLanguage,options.identifySpeakers,options.voiceIndex);
                    emit(item, "synthesizing");
                    const auto loadStart = std::chrono::steady_clock::now();
                    auto voiceEngine = cache.get(voice, [&] { return std::make_unique<TTSEngine>(bundles.at(voice)); }, MemoryBudget::shared().pressure());
                    ttsVoiceLoadMs_ = voiceEngine.loaded ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart).count() : 0;
                    if (voiceEngine.loaded) ++voiceCacheMisses_; else ++voiceCacheHits_;
                    publishCache();
                    { std::lock_guard lock(errorMutex_); loadedVoice_ = voice; }
                    item.identity.timing.voiceReady = hostclock::now();
                    auto audio = voiceEngine.engine.synthesize(item.translated, stop);
                    item.identity.timing.ttsFinished = hostclock::now();
                    if (stop.stop_requested() || item.identity.session != session_) break;
                    ttsMs_ = audio.latencyMs; ++synthesized_;
                    ttsAudioDurationMs_ = 1000.0 * audio.samples.size() / audio.sampleRate;
                    const auto pending=playback_.pendingSeconds();
                    // One long utterance can play in full when output is free.
                    // Never cut active PCM; decline only new complete units.
                    if(pending>0 && pending+audio.samples.size()/double(audio.sampleRate)>options.maxQueuedSpeechSeconds){
                        ++audioSkipped_;emit(item,"audio-skipped","Áudio omitido para evitar atraso acumulado; tradução mantida em texto.");continue;
                    }
                    SynthesizedAudio synthesized{item, std::move(audio.samples), audio.sampleRate, voice, audio.latencyMs};
                    ConversationEvent event{item, "queued", "", voice}; event.synthesisLatencyMs = audio.latencyMs;
                    if (item.identity.session == session_) events_.push(std::move(event));
                    if (!playback_.enqueue(synthesized, rate)) emit(item, "dropped", "Fila de reprodução cheia; este trecho não será reproduzido.");
                } catch (const MemoryBudget::Unavailable& error) { publishCache(); emit(item, "unavailable", error.what()); }
                catch (...) { publishCache(); emit(item, "error", "Falha na voz local deste trecho."); }
            }
        } catch (...) { if (!stop.stop_requested()) { ttsState_ = 3; fail("Modelos de voz ausentes, corrompidos ou incompatíveis."); } }
    });
    coordinator_ = std::jthread([this, input = std::move(input), session, policy, options](std::stop_token stop) {
        std::uint64_t segment = 0;
        std::map<std::uint64_t, SegmentStabilityManager> stability;
        std::set<std::uint64_t> firstAudioSources;
        std::deque<std::uint64_t> firstAudioOrder;
        mach_timebase_info_data_t tb{}; mach_timebase_info(&tb);
        while (!stop.stop_requested()) {
            ASRResult asr;
            if (input(asr)) {
                if (asr.languageStatus == "utterance-end") {
                    for (std::uint64_t lane = 0; lane < 8; ++lane) {
                        const auto key = asr.utterance * 8 + lane;
                        if (stability.erase(key)) { TranslationResult abandoned; abandoned.identity.session = session; abandoned.identity.segment = (1ULL << 62) | key; emit(abandoned, "removed"); }
                    }
                    // ASR emits one end marker after all lanes of each final
                    // source unit. FIFO consumption means preceding results have
                    // been coordinated here, including filtered/empty units.
                    // This does not acknowledge translation, TTS or playback.
                    coordinatedUtterances_.fetch_add(1, std::memory_order_release);
                    continue;
                }
                const auto key = asr.utterance * 8 + asr.lane;
                const auto partialID = (1ULL << 62) | key;
                TranslationResult item{{session, partialID, asr.hostTime, asr.firstFrame}, asr.text, "", asr.detectedLanguage,
                    asr.targetLanguage, asr.durationMs, asr.latencyMs, 0};
                item.identity.speaker = asr.speaker; item.identity.revision = asr.revision;
                item.identity.timing = asr.timing;
                item.identity.sourceSegment = key;
                if(!options.targetLanguage.empty()&&!asr.targetLanguage.empty())item.targetLanguage=options.targetLanguage=="pt"?"pt-BR":options.targetLanguage;
                if (!sourceLanguage_.empty() && asr.detectedLanguage != sourceLanguage_) {
                    stability.erase(key);
                    emit(item, "removed");
                    if (asr.final) emit(item, "direction-filtered", "Idioma diferente do esperado neste caminho de áudio.");
                }
                else if (!item.targetLanguage.empty() && (item.sourceLanguage==item.targetLanguage || (item.sourceLanguage=="pt"&&item.targetLanguage=="pt-BR"))) {
                    stability.erase(key);emit(item,"removed");
                    if(asr.final)emit(item,"same-language","Fala já está no idioma de destino.");
                }
                else if (asr.overlap) emit(item, "overlap-unresolved", "Falas simultâneas detectadas; separação ainda não confirmada.");
                else if (asr.targetLanguage.empty()) emit(item, asr.final ? (asr.languageStatus == "unsupported" ? "unsupported" : "uncertain") : "partial");
                else if (translationState_ == 3) emit(item, "error", "Tradução indisponível: verifique os modelos.");
                else {
                    if (!stability.contains(key) && stability.size() >= 32) {
                        emit(item, "dropped", "Limite de revisões simultâneas atingido.");
                    } else {
                        auto [manager, inserted] = stability.try_emplace(key, policy);
                        // Text can stabilize before the embedding has enough evidence.
                        // Keep accumulating context but do not speak a provisional
                        // default voice and switch voices when identity is confirmed.
                        const auto update = manager->second.observe(asr.text, asr.final, asr.timing.windowEndFrame, asr.speaker || asr.final, asr.boundaryProbability);
                        emit(item, "removed");
                        if (update.conflict) {
                            ++revisionConflicts_;
                            if (update.recovered) ++recoveredContinuations_;
                            emit(item, update.recovered ? "revision-preserved" : "revision-conflict", update.recovered
                                ? "Uma palavra já falada mudou na revisão. O áudio foi preservado e a continuação foi mantida."
                                : "A revisão divergiu de texto já falado; esse áudio foi preservado. A continuação não pôde ser confirmada.");
                        }
                        if (!update.conflict || update.recovered) {
                            if (!update.confirmed.empty()) {
                                auto committed = item; committed.identity.segment = ++segment; committed.original = update.confirmed;
                                committed.identity.timing.confirmed = hostclock::now();
                                emit(committed, "recognized");
                                committed.identity.timing.translationQueued = hostclock::now();
                                if (!translationQueue_.push(committed)) emit(committed, "dropped", "Fila de tradução cheia; este trecho foi descartado.");
                            }
                            if (!update.remaining.empty()) { item.original = update.remaining; emit(item, "partial"); }
                        }
                    }
                }
                if (asr.final) stability.erase(key);
            }
            PlaybackEvent playback;
            while (playback_.popEvent(playback)) {
                ConversationEvent event;
                event.result.identity = playback.identity; event.playbackHostTime = playback.hostTime;
                event.state = playback.audible ? "audio-onset" : playback.started ? "playing" : "played";
                const auto& timing = playback.identity.timing;
                if(playback.audible)if(auto ms=hostclock::milliseconds(timing.windowEndHostTime,playback.hostTime))chunkLag_.add(*ms+deviceLatencyMs_);
                if (playback.started) {
                    if (auto ms = hostclock::milliseconds(timing.playbackQueued, playback.hostTime)) playbackWait_.add(*ms);
                }
                if (playback.audible && timing.endpointReason == EndpointReason::silence) {
                    if (auto ms = hostclock::milliseconds(timing.speechEndHostTime, playback.hostTime)) afterVadEnd_.add(*ms + deviceLatencyMs_);
                    if (auto ms = hostclock::milliseconds(timing.speechEndHostTime, timing.endpointHostTime)) endpoint_.add(*ms);
                }
                if (playback.identity.captureHostTime && playback.hostTime >= playback.identity.captureHostTime) {
                    const auto ms = (playback.hostTime - playback.identity.captureHostTime) * double(tb.numer) / tb.denom / 1e6 + deviceLatencyMs_;
                    if (playback.started && firstAudioSources.insert(playback.identity.sourceSegment).second) {
                        firstAudio_.add(ms); event.firstAudioMs = ms; firstAudioOrder.push_back(playback.identity.sourceSegment);
                        if (firstAudioOrder.size() > 256) { firstAudioSources.erase(firstAudioOrder.front()); firstAudioOrder.pop_front(); }
                    }
                    else if (!playback.started && !playback.audible) { endToEnd_.add(ms); event.endToEndMs = ms; }
                }
                if (playback.identity.session == session_) events_.push(std::move(event));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
}
void ConversationPipeline::requestStop() {
    session_ = 0; playback_.invalidate();
    coordinator_.request_stop(); translationWorker_.request_stop(); ttsWorker_.request_stop();
}
void ConversationPipeline::stop() {
    requestStop();
    if (coordinator_.joinable()) coordinator_.join();
    if (translationWorker_.joinable()) translationWorker_.join();
    if (ttsWorker_.joinable()) ttsWorker_.join();
    translationQueue_.clear(); ttsQueue_.clear(); events_.clear();
    firstAudio_.clear(); endToEnd_.clear();
    afterVadEnd_.clear(); endpoint_.clear(); translationWait_.clear(); ttsWait_.clear(); playbackWait_.clear();
    translationState_ = 0; ttsState_ = 0; translated_ = 0; synthesized_ = 0; translationMs_ = 0; ttsMs_ = 0;
    ttsAudioDurationMs_ = 0; ttsVoiceLoadMs_ = 0;
    ttsWarmupMs_ = 0; voiceCacheHits_ = 0; voiceCacheMisses_ = 0;
    revisionConflicts_ = 0; recoveredContinuations_ = 0;
    coordinatedUtterances_ = 0;
    audioSkipped_=0; chunkLag_.clear();
    std::lock_guard lock(errorMutex_); error_.clear(); loadedVoice_.clear(); cachedVoices_.clear();
}
ConversationMetrics ConversationPipeline::snapshot() const {
    std::lock_guard lock(errorMutex_);
    // Observe the coordinator acknowledgment BEFORE the output queue. Otherwise
    // a consumer could read an empty queue, then a newer acknowledgment, and miss
    // a recognized event published between those observations. A draining caller
    // must also track each accepted unit through its terminal playback/error event.
    const auto coordinated = coordinatedUtterances_.load(std::memory_order_acquire);
    const auto eventMetrics = events_.metrics();
    return {translationState_.load(), ttsState_.load(), translated_.load(), synthesized_.load(), eventMetrics.droppedItems,
        translationMs_.load(), ttsMs_.load(), translationQueue_.metrics(), ttsQueue_.metrics(), error_, firstAudio_.snapshot(), endToEnd_.snapshot(), loadedVoice_, ttsAudioDurationMs_.load(), ttsVoiceLoadMs_.load(), afterVadEnd_.snapshot(), endpoint_.snapshot(), translationWait_.snapshot(), ttsWait_.snapshot(), playbackWait_.snapshot(), playback_.pendingSeconds(), cachedVoices_, ttsWarmupMs_.load(), voiceCacheHits_.load(), voiceCacheMisses_.load(), revisionConflicts_.load(), recoveredContinuations_.load(), coordinated, eventMetrics.currentDepth,audioSkipped_.load(),chunkLag_.snapshot()};
}
}
