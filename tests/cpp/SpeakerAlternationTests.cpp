#include "../../engine/speakers/SpeakerEmbeddingEngine.hpp"
#include "../../engine/speakers/SpeakerTracker.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/common/ModelHash.hpp"
#include <array>
#include <iostream>

// Uses actual native embeddings and the production tracker. Default input is
// explicitly synthetic. A human run requires a separate provenance manifest
// with verified distinct identity references; file names are never identities.
int main(int argc, char** argv) { try {
    if (argc < 2 || argc > 3) throw std::runtime_error("Usage: speaker-alternation-tests MODEL_ROOT [IDENTITY_MANIFEST]");
    const std::string root = argv[1];
    const std::string input = argc == 3 ? argv[2] : "tests/data/speaker-alternation-inputs.json";
    nlohmann::json models, manifest;
    std::ifstream(root + "/manifest.json") >> models;
    std::ifstream(input) >> manifest;
    const auto sourceType = manifest.at("sourceType").get<std::string>();
    if (sourceType != "human-verified" && sourceType != "synthetic-development") throw std::runtime_error("Input identity provenance is not explicit");
    const auto& records = manifest.at("recordings");
    if (records.size() != 2) throw std::runtime_error("Exactly two verified identity references required");
    std::array<std::vector<float>, 2> pcm;
    std::array<std::string, 2> references;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& record = records.at(i);
        if (!record.value("speakerReferenceVerified", false) || !record.contains("speakerReference") || record.at("speakerReference").is_null())
            throw std::runtime_error("Unverified human identity cannot be inferred from a file name");
        references[i] = record.at("speakerReference").get<std::string>();
        if (references[i].empty() || record.value("referenceBasis", "").empty()) throw std::runtime_error("Missing identity evidence");
        if (sourceType == "human-verified" && (record.value("license", "").empty() || record.value("source", "").empty()))
            throw std::runtime_error("Human recording requires explicit source and license");
        const auto file = record.at("filename").get<std::string>();
        lvt::verifyModelHash(file, record.at("sha256"));
        pcm[i] = lvt::readWav16k(file);
        if (pcm[i].size() < 38400 || pcm[i].size() > 160000) throw std::runtime_error("Use a declared 2.4-10 s single-speaker fixture, without implicit cropping");
    }
    if (references[0] == references[1]) throw std::runtime_error("Alternation requires distinct independently verified references");
    const auto found = std::find_if(models.at("models").begin(), models.at("models").end(), [](const auto& entry) { return entry.at("type") == "embedding"; });
    if (found == models.at("models").end()) throw std::runtime_error("Missing embedding model");
    lvt::SpeakerEmbeddingEngine embeddings(lvt::ModelBundle::fromJSON(root, *found));
    lvt::SpeakerTracker tracker;
    std::array<std::uint32_t, 2> known{};
    std::array<unsigned, 2> finals{};
    std::uint64_t frame = 0;
    for (unsigned step = 0; step < 8; ++step) {
        const auto person = step % 2;
        auto audio = pcm[person];
        const float gain = step < 2 ? 1.f : step % 3 == 0 ? .2f : .65f;
        for (auto& sample : audio) sample *= gain;
        if (step == 6) frame += 16000ULL * 60;
        const auto embedding = embeddings.extract(audio);
        if (embedding.empty()) throw std::runtime_error("Native embedding missing");
        const auto id = tracker.identify(embedding, frame, true, false, 0, true, true, frame + audio.size());
        ++finals[person];
        if (id) {
            if (known[person] && id != known[person]) throw std::runtime_error("Confirmed voice identity changed across gain/pause variation");
            known[person] = id;
        }
        if (finals[person] >= 2 && !id) throw std::runtime_error("Repeated known source remained unknown; do not hide failure by disabling enrollment");
        if (known[0] && known[1] && known[0] == known[1]) throw std::runtime_error("Two verified sources collapsed into one identity");
        if (tracker.count() > 2) throw std::runtime_error("Gain/pause created an extra permanent identity");
        std::cout << nlohmann::json{{"sourceType", sourceType}, {"step", step}, {"reference", references[person]},
            {"speaker", id}, {"speakerCount", tracker.count()}, {"gain", gain}, {"firstFrame", frame},
            {"scope", "Real native embeddings/tracker, replayed sources; no live call, TTS voice binding or natural conversation acceptance"}}.dump() << '\n';
        frame += audio.size() + 16000;
    }
    if (tracker.count() != 2 || !known[0] || !known[1]) throw std::runtime_error("Two voices were not preserved");
    std::cout << nlohmann::json{{"result", "PASS"}, {"sourceType", sourceType}, {"verifiedIdentities", 2}}.dump() << '\n';
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
