#include "../../engine/speakers/SpeakerEmbeddingEngine.hpp"
#include "../../engine/speakers/SpeakerTracker.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <chrono>
#include <iostream>
#include <map>
int main() {
    try {
        nlohmann::json entries;
        std::ifstream("models/speaker-assets.json") >> entries;
        for (const auto& entry : entries) {
            if (entry.at("type") != "embedding" && entry.at("type") != "embedding-reference") continue;
            lvt::SpeakerEmbeddingEngine engine(lvt::ModelBundle::fromJSON("models", entry));
            std::map<std::string, std::vector<float>> first, second;
            for (const std::string name : {"ptbr", "enus", "engb"}) {
                auto pcm = lvt::readWav16k("tests/data/generated/" + name + ".wav");
                const auto begin = std::chrono::steady_clock::now();
                first[name] = engine.extract({pcm.data(), std::min<std::size_t>(pcm.size(), 48000)});
                second[name] = engine.extract({pcm.data() + pcm.size() - std::min<std::size_t>(pcm.size(), 48000), std::min<std::size_t>(pcm.size(), 48000)});
                if (first[name].empty() || first[name].size() != second[name].size()) throw std::runtime_error("Speaker embeddings missing");
                std::cout << nlohmann::json{{"model",entry["name"]},{"fixture",name},{"dimensions",first[name].size()},
                    {"twoExtractionsMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()},
                    {"sameVoiceCosine",lvt::SpeakerTracker::similarity(first[name],second[name])}}.dump() << std::endl;
            }
            for (const auto& a : first) for (const auto& b : first) if (a.first < b.first)
                std::cout << nlohmann::json{{"model",entry["name"]},{"voiceA",a.first},{"voiceB",b.first},
                    {"differentVoiceCosine",lvt::SpeakerTracker::similarity(a.second,b.second)}}.dump() << std::endl;
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
