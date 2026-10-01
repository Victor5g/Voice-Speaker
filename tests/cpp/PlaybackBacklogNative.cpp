#include "../../engine/conversation/ConversationPipeline.hpp"
#include "CorpusDrainTracker.hpp"
#include <iostream>
#include <set>
int main(){try{
    lvt::PCMPlayback playback;lvt::ConversationPipeline pipeline(playback);
    lvt::ProcessingOptions options;options.sourceLanguage="en";options.targetLanguage="pt";options.identifySpeakers=false;options.maxQueuedSpeechSeconds=3;
    std::atomic<bool> admitted{false};unsigned supplied=0;
    pipeline.start("models",48000,93,[&](lvt::ASRResult& r){
        if(!admitted||supplied>=6)return false;
        r={};r.text="Please send the documents after the meeting. We need to discuss all the details before we begin the project.";
        r.detectedLanguage="en";r.targetLanguage="pt-BR";r.speaker=1;r.utterance=++supplied;r.final=true;r.hostTime=lvt::hostclock::now();return true;
    },lvt::StreamingPolicy::production(),options);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    while(std::chrono::steady_clock::now()<deadline){auto s=pipeline.snapshot();if(s.translationState==3||s.ttsState==3)throw std::runtime_error(s.error);if(s.translationState==2&&s.ttsState==2)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    if(pipeline.snapshot().translationState!=2||pipeline.snapshot().ttsState!=2)throw std::runtime_error("Initialization timed out");
    admitted=true;std::set<std::uint64_t> texts,skips,queued,played; lvt::test::CorpusDrainTracker drain;std::array<float,960> out{};double energy=0;bool rendering=false;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    while(std::chrono::steady_clock::now()<deadline){
        lvt::ConversationEvent e;while(pipeline.pop(e)){
            drain.observe(e.result.identity.segment,e.state);
            if(e.state=="translated")texts.insert(e.result.identity.segment);
            if(e.state=="queued")queued.insert(e.result.identity.segment);
            if(e.state=="audio-skipped") {if(e.result.translated.empty())throw std::runtime_error("Skipped audio lost text");skips.insert(e.result.identity.segment);}
            if(e.state=="played"&&!played.insert(e.result.identity.segment).second)throw std::runtime_error("Duplicate playback");
            if(e.state=="error"||e.state=="dropped"||e.state=="unavailable")throw std::runtime_error(e.error);
        }
        // Deliberately unavailable output reproduces a congested sink. Once all
        // six inputs have a decision, render the admitted unit without cuts.
        if(queued.size()+skips.size()==6)rendering=true;
        if(rendering){playback.render(out.data(),out.size(),48000);for(auto v:out)energy+=v*v;}
        if(rendering&&played==queued&&drain.idle())break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto s=pipeline.snapshot();
    if(texts.size()!=6||queued.size()!=1||skips.size()!=5||played!=queued||energy<=0||!drain.idle()||s.audioSkipped!=5)
        throw std::runtime_error("Congested sink did not preserve text and one whole playback");
    pipeline.requestStop();playback.render(out.data(),out.size(),48000);if(std::any_of(out.begin(),out.end(),[](float x){return x!=0;}))throw std::runtime_error("STOP leaked PCM");pipeline.stop();
    std::cout<<nlohmann::json{{"result","PASS"},{"translated",texts.size()},{"wholeAudioSkipped",skips.size()},{"wholeAudioPlayed",played.size()},
        {"scope","Real translation/TTS with deliberately blocked sink, then accelerated PCM rendering; functional bound test, not realtime latency"}}.dump()<<std::endl;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
