#include "../../engine/tts/VoiceCache.hpp"
#include "../../engine/tts/TTSEngine.hpp"
#include <chrono>
#include <iostream>
int main(int argc,char** argv) { try {
    if(argc!=2) throw std::runtime_error("Usage: voice-cache-benchmark MODEL_ROOT");
    nlohmann::json manifest; std::ifstream(std::filesystem::path(argv[1])/"manifest.json")>>manifest;
    const std::string voice="en_US-lessac-medium";
    auto entry=std::find_if(manifest["models"].begin(),manifest["models"].end(),[&](const auto& row){return row.value("voice","")==voice;});
    if(entry==manifest["models"].end()) throw std::runtime_error("Voice missing");
    auto bundle=lvt::ModelBundle::fromJSON(argv[1],*entry);
    lvt::MemoryBudget::shared().setPressure(1);
    lvt::VoiceCache<lvt::TTSEngine> cache;
    auto factory=[&]{return std::make_unique<lvt::TTSEngine>(bundle);};
    cache.get(voice,factory,true).engine.synthesize("Warm up.");
    const char* phrases[]={"Thank you.","Yes.","No.","This is an unboxing.","Please send your feedback.","The meeting starts at nine."};
    for(unsigned i=0;i<30;++i) {
        auto begin=std::chrono::steady_clock::now();
        auto access=cache.get(voice,factory,true);
        auto loaded=std::chrono::steady_clock::now();
        auto audio=access.engine.synthesize(phrases[i%6]);
        auto end=std::chrono::steady_clock::now();
        std::cout<<nlohmann::json{{"iteration",i},{"voice",voice},{"text",phrases[i%6]},{"loaded",access.loaded},
            {"loadMs",std::chrono::duration<double,std::milli>(loaded-begin).count()},
            {"totalMs",std::chrono::duration<double,std::milli>(end-begin).count()},
            {"ttsMs",audio.latencyMs},{"samples",audio.samples.size()},
            {"footprintBytes",lvt::MemoryBudget::physicalFootprint()},{"pressure",1}}.dump()<<std::endl;
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
