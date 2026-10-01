#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/tts/TTSEngine.hpp"
#include "../../engine/audio/SincResampler.hpp"
#include "../../engine/common/ModelHash.hpp"
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>

static std::string compact(std::string text) {
    for (std::size_t at=0;(at=text.find("Ã",at))!=std::string::npos;at+=2) text.replace(at,2,"ã");
    std::string value;
    for (unsigned char c:text) if(c>=128||std::isalnum(c)) value+=char(c<128?std::tolower(c):c);
    return value;
}

// Development-only geometry experiment. Every condition uses the exact same
// synthesized PCM and an independent native Whisper state with no prior decode.
// No production inference parameters or word-specific rules are changed.
int main(int argc,char** argv) { try {
    if(argc!=2)throw std::runtime_error("Usage: asr-short-speech-probe MODEL_ROOT");
    const std::string root=argv[1];
    nlohmann::json selection,manifest;
    std::ifstream(root+"/selection.json")>>selection;
    std::ifstream(root+"/manifest.json")>>manifest;
    const std::string model=root+"/"+selection.at("filename").get<std::string>();
    lvt::WhisperASR weights(model,selection.at("sha256"));
    const auto directory=std::filesystem::path("artifacts/realtime-improvements/short-speech-probe")/("run-"+std::to_string(lvt::hostclock::now()));
    std::filesystem::create_directories(directory);
    std::map<std::string,std::pair<unsigned,unsigned>> counts;
    unsigned index=0;
    for(const std::string language:{"pt","en"}) {
        const std::string voice=language=="pt"?"pt_BR-faber-medium":"en_US-lessac-medium";
        const auto bundle=std::find_if(manifest.at("models").begin(),manifest.at("models").end(),[&](const auto& entry){return entry.value("type","")=="tts"&&entry.value("voice","")==voice;});
        if(bundle==manifest.at("models").end())throw std::runtime_error("Missing voice");
        lvt::TTSEngine tts(lvt::ModelBundle::fromJSON(root,*bundle));
        const auto words=language=="pt"?std::vector<std::string>{"Obrigado.","Sim.","Não.","Ok."}:std::vector<std::string>{"Thanks."};
        for(const auto& word:words) {
            const auto generated=tts.synthesize(word);
            lvt::SincResampler resampler;
            const auto pcm=resampler.to16k(generated.samples,generated.sampleRate);
            const auto fixture=directory/("generated-"+std::to_string(index++)+".f32");
            {std::ofstream file(fixture,std::ios::binary);file.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*sizeof(float));if(!file)throw std::runtime_error("Failed to retain experimental PCM");}
            const auto hash=lvt::sha256File(fixture.string());
            const auto first=std::find_if(pcm.begin(),pcm.end(),[](float value){return std::abs(value)>.001f;});
            const auto last=std::find_if(pcm.rbegin(),pcm.rend(),[](float value){return std::abs(value)>.001f;});
            if(first==pcm.end())throw std::runtime_error("Generated fixture empty");
            const auto begin=std::size_t(first-pcm.begin())>320?std::size_t(first-pcm.begin())-320:0;
            const auto end=std::min(pcm.size(),pcm.size()-std::size_t(last-pcm.rbegin())+320);
            for(bool trimmed:{false,true})for(unsigned minimum:{8000u,16000u,32000u})for(unsigned lead:{0u,3200u}) {
                const auto samples=trimmed?std::span(pcm).subspan(begin,end-begin):std::span(pcm);
                std::vector<float> audio(lead,0);audio.insert(audio.end(),samples.begin(),samples.end());
                audio.resize(std::max<std::size_t>(minimum,audio.size()),0);
                auto engine=weights.forkState();
                const auto result=engine->recognize(audio,{}, {}, {},false,language);
                const auto value=compact(result.text);
                const bool matched=value==compact(word)||(word=="Thanks."&&value=="thankyou")||(word=="Não."&&value=="nao")||(word=="Ok."&&value=="okay");
                const std::string condition=(trimmed?"trimmed":"original")+std::string("-min")+std::to_string(minimum)+"-lead"+std::to_string(lead);
                ++counts[condition].second;counts[condition].first+=matched;
                std::cout<<nlohmann::json{{"condition",condition},{"sourceType","synthetic local TTS; padding experiment, not human quality acceptance"},
                    {"fixture",fixture.string()},{"fixtureSHA256",hash},{"fixtureFormat","float32 little-endian mono 16 kHz"},
                    {"input",word},{"voice",voice},{"sourceFrames",pcm.size()},{"trimFirst",trimmed?begin:0},{"trimEndExclusive",trimmed?end:pcm.size()},
                    {"leadFrames",lead},{"minimumFrames",minimum},{"inferenceFrames",audio.size()},
                    {"text",result.text},{"matched",matched},{"asrMs",result.latencyMs},{"rejectedNoSpeech",result.rejectedNoSpeech},
                    {"confidenceMeasured",result.speechConfidenceMeasured},{"noSpeechProbability",result.minimumNoSpeechProbability},
                    {"meanTextTokenLogProbability",result.meanTextTokenLogProbability},{"priorInferenceCount",0}}.dump()<<std::endl;
            }
        }
    }
    nlohmann::json summary=nlohmann::json::array();
    for(const auto& [condition,count]:counts)summary.push_back({{"condition",condition},{"matched",count.first},{"samples",count.second}});
    std::cout<<nlohmann::json{{"result","EXPERIMENT_COMPLETED"},{"conditions",summary},{"productionChanged",false},{"qualityAccepted",false}}.dump()<<std::endl;
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;} }
