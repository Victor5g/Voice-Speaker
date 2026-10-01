#include "../../engine/tts/TTSEngine.hpp"
#include <chrono>
#include <iostream>
int main() {
    try {
        nlohmann::json voices;std::ifstream("models/tts-assets.json")>>voices;
        for(const auto& entry:voices) {
            const auto begin=std::chrono::steady_clock::now();
            lvt::TTSEngine engine(lvt::ModelBundle::fromJSON("models",entry));
            const auto loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            const bool pt=entry["language"]=="pt-BR";
            const std::vector<std::string> texts=pt?std::vector<std::string>{"Olá.","Eu não autorizei o pagamento.","Eu não autorizei o pagamento. O ônibus chega amanhã às três horas."}:
                std::vector<std::string>{"Hello.","I did not authorize the payment.","I did not authorize the payment. The bus arrives tomorrow at three o'clock."};
            for(int repeat=0;repeat<3;++repeat)for(const auto& text:texts){
                auto audio=engine.synthesize(text);const double durationMs=1000.0*audio.samples.size()/audio.sampleRate;
                std::cout<<nlohmann::json{{"voice",entry["voice"]},{"characters",text.size()},{"repeat",repeat},
                    {"loadMs",loadMs},{"inferenceMs",audio.latencyMs},{"generatedAudioMs",durationMs},{"rtf",audio.latencyMs/durationMs}}.dump()<<std::endl;
            }
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
