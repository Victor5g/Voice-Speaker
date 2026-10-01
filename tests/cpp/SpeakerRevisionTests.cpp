#include "../../engine/speakers/OnlineDiarizer.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/common/ModelHash.hpp"
#include <iostream>
int main() {
    try {
        nlohmann::json manifest; std::ifstream("models/manifest.json") >> manifest;
        auto bundle = [&](const char* type) {
            for (const auto& e : manifest["models"]) if (e["type"] == type) return lvt::ModelBundle::fromJSON("models",e);
            throw std::runtime_error("Missing model");
        };
        const std::string file = "tests/data/human/en_us-15618717836108767541.wav";
        nlohmann::json corpus; std::ifstream("tests/data/human/manifest.json") >> corpus;
        const auto record = std::find_if(corpus.at("recordings").begin(), corpus.at("recordings").end(), [&](const auto& value) { return value.at("filename") == file; });
        if (record == corpus.at("recordings").end()) throw std::runtime_error("Human fixture lacks provenance");
        lvt::verifyModelHash(file, record->at("sha256"));
        auto pcm = lvt::readWav16k(file);
        for (bool trimSpeakerEdges : {false,true}) {
        lvt::OnlineDiarizer diarizer(bundle("embedding"),bundle("overlap"),bundle("separation"),trimSpeakerEdges);
        // One recording, replayed with deterministic gain transformations. This
        // is not proof across different human utterances or natural intonation.
        constexpr std::uint64_t starts[] = {0,192000,416000,16000ULL*60,16000ULL*120};
        for (int pass=0;pass<5;++pass) {
            for (std::size_t count=24000;;count=std::min(count+24000,pcm.size())) {
                lvt::SpeechSegment segment{{pcm.begin(),pcm.begin()+count},16000,starts[pass],0};
                segment.utterance=pass+1;segment.final=count==pcm.size();
                if(pass) for(std::size_t i=0;i<count;++i) segment.samples[i]*=pass==1?.2f:.15f+.85f*float(i)/pcm.size();
                const auto result=diarizer.process(std::move(segment));
                if(diarizer.speakerCount()>1 || result.size()>1 || diarizer.metrics().separationCalls)
                    throw std::runtime_error("A volume-varying single voice created another source/identity");
                for (const auto& unit : result) if (unit.speaker > 1) throw std::runtime_error("Same recording changed confirmed speaker ID");
                if(count==pcm.size()) {
                    if (pass >= 1 && (diarizer.speakerCount()!=1 || result.empty() || result.front().speaker!=1))
                        throw std::runtime_error("Independent finals failed to establish/preserve the same participant");
                    std::cout << nlohmann::json{{"fixture",file},{"pass",pass},{"trimSpeakerEdges",trimSpeakerEdges},
                        {"speakerCount",diarizer.speakerCount()},{"speaker",result.empty()?0:result.front().speaker},
                        {"scope","One human recording replay with controlled gain and frame gaps; not natural speaker-change acceptance"}}.dump() << '\n';
                    break;
                }
            }
        }
        if(diarizer.speakerCount()!=1)throw std::runtime_error("Regression was hidden by disabling identity tracking");
        }
        std::cout << "PASS single human voice across revisions and volume changes retains one identity without separation\n";
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
