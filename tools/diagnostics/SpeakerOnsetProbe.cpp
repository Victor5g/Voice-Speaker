#include "../../engine/speakers/SpeakerEmbeddingEngine.hpp"
#include "../../engine/speakers/SpeakerTracker.hpp"
#include "../../engine/speakers/OverlapDetector.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <iostream>
#include <bit>
// Development comparison only; no enrollment threshold or production PCM changes.
int main() { try {
    nlohmann::json manifest; std::ifstream("models/manifest.json") >> manifest;
    auto bundle = [&](const char* type) {
        for (const auto& entry : manifest["models"]) if (entry["type"] == type) return lvt::ModelBundle::fromJSON("models",entry);
        throw std::runtime_error("Missing model");
    };
    lvt::SpeakerEmbeddingEngine engine(bundle("embedding"));
    lvt::OverlapDetector segmenter(bundle("overlap"));
    auto pcm=lvt::readWav16k("tests/data/conversation_ptbr_en.wav");
    for (const auto origin : {8000u,182400u}) for (const auto count : {32000u,40000u,48000u,56000u,64000u,72000u,80000u}) {
        if (origin+count>pcm.size()) continue;
        const auto input=std::span<const float>(pcm).subspan(origin,count);
        auto regions=segmenter.analyze(input);
        std::size_t start=count,end=0; bool overlap=false;
        for (const auto& region : regions) if (region.localMask) {
            start=std::min(start,region.start);end=std::max(end,region.end);
            overlap |= std::popcount(region.localMask)>1;
        }
        for (bool trim : {false,true}) {
            if (trim && end<=start)continue;
            const auto samples=trim?input.subspan(start,end-start):input;
            auto embedding=engine.extract(samples);
            auto left=engine.extract(samples.first(samples.size()/2));
            auto right=engine.extract(samples.subspan(samples.size()/2));
            const auto lr=lvt::SpeakerTracker::similarity(left,right);
            const auto fl=lvt::SpeakerTracker::similarity(embedding,left),fr=lvt::SpeakerTracker::similarity(embedding,right);
            std::cout<<nlohmann::json{{"origin",origin},{"inputSamples",count},{"trim",trim},
                {"speechStart",start},{"speechEnd",end},{"samples",samples.size()},{"overlap",overlap},
                {"leftRight",lr},{"fullLeft",fl},{"fullRight",fr},
                {"passesExistingAgreement",!overlap && samples.size()>=38400 && lr>=.68 && fl>=.68 && fr>=.68}}.dump()<<std::endl;
        }
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
