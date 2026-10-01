#include "../../engine/asr/ASRBackend.hpp"
#include <iostream>
#include <unistd.h>

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
template<class F> bool rejects(F action) { try { action(); return false; } catch (const std::exception&) { return true; } }
int main() {
    std::string pattern = (std::filesystem::current_path() / "build/coreml-test.XXXXXX").string();
    const char* created = mkdtemp(pattern.data());
    if (!created) return 2;
    const std::filesystem::path root(created);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};
    try {
        using namespace lvt;
        const auto directory = root / "encoder.mlmodelc";
        std::filesystem::create_directory(directory);
        std::ofstream(directory / "model.mil") << "verified test model";
        const auto hash = sha256File((directory / "model.mil").string());
        ASRConfiguration config{ASRBackend::coreMLANE,
            ModelBundle{root, {{"encoder.mlmodelc/model.mil", hash, 19}}, "encoder.mlmodelc/model.mil"}, "encoder.mlmodelc"};
        config.verify();
        require(true, "Exact Core ML inventory verifies before initialization");
        require(parseASRBackend("coreml-ane") == ASRBackend::coreMLANE && parseASRBackend("coreml-gpu") == ASRBackend::coreMLGPU,
                "Backend modes remain distinct");
        require(rejects([] { parseASRBackend("automatic-unknown"); }), "Unknown backend fails explicitly");
        require(asrConfiguration("", "", "").backend == ASRBackend::metal, "No implicit Core ML discovery");
        require(rejects([] { asrConfiguration("", "", "", "coreml-ane"); }), "Core ML cannot bypass resource verification");
        std::ofstream(directory / "extra.bin") << "unexpected";
        require(rejects([&] { config.verify(); }), "Unlisted auxiliary files rejected");
        std::filesystem::remove(directory / "extra.bin");
        std::filesystem::create_symlink("model.mil", directory / "alias");
        require(rejects([&] { config.verify(); }), "Encoder symlinks rejected");
        std::filesystem::remove(directory / "alias");
        std::ofstream(directory / "model.mil") << "corrupt! test model!";
        require(rejects([&] { config.verify(); }), "Changed model bytes rejected");
        std::filesystem::remove(directory / "model.mil");
        require(rejects([&] { config.verify(); }), "Missing encoder fails instead of falling back");
        config.encoderDirectory = "../escaped.mlmodelc";
        require(rejects([&] { config.encoder.path(config.encoderDirectory); }), "Encoder directory traversal rejected");
        nlohmann::json selection{{"asrBackend", "coreml-ane"}};
        std::ofstream(root / "selection.json") << selection;
        std::ofstream(root / "manifest.json") << nlohmann::json{{"models", nlohmann::json::array()}};
        require(rejects([&] { asrConfiguration(root.string(), "wrong.bin", std::string(64, '0')); }), "Encoder cannot be paired with an unlisted ASR model");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
