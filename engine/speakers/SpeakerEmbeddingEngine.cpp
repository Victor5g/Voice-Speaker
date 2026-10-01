#include "SpeakerEmbeddingEngine.hpp"
#include <kaldi-native-fbank/csrc/online-feature.h>
#include <array>
#include <cmath>

namespace lvt {
std::vector<float> SpeakerEmbeddingEngine::extract(std::span<const float> pcm, std::stop_token stop) {
    if (stop.stop_requested() || pcm.size() < 12800) return {};
    if (pcm.size() > 16000 * 10) throw std::runtime_error("Embedding window exceeds ten seconds");
    knf::FbankOptions options;
    options.frame_opts.samp_freq = 16000;
    options.frame_opts.frame_length_ms = 25; options.frame_opts.frame_shift_ms = 10;
    options.frame_opts.dither = 0; options.frame_opts.snip_edges = true; options.frame_opts.window_type = "hamming";
    options.mel_opts.num_bins = 80; options.use_energy = false;
    knf::OnlineFbank fbank(options);
    std::vector<float> scaled(pcm.size());
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        if (!std::isfinite(pcm[i])) throw std::runtime_error("Invalid embedding audio");
        scaled[i] = pcm[i] * 32768;
    }
    fbank.AcceptWaveform(16000, scaled.data(), int(scaled.size())); fbank.InputFinished();
    const int frames = fbank.NumFramesReady();
    if (frames < 50) return {};
    std::vector<float> features(std::size_t(frames) * 80);
    std::array<double, 80> means{};
    for (int t = 0; t < frames; ++t) for (int f = 0; f < 80; ++f) {
        features[t * 80 + f] = fbank.GetFrame(t)[f]; means[f] += features[t * 80 + f] / frames;
    }
    for (int t = 0; t < frames; ++t) for (int f = 0; f < 80; ++f) features[t * 80 + f] -= float(means[f]);
    const std::array<std::int64_t, 3> shape{1, frames, 80};
    auto result = model_.run(features, shape, 1024, stop);
    if (stop.stop_requested()) return {};
    if (result.shape.size() != 2 || result.shape[0] != 1 || result.values.size() < 64) throw std::runtime_error("Invalid speaker embedding shape");
    double norm = 0;
    for (float v : result.values) { if (!std::isfinite(v)) throw std::runtime_error("Invalid speaker embedding"); norm += v * v; }
    if (norm < 1e-12) return {};
    for (auto& v : result.values) v /= float(std::sqrt(norm));
    return result.values;
}
}
