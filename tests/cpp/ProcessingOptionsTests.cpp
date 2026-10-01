#include "../../engine/common/ProcessingOptions.hpp"
#include "../../engine/tts/SessionVoices.hpp"
#include "../../engine/vad/VadSegmenter.hpp"
#include "CorpusDrainTracker.hpp"
#include <iostream>
int main(){try{
    unsigned n=0;auto check=[&](bool ok,const char* message){if(!ok)throw std::runtime_error(message);++n;};
    auto a=lvt::ProcessingOptions::fromJSON(R"({"sourceLanguage":"pt","targetLanguage":"en","identifySpeakers":false,"speechOutput":false,"voiceIndex":2,"endpointSilenceMs":200})");
    auto b=lvt::ProcessingOptions::fromJSON(R"({"sourceLanguage":"en","targetLanguage":"pt","voiceIndex":1,"endpointSilenceMs":800})");
    check(!a.identifySpeakers&&!a.speechOutput&&b.identifySpeakers&&b.speechOutput,"Options are independent route values");
    check(a.sourceLanguage=="pt"&&b.sourceLanguage=="en"&&a.endpointSilenceMs==200&&b.endpointSilenceMs==800,"Opposite languages and pause settings remain separate");
    for(const auto* bad:{R"({"sourceLanguage":"de"})",R"({"sourceLanguage":"pt","targetLanguage":"pt"})",R"({"voiceIndex":3})",R"({"endpointSilenceMs":0})",R"({"endpointSilenceMs":1001})",R"({"maxQueuedSpeechSeconds":0})"}){
        bool rejected=false;try{lvt::ProcessingOptions::fromJSON(bad);}catch(...){rejected=true;}check(rejected,"Invalid processing options fail before starting workers");
    }
    for(const auto* key:{"voiceIndex","endpointSilenceMs"}) {
        for(const auto* value:{"4294967296","4294967596","18446744073709551615","-1","0.5","300.0","true","\"1\"","null"}) {
            bool rejected=false;
            try{lvt::ProcessingOptions::fromJSON(std::string("{\"")+key+"\":"+value+"}");}catch(...){rejected=true;}
            check(rejected,"Integer settings reject overflow, fractions and other JSON types before conversion");
        }
    }
    for(const auto* valid:{"{}",R"({"sourceLanguage":"en"})",R"({"targetLanguage":"pt"})",R"({"sourceLanguage":"en","targetLanguage":"pt"})"}) {
        auto options=lvt::ProcessingOptions::fromJSON(valid);
        options.validateForSourceLanguage("en");
        check(true,"Legacy English route remains compatible with automatic and matching manual options");
    }
    for(const auto* invalid:{R"({"sourceLanguage":"pt"})",R"({"sourceLanguage":"pt","targetLanguage":"en"})",R"({"targetLanguage":"en"})"}) {
        bool rejected=false;
        try{lvt::ProcessingOptions::fromJSON(invalid).validateForSourceLanguage("en");}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Legacy route rejects contradictory source or identical effective destination");
    }
    bool rejectedLegacy=false;
    try{lvt::ProcessingOptions{}.validateForSourceLanguage("de");}catch(const std::invalid_argument&){rejectedLegacy=true;}
    check(rejectedLegacy,"Unsupported legacy source fails before loading translation models");
    lvt::SessionVoices voices,otherRoute;
    check(voices.voice(1,"en")=="en_US-lessac-medium","First participant gets fixed English voice");
    check(voices.voice(2,"pt")=="pt_BR-jeff-medium","Second participant gets separate pair");
    check(voices.voice(0,"en")=="en_US-ryan-medium","Unknown evidence does not arbitrarily reset the last voice");
    check(voices.voice(1,"pt-BR")=="pt_BR-faber-medium","Returning participant retains pair across language changes");
    check(otherRoute.voice(0,"pt")=="pt_BR-faber-medium","Other direction has independent voice history");
    for(unsigned id:{0u,1u,2u,64u})check(voices.voice(id,"pt",false,2)=="pt_BR-edresson-low","Manual voice ignores participant IDs");
    for(unsigned voicedBlocks:{10u,350u})for(unsigned pause:{200u,300u,800u}){
        lvt::VadSegmenter segmenter(true,pause);std::array<float,320> frame{};std::uint64_t position=0;
        for(unsigned i=0;i<voicedBlocks;++i){segmenter.process(frame,true,16000,position,0);position+=320;}
        unsigned elapsed=0;std::optional<lvt::SpeechSegment> result;
        while(!result&&elapsed<1100){result=segmenter.process(frame,false,16000,position,0);position+=320;elapsed+=20;}
        check(result.has_value()&&elapsed==pause,"Pause changes real segment endpoint by configured amount");
        check(result->timing.endpointReason==lvt::EndpointReason::silence,"Configured endpoint retains silence classification");
    }
    lvt::test::CorpusDrainTracker drain;
    for(const auto* state:{"text-only","audio-skipped"}){drain.observe(1,"recognized");drain.observe(1,state);check(drain.idle(),"Non-spoken terminal states drain accepted translation");}
    std::cout<<"RESULT "<<n<<" passed, 0 failed\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
