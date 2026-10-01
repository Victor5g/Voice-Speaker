#include "../../engine/common/OnnxModel.hpp"
#include "../../engine/common/ModelHash.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <array>
#include <chrono>
#include <iostream>

// Measure the existing segmentation model's speech posterior independently of
// speaker IDs, embeddings, source separation and Whisper. Diagnostic only: this
// executable neither rejects production input nor selects a new threshold.
int main(int argc,char** argv) { try {
    if(argc!=3)throw std::runtime_error("Usage: speech-gate-probe MODEL_ROOT INPUTS_JSON");
    const std::string root=argv[1];
    nlohmann::json manifest,inputs;
    std::ifstream(root+"/manifest.json")>>manifest;
    std::ifstream(argv[2])>>inputs;
    auto entry=std::find_if(manifest.at("models").begin(),manifest.at("models").end(),[](const auto& item){return item.at("type")=="overlap";});
    if(entry==manifest.at("models").end())throw std::runtime_error("Missing segmentation model");
    lvt::OnnxModel model(lvt::ModelBundle::fromJSON(root,*entry));
    const auto window=std::stoul(model.metadata("window_size"));
    const auto stride=std::stoul(model.metadata("receptive_field_shift"));
    const auto field=std::stoul(model.metadata("receptive_field_size"));
    if(window!=160000||!stride||!field||model.metadata("num_classes")!="7")throw std::runtime_error("Unexpected powerset contract");
    for(const auto& input:inputs.at("inputs")) {
        const auto file=input.at("filename").get<std::string>();
        lvt::verifyModelHash(file,input.at("sha256"));
        std::vector<float> original;
        if(input.value("format","wav")=="f32le") {
            std::ifstream stream(file,std::ios::binary|std::ios::ate);
            const auto bytes=stream.tellg();
            if(bytes<=0||bytes>16000*30*4||bytes%4)throw std::runtime_error("Invalid PCM float fixture");
            original.resize(std::size_t(bytes)/4);stream.seekg(0);stream.read(reinterpret_cast<char*>(original.data()),bytes);
            if(!stream)throw std::runtime_error("Cannot read PCM float fixture");
        }else original=lvt::readWav16k(file);
        const auto first=input.value("firstFrame",std::size_t(0)),end=input.value("endFrame",original.size());
        if(first>=end||end>original.size())throw std::runtime_error("Fixture bounds invalid");
        const auto pcm=std::span(original).subspan(first,end-first);
        const auto started=std::chrono::steady_clock::now();
        std::size_t count=0,aboveHalf=0,above90=0,unpaddedCount=0;
        double total=0,maxSpeech=0,maxUnpaddedSpeech=0;
        nlohmann::json frames=nlohmann::json::array();
        for(std::size_t offset=0;offset<pcm.size();offset+=window/2) {
            std::vector<float> padded(window,0);std::copy_n(pcm.begin()+offset,std::min(window,pcm.size()-offset),padded.begin());
            const std::array<std::int64_t,3> shape{1,1,std::int64_t(window)};
            auto output=model.run(padded,shape,window*7,{});
            if(output.shape.size()!=3||output.shape[2]!=7)throw std::runtime_error("Invalid powerset output");
            for(std::size_t frame=0;frame<std::size_t(output.shape[1]);++frame) {
                const auto at=offset+frame*stride;
                if(at>=std::min(pcm.size(),offset+window/2))break;
                const auto logits=output.values.begin()+frame*7;
                const auto maximum=*std::max_element(logits,logits+7);
                double denominator=0;
                for(auto value=logits;value!=logits+7;++value)denominator+=std::exp(*value-maximum);
                const double probability=1-std::exp(*logits-maximum)/denominator;
                if(!std::isfinite(probability))throw std::runtime_error("Non-finite speech posterior");
                ++count;total+=probability;maxSpeech=std::max(maxSpeech,probability);
                aboveHalf+=probability>=.5;above90+=probability>=.9;
                const bool unpadded=at+field<=pcm.size();
                if(unpadded){++unpaddedCount;maxUnpaddedSpeech=std::max(maxUnpaddedSpeech,probability);}
                frames.push_back({{"firstFrame",at},{"speechProbability",probability},{"receptiveFieldWithinPCM",unpadded}});
            }
        }
        std::cout<<nlohmann::json{{"fixture",file},{"sha256",input.at("sha256")},{"label",input.value("label","")},
            {"firstFrame",first},{"endFrame",end},{"frames",count},{"strideFrames",stride},{"receptiveFieldFrames",field},
            {"maxSpeechProbability",maxSpeech},{"meanSpeechProbability",count?total/count:0},
            {"framesAtLeastHalf",aboveHalf},{"framesAtLeast90Percent",above90},
            {"unpaddedFrames",unpaddedCount},{"maxUnpaddedSpeechProbability",maxUnpaddedSpeech},
            {"analysisMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()},
            {"frameEvidence",frames},{"scope","Raw existing Pyannote speech posterior; not calibrated acceptance or a selected production threshold"}}.dump()<<std::endl;
    }
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;} }
