#pragma once
#include "../common/ModelAssets.hpp"
#include <fstream>

namespace lvt {
enum class ASRBackend { metal = 0, coreMLANE = 1, coreMLAll = 2, coreMLGPU = 3 };
inline ASRBackend parseASRBackend(const std::string& value) {
    if (value == "metal") return ASRBackend::metal;
    if (value == "coreml-ane") return ASRBackend::coreMLANE;
    if (value == "coreml-all") return ASRBackend::coreMLAll;
    if (value == "coreml-gpu") return ASRBackend::coreMLGPU;
    throw std::runtime_error("Unknown ASR backend");
}
struct ASRConfiguration {
    ASRBackend backend{ASRBackend::metal};
    ModelBundle encoder;
    std::string encoderDirectory;
    bool reuseEncoder{true};
    void verify() const {
        if (backend == ASRBackend::metal) return;
        if (int(backend) < 1 || int(backend) > 3) throw std::runtime_error("Invalid ASR backend");
        encoder.verify();
        const auto directory = encoder.path(encoderDirectory);
        if (!std::filesystem::is_directory(directory) || directory.extension() != ".mlmodelc")
            throw std::runtime_error("Invalid Core ML encoder directory");
        // Core ML may read any file in this directory: require an exact inventory.
        std::unordered_set<std::string> expected;
        for (const auto& asset : encoder.files) expected.insert(encoder.path(asset.filename).string());
        std::size_t count = 0;
        for (const auto& asset : std::filesystem::recursive_directory_iterator(directory)) {
            if (asset.is_symlink()) throw std::runtime_error("Core ML model symlink rejected");
            if (asset.is_directory()) continue;
            if (!asset.is_regular_file() || !expected.contains(asset.path().string()))
                throw std::runtime_error("Unverified Core ML encoder asset");
            ++count;
        }
        if (count != expected.size()) throw std::runtime_error("Incomplete Core ML encoder inventory");
    }
};
inline ASRConfiguration asrConfiguration(const std::string& resources, const std::string& modelPath,
                                       const std::string& modelHash, const std::string& overrideBackend = "") {
    ASRConfiguration result;
    if (resources.empty()) {
        if (!overrideBackend.empty() && overrideBackend != "metal") throw std::runtime_error("Core ML resources required");
        return result;
    }
    nlohmann::json selection;
    std::ifstream(std::filesystem::path(resources) / "selection.json") >> selection;
    result.backend = parseASRBackend(overrideBackend.empty() ? selection.value("asrBackend", "metal") : overrideBackend);
    if (result.backend == ASRBackend::metal) return result;
    nlohmann::json manifest;
    std::ifstream(std::filesystem::path(resources) / "manifest.json") >> manifest;
    const auto asr = std::find_if(manifest.at("models").begin(), manifest.at("models").end(), [&](const auto& entry) {
        return entry.at("type") == "asr" && entry.at("sha256") == modelHash &&
            std::filesystem::weakly_canonical(std::filesystem::path(resources) / entry.at("filename").template get<std::string>()) ==
            std::filesystem::weakly_canonical(modelPath);
    });
    if (asr == manifest.at("models").end()) throw std::runtime_error("Core ML base model mismatch");
    const auto encoder = std::find_if(manifest.at("models").begin(), manifest.at("models").end(), [&](const auto& entry) {
        return entry.at("type") == "asr-encoder" && entry.value("asrSHA256", "") == modelHash;
    });
    if (encoder == manifest.at("models").end()) throw std::runtime_error("Verified Core ML encoder missing");
    result.encoder = ModelBundle::fromJSON(resources, *encoder);
    result.encoderDirectory = encoder->at("directory");
    return result;
}
}
