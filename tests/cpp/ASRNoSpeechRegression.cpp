#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/common/ModelHash.hpp"
#include <cctype>
#include <iostream>

static std::string compact(const std::string& text) {
    std::string out;
    for(unsigned char c:text)if(c>=128||std::isalnum(c))out+=char(c<128?std::tolower(c):c);
    return out;
}

// Real backend regression: each input/route/prompt starts with independent
// native state. The descriptor pins retained PCM so before/after runs never
// regenerate stochastic TTS. Semantic failures remain failures.
int main(int argc,char** argv) { try {
    if(argc!=3)throw std::runtime_error("Usage: asr-no-speech-regression MODEL_ROOT INPUTS_JSON");
    nlohmann::json selection,descriptor;
    const std::string root=argv[1];
    std::ifstream(root+"/selection.json")>>selection;
    std::ifstream(argv[2])>>descriptor;
    lvt::WhisperASR weights(root+"/"+selection.at("filename").get<std::string>(),selection.at("sha256"));
    unsigned passed=0,failed=0;
    for(const auto& input:descriptor.at("inputs")) {
        const auto file=input.at("filename").get<std::string>();
        lvt::verifyModelHash(file,input.at("sha256"));
        std::vector<float> original;
        if(input.value("format","wav")=="f32le") {
            std::ifstream stream(file,std::ios::binary|std::ios::ate);
            const auto bytes=stream.tellg();
            if(bytes<=0||bytes>16000*30*4||bytes%4)throw std::runtime_error("Invalid float PCM fixture");
            original.resize(std::size_t(bytes)/4);stream.seekg(0);stream.read(reinterpret_cast<char*>(original.data()),bytes);
            if(!stream)throw std::runtime_error("Cannot read float PCM fixture");
        }else original=lvt::readWav16k(file);
        const auto first=input.value("firstFrame",std::size_t(0)),end=input.value("endFrame",original.size());
        if(first>=end||end>original.size())throw std::runtime_error("Invalid PCM bounds");
        const auto pcm=std::span(original).subspan(first,end-first);
        for(const auto& language:input.at("languages"))for(const auto& prompt:input.at("prompts")) {
            auto engine=weights.forkState();
            const auto forced=language.get<std::string>(),context=prompt.get<std::string>();
            const auto result=engine->recognize(pcm,{},[&](const auto&){return context;},{},false,forced);
            bool textOK=false;
            if(input.contains("expectedTexts"))for(const auto& expected:input.at("expectedTexts"))textOK|=compact(result.text)==compact(expected.get<std::string>());
            else textOK=!result.text.empty();
            const bool lidOK=engine->languageDetections()==(forced.empty()&&!result.rejectedDigitalSilence?1:0);
            // A valid speech result must not retain the all-zero SOT row's
            // uniform distribution. This is a regression sentinel, not a gate
            // or speech confidence threshold used by the application.
            const bool scoreOK=!result.speechConfidenceMeasured||std::abs(result.minimumNoSpeechProbability-1.0/51865)>1e-7;
            const bool ok=textOK&&lidOK&&scoreOK;
            (ok?passed:failed)++;
            std::cout<<nlohmann::json{{"fixture",file},{"sha256",input.at("sha256")},{"label",input.value("label","")},
                {"firstFrame",first},{"endFrame",end},{"forcedLanguage",forced},{"context",context},{"text",result.text},
                {"expectedTexts",input.value("expectedTexts",nlohmann::json(nullptr))},{"textMatched",textOK},
                {"languageDetections",engine->languageDetections()},{"scoreNotUniform",scoreOK},
                {"rejectedDigitalSilence",result.rejectedDigitalSilence},
                {"speechConfidenceMeasured",result.speechConfidenceMeasured},{"noSpeechProbability",result.minimumNoSpeechProbability},
                {"meanTextTokenLogProbability",result.meanTextTokenLogProbability},{"asrMs",result.latencyMs},
                {"independentState",true},{"result",ok?"PASS":"FAIL"}}.dump()<<std::endl;
        }
    }
    std::cout<<nlohmann::json{{"result",failed?"FAIL":"PASS"},{"passed",passed},{"failed",failed}}.dump()<<std::endl;
    return failed?1:0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 2;} }
