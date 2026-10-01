#include "TTSEngine.hpp"
#include <sherpa-onnx/c-api/c-api.h>
#include <chrono>
#include <cmath>
#include <sstream>

namespace lvt {
TTSEngine::TTSEngine(const ModelBundle& bundle) {
    bundle.verify();
    memory_ = MemoryBudget::shared().claim(std::filesystem::file_size(bundle.path(bundle.primary)) * 3 + (128ULL << 20));
    const auto relative = std::filesystem::path(bundle.primary).parent_path();
    const auto model = bundle.path(bundle.primary).string();
    const auto tokens = bundle.verifiedMember((relative / "tokens.txt").string()).string();
    const auto data = bundle.path((relative / "espeak-ng-data").string()).string();
    bundle.verifiedMember((relative / "espeak-ng-data/phontab").string());
    SherpaOnnxOfflineTtsConfig config{};
    config.model.vits.model = model.c_str();
    config.model.vits.tokens = tokens.c_str();
    config.model.vits.data_dir = data.c_str();
    config.model.vits.noise_scale = .667f;
    config.model.vits.noise_scale_w = .8f;
    config.model.vits.length_scale = 1;
    config.model.num_threads = 2;
    config.model.provider = "cpu";
    config.max_num_sentences = 1;
    config.silence_scale = .2f;
    tts_ = SherpaOnnxCreateOfflineTts(&config);
    if (!tts_) throw std::runtime_error("Local voice failed to load");
}
TTSEngine::~TTSEngine() { if (tts_) SherpaOnnxDestroyOfflineTts(tts_); }
VoiceAudio TTSEngine::synthesize(const std::string& text, std::stop_token stop) {
    if (stop.stop_requested()) return {};
    if (text.empty() || text.size() > 8192) throw std::runtime_error("Invalid synthesis text length");
    // Production units originate from bounded speech windows. Reject pathological
    // text before VITS can allocate a minutes-long phoneme/audio tensor.
    std::istringstream words(text); std::string word; std::size_t count = 0;
    while (words >> word) ++count;
    if (count > 96 || text.size() > 1600) throw std::runtime_error("Synthesis unit exceeds the bounded speech contract");
    auto workingMemory = MemoryBudget::shared().claim(256ULL << 20);
    const auto begin = std::chrono::steady_clock::now();
    SherpaOnnxGenerationConfig config{};
    config.speed = 1; config.silence_scale = .2f;
    auto callback = [](const float*, int32_t, float, void* context) -> int32_t {
        return !static_cast<std::stop_token*>(context)->stop_requested();
    };
    auto* raw = SherpaOnnxOfflineTtsGenerateWithConfig(tts_, text.c_str(), &config, callback, &stop);
    auto audio = std::unique_ptr<const SherpaOnnxGeneratedAudio, decltype(&SherpaOnnxDestroyOfflineTtsGeneratedAudio)>(raw, SherpaOnnxDestroyOfflineTtsGeneratedAudio);
    if (stop.stop_requested()) return {};
    if (!audio || !audio->samples || audio->sample_rate < 8000 || audio->sample_rate > 48000 ||
        audio->n <= 0 || audio->n > audio->sample_rate * 30) throw std::runtime_error("Voice inference failed or exceeded 30 seconds");
    VoiceAudio result{{audio->samples, audio->samples + audio->n}, audio->sample_rate,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()};
    double energy = 0;
    for (float sample : result.samples) {
        if (!std::isfinite(sample) || std::abs(sample) > 2) throw std::runtime_error("Invalid synthesized PCM");
        energy += sample * sample;
    }
    if (energy / result.samples.size() < 1e-10) throw std::runtime_error("Voice produced only silence");
    return result;
}
}
