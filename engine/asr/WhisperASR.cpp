#include "WhisperASR.hpp"
#include "BoundaryEvidence.hpp"
#include "SpeechEvidence.hpp"
#include "../common/ModelHash.hpp"
#include "whisper.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>
#include <mutex>

namespace lvt {
namespace {
thread_local std::atomic<bool>* loadingMetalObserver = nullptr;
std::once_flag loggerOnce;
}
struct WhisperASR::Weights {
    ASRConfiguration configuration;
    // The native context retains coreml_model_path. Keep its storage alive until
    // the last state has gone, including when the original wrapper is destroyed.
    std::string encoderPath;
    whisper_context* context{};
    explicit Weights(const ASRConfiguration& config) : configuration(config) {}
    ~Weights() { if (context) whisper_free(context); }
};
WhisperASR::WhisperASR(const std::string& path, const std::string& hash, const ASRConfiguration& configuration) {
    verifyModelHash(path, hash); // Before any model parsing or GPU allocation.
    configuration.verify();
    backend_ = configuration.backend;
    reuseEncoder_ = configuration.reuseEncoder;
    weights_ = std::make_shared<Weights>(configuration);
    if (backend_ != ASRBackend::metal) weights_->encoderPath = configuration.encoder.path(configuration.encoderDirectory).string();
    threads_ = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
    std::call_once(loggerOnce, [] {
        whisper_log_set([](ggml_log_level, const char *message, void *) {
            // The process-wide logger never owns a session pointer. Loading and
            // teardown may overlap after STOP without stale callback userdata.
            if (!loadingMetalObserver) return;
            if (std::strstr(message, "using Metal backend")) *loadingMetalObserver = true;
            if (std::strstr(message, "failed to initialize Metal backend")) *loadingMetalObserver = false;
        }, nullptr);
    });
    struct ObserverScope {
        ObserverScope(std::atomic<bool>& value) { loadingMetalObserver = &value; }
        ~ObserverScope() { loadingMetalObserver = nullptr; }
    } observer(metalActive_);
    auto params = whisper_context_default_params();
    params.use_gpu = true;
    params.flash_attn = true;
    params.coreml_compute_units = int(backend_);
    params.coreml_model_path = weights_->encoderPath.empty() ? nullptr : weights_->encoderPath.c_str();
    params.reuse_encoder = reuseEncoder_;
    context_ = whisper_init_from_file_with_params_no_state(path.c_str(), params);
    weights_->context = context_;
    if (!context_) {
        throw std::runtime_error("whisper.cpp could not initialize the verified model");
    }
    initializeState();
}
WhisperASR::WhisperASR(std::shared_ptr<Weights> weights) : weights_(std::move(weights)) {
    // A Core ML state opens the encoder resources again; recheck the inventory.
    weights_->configuration.verify();
    context_ = weights_->context;
    backend_ = weights_->configuration.backend;
    reuseEncoder_ = weights_->configuration.reuseEncoder;
    threads_ = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
    initializeState();
}
void WhisperASR::initializeState() {
    struct ObserverScope {
        std::atomic<bool>* previous;
        explicit ObserverScope(std::atomic<bool>& value) : previous(loadingMetalObserver) { loadingMetalObserver = &value; }
        ~ObserverScope() { loadingMetalObserver = previous; }
    } observer(metalActive_);
    state_ = whisper_init_state(context_);
    if (!state_) throw std::runtime_error("whisper.cpp could not initialize inference state");
}
std::unique_ptr<WhisperASR> WhisperASR::forkState() const {
    return std::unique_ptr<WhisperASR>(new WhisperASR(weights_));
}
WhisperASR::~WhisperASR() {
    if (state_) whisper_free_state(state_);
}
ASRResult WhisperASR::recognize(std::span<const float> samples, std::stop_token stop,
    std::function<std::string(const std::string&)> context, std::function<bool()> obsolete, bool collectBoundaryEvidence,
    std::string forcedLanguage) {
    ASRResult result;
    result.durationMs = samples.size() / 16.0;
    auto cancelled = [&] { return stop.stop_requested() || (obsolete && obsolete()); };
    if (forcedLanguage == "pt-BR") forcedLanguage = "pt";
    if (!forcedLanguage.empty() && forcedLanguage != "pt" && forcedLanguage != "en")
        throw std::runtime_error("Manual recognition language must be pt or en");
    result.languageForced = !forcedLanguage.empty();
    if (samples.size() < 1600 || cancelled()) return result;
    const auto speech = SpeechEvidence::analyze(samples);
    result.peakBlockRMS = speech.peakBlockRMS;
    if (speech.silent) { result.rejectedNoSpeech = true; result.rejectedDigitalSilence = true; return result; }
    // Preserve brief legitimate speech instead of rejecting every sub-500 ms
    // input. This only supplies model padding; it does not add capture latency.
    std::vector<float> padded;
    if (samples.size() < 8000) { padded.assign(samples.begin(), samples.end()); padded.resize(8000, 0); samples = padded; }
    const auto begin = std::chrono::steady_clock::now();
    result.timing.inferenceStarted = hostclock::now();
    if (whisper_pcm_to_mel_with_state(context_, state_, samples.data(), int(samples.size()), threads_) != 0)
        throw std::runtime_error("Whisper mel extraction failed");
    result.timing.melFinished = hostclock::now();
    if (cancelled()) return {};
    int language = -1;
    if (result.languageForced) {
        language = whisper_lang_id(forcedLanguage.c_str());
    } else {
        std::vector<float> probabilities(whisper_lang_max_id() + 1);
        ++languageDetections_;
        language = whisper_lang_auto_detect_with_state(context_, state_, 0, threads_, probabilities.data());
        if (language >= 0) {
            result.ptProbability = probabilities[whisper_lang_id("pt")];
            result.enProbability = probabilities[whisper_lang_id("en")];
            result.detectedProbability = probabilities[language];
        }
    }
    result.timing.languageFinished = hostclock::now();
    if (language < 0) throw std::runtime_error("Whisper language detection failed");
    result.detectedLanguage = whisper_lang_str(language);
    if (cancelled()) return {};
    auto params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    params.beam_search.beam_size = 5;
    params.n_threads = threads_;
    params.translate = false;
    params.no_context = true;
    const std::string prompt = context ? context(result.detectedLanguage) : "";
    params.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
    params.n_max_text_ctx = 128;
    params.print_progress = false; params.print_realtime = false;
    params.print_timestamps = false; params.print_special = false;
    params.language = whisper_lang_str(language);
    params.suppress_blank = true; params.suppress_nst = true;
    params.temperature = 0; params.temperature_inc = 0;
    std::function<bool()> cancellation = cancelled;
    params.abort_callback = [](void *state) { return (*static_cast<std::function<bool()>*>(state))(); };
    params.abort_callback_user_data = &cancellation;
    // Automatic language detection already encoded this exact mel. In manual
    // mode only mel exists: full decoding performs its one required encode.
    // Passing null PCM avoids computing the same mel twice. The native cache is
    // invalidated by every new PCM/mel, window offset or audio-context change.
    // No reuse across growing windows, different speakers or different routes.
    const bool usePreparedMel = reuseEncoder_ || result.languageForced;
    const int status = whisper_full_with_state(context_, state_, params, usePreparedMel ? nullptr : samples.data(), usePreparedMel ? 0 : int(samples.size()));
    result.timing.inferenceFinished = hostclock::now();
    if (cancelled()) return {};
    if (status != 0) throw std::runtime_error("Whisper transcription failed");
    BoundaryEvidence boundaries;
    double minimumNoSpeech = 1, logProbabilitySum = 0;
    std::size_t confidenceTokens = 0;
    bool confidenceValid = true;
    for (int i = 0; i < whisper_full_n_segments_from_state(state_); ++i) {
        result.text += whisper_full_get_segment_text_from_state(state_, i);
        const auto noSpeech = whisper_full_get_segment_no_speech_prob_from_state(state_, i);
        confidenceValid &= std::isfinite(noSpeech) && noSpeech >= 0 && noSpeech <= 1;
        minimumNoSpeech = std::min(minimumNoSpeech, double(noSpeech));
        for (int token = 0; token < whisper_full_n_tokens_from_state(state_, i); ++token) {
            if (whisper_full_get_token_id_from_state(state_, i, token) >= whisper_token_eot(context_)) continue;
            const auto probability = whisper_full_get_token_p_from_state(state_, i, token);
            const char* text = whisper_full_get_token_text_from_state(context_, state_, i, token);
            if (collectBoundaryEvidence) boundaries.token(text, probability);
            // Padding/special tokens and whitespace alone do not count as text evidence.
            if (std::string_view(text).find_first_not_of(" \t\r\n") == std::string_view::npos) continue;
            if (!std::isfinite(probability) || probability <= 0 || probability > 1) { confidenceValid = false; continue; }
            logProbabilitySum += std::log(probability); ++confidenceTokens;
        }
    }
    result.speechConfidenceMeasured = confidenceValid && confidenceTokens > 0;
    if (result.speechConfidenceMeasured) {
        result.minimumNoSpeechProbability = minimumNoSpeech;
        result.meanTextTokenLogProbability = logProbabilitySum / confidenceTokens;
        if (speech.rejectsWeakNoSpeech(minimumNoSpeech, result.meanTextTokenLogProbability, confidenceTokens,
                                      params.no_speech_thold, params.logprob_thold)) {
            result.text.clear(); result.rejectedNoSpeech = true;
        }
    }
    const auto first = result.text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) result.text.clear();
    else result.text = result.text.substr(first, result.text.find_last_not_of(" \t\r\n") - first + 1);
    if (collectBoundaryEvidence) result.boundaryProbability = boundaries.aligned(result.text);
    result.latencyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return result;
}
}
