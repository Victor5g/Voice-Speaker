#include "../../engine/audio/AudioCore.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "CorpusDrainTracker.hpp"
#include "TimelineJSON.hpp"
#include <iostream>
#include <set>
int main(){try{
    nlohmann::json selection;std::ifstream("models/selection.json")>>selection;
    lvt::AudioCore cores[2];lvt::ProcessingOptions options[2];
    options[0].sourceLanguage="pt";options[0].targetLanguage="en";options[0].identifySpeakers=false;options[0].speechOutput=false;options[0].voiceIndex=1;options[0].endpointSilenceMs=200;
    options[1].sourceLanguage="en";options[1].targetLanguage="pt";options[1].identifySpeakers=false;options[1].voiceIndex=2;options[1].endpointSilenceMs=600;
    for(int i=0;i<2;++i){cores[i].configureASR("models/"+selection.at("filename").get<std::string>(),selection.at("sha256"));cores[i].configureConversation("models",48000);cores[i].configureProcessing(options[i]);cores[i].start();}
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    auto ready=[&]{for(int i=0;i<2;++i){auto s=cores[i].snapshot();if(s.asr.state==3||s.conversation.translationState==3||s.conversation.ttsState==3)throw std::runtime_error(s.asr.error+s.conversation.error);if(s.asr.state!=2||s.conversation.translationState!=2||(i&&s.conversation.ttsState!=2))return false;}return true;};
    while(!ready()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if(!ready())throw std::runtime_error("Model initialization timeout");
    std::vector<float> pcm[]={lvt::readWav16k("tests/data/generated/ptbr.wav"),lvt::readWav16k("tests/data/generated/enus.wav")};
    std::size_t cursor[2]{};lvt::test::CorpusDrainTracker drain[2];std::set<std::uint64_t> translated[2],finished[2],sessions;
    unsigned voices=0;double energy[2]{};bool complete=false;
    std::array<float,320> input{};std::array<float,960> output{};
    auto next=std::chrono::steady_clock::now();until=next+std::chrono::seconds(120);
    while(std::chrono::steady_clock::now()<until){
        for(int i=0;i<2;++i){input.fill(0);auto count=std::min(input.size(),pcm[i].size()-cursor[i]);std::copy_n(pcm[i].data()+cursor[i],count,input.data());cursor[i]+=count;
            auto now=lvt::hostclock::now();cores[i].capture(input.data(),input.size(),16000,now);cores[i].render(output.data(),output.size(),48000,now);for(float x:output)energy[i]+=x*x;
            lvt::ConversationEvent event;
            while(cores[i].popConversationEvent(event)){
                auto& r=event.result;drain[i].observe(r.identity.segment,event.state);
                if(event.state=="error"||event.state=="unavailable"||event.state=="dropped")throw std::runtime_error(event.error);
                if(event.state=="translated"){
                    if(r.sourceLanguage!=options[i].sourceLanguage||r.targetLanguage!=(i?"pt-BR":"en")||r.identity.speaker!=1)throw std::runtime_error("Crossed route settings");
                    translated[i].insert(r.identity.segment);sessions.insert(r.identity.session);
                }
                if(event.state==(i?"played":"text-only"))if(!finished[i].insert(r.identity.segment).second)throw std::runtime_error("Duplicate completion");
                if(!event.voice.empty()){if(!i||event.voice!="pt_BR-edresson-low")throw std::runtime_error("Wrong manual voice");++voices;}
                std::cout<<nlohmann::json{{"route",i},{"state",event.state},{"original",r.original},{"translation",r.translated},{"voice",event.voice},{"timing",lvt::test::timelineJSON(event)}}.dump()<<std::endl;
            }
        }
        complete=true;
        for(int i=0;i<2;++i){auto s=cores[i].snapshot();complete&=cursor[i]==pcm[i].size()&&!s.vad.speaking&&s.asr.processed==s.vad.segments&&s.conversation.coordinatedUtterances==s.asr.processed&&!s.conversation.pendingEvents&&drain[i].idle()&&!translated[i].empty()&&translated[i]==finished[i];}
        if(complete)break;next+=std::chrono::milliseconds(20);std::this_thread::sleep_until(next);
    }
    if(!complete||sessions.size()!=2||energy[0]!=0||energy[1]<=0||!voices)throw std::runtime_error("Routes did not finish independently");
    for(int i=0;i<2;++i){auto s=cores[i].snapshot();
        if(s.asr.languageDetections||s.asr.diarization.loaded||s.asr.diarization.embeddingMs||s.asr.diarization.overlapMs||s.playbackDropped||s.conversation.eventDrops)throw std::runtime_error("Disabled stages executed or queues lost data");
        if(!i&&(s.conversation.synthesized||s.conversation.ttsState||!s.conversation.cachedVoices.empty()))throw std::runtime_error("Text-only path loaded or executed TTS");
        cores[i].requestStop();cores[i].render(output.data(),output.size(),48000);if(std::any_of(output.begin(),output.end(),[](float x){return x!=0;}))throw std::runtime_error("STOP leaked PCM");cores[i].stop();lvt::ConversationEvent late;if(cores[i].popConversationEvent(late))throw std::runtime_error("STOP leaked events");
    }
    std::cout<<nlohmann::json{{"result","PASS"},{"translated",{translated[0].size(),translated[1].size()}},{"scope","Synthetic WAVs, two native routes with different options; no physical call acceptance"}}.dump()<<std::endl;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
