#include "../../engine/speakers/OnlineDiarizer.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <iostream>
#include <set>
int main() {
    try {
        nlohmann::json manifest, human;
        std::ifstream("models/manifest.json") >> manifest;
        std::ifstream("tests/data/human/manifest.json") >> human;
        auto bundle = [&](const char* type) {
            for (const auto& entry : manifest["models"]) if (entry["type"] == type) return lvt::ModelBundle::fromJSON("models", entry);
            throw std::runtime_error("Missing model");
        };
        std::vector<std::string> files{"tests/data/generated/ptbr.wav", "tests/data/generated/enus.wav"};
        for (const auto& entry : human["recordings"]) files.push_back(entry["filename"]);
        for (const auto& file : files) {
            auto pcm = lvt::readWav16k(file); pcm.resize(std::min<std::size_t>(pcm.size(), 156800));
            lvt::OnlineDiarizer diarizer(bundle("embedding"), bundle("overlap"), bundle("separation"));
            for (int pass = 0; pass < 3; ++pass) {
                for (std::size_t count = 24000; ; count = std::min(count + 24000, pcm.size())) {
                    lvt::SpeechSegment segment{{pcm.begin(), pcm.begin() + count}, 16000, std::uint64_t(pass * 192000), 0};
                    segment.utterance = pass + 1; segment.revision = count / 24000; segment.final = count == pcm.size();
                    for (std::size_t i = 0; i < count; ++i) {
                        const float gain = pass == 0 ? 1 : pass == 1 ? .2f : .15f + .85f * float(i) / pcm.size();
                        segment.samples[i] *= gain;
                    }
                    const auto output = diarizer.process(std::move(segment));
                    std::vector<unsigned> ids; for (const auto& unit : output) ids.push_back(unit.speaker);
                    const auto m = diarizer.metrics();
                    std::cout << nlohmann::json{{"file",file},{"pass",pass},{"samples",count},{"final",count==pcm.size()},
                        {"speakers",diarizer.speakerCount()},{"ids",ids},{"units",output.size()},{"diarizationMs",m.latencyMs},
                        {"overlapMs",m.overlapMs},{"embeddingMs",m.embeddingMs},{"overlapPercent",m.overlapPercent},{"separationCalls",m.separationCalls}}.dump() << std::endl;
                    if (count == pcm.size()) break;
                }
            }
        }
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
