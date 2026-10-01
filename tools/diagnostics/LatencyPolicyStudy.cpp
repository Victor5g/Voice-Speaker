// Isolated study of actual production policies. No ASR/model/audio-device run.
#include "../../engine/vad/VadSegmenter.hpp"
#include "../../engine/conversation/SegmentStabilityManager.hpp"
#include "../../engine/language/LanguageStabilizer.hpp"
#include <iostream>
#include <stdexcept>
using namespace lvt;
static void require(bool ok) { if (!ok) throw std::runtime_error("Policy study expectation failed"); }
static int endpoint(int speechMs, bool silence) {
    VadSegmenter segmenter(true);
    std::array<float,320> frame{};
    for (int ms=0; ms<12000; ms+=20) {
        const bool voiced = !silence || ms<speechMs;
        if (auto segment=segmenter.process(frame,voiced,16000,ms*16,0)) return ms+20;
    }
    return -1;
}
int main() { try {
    // Inputs are controlled VAD decisions, not physical speech or waveform labels.
    for (const int speechMs : {2000,6500}) {
        const int end=endpoint(speechMs,true), delay=end-speechMs;
        require(delay==(speechMs==2000?300:120));
        std::cout << "{\"experiment\":\"endpoint_after_vad_negative\",\"speechMs\":"<<speechMs<<",\"delayMs\":"<<delay<<"}\n";
    }
    require(endpoint(12000,false)==9800);
    std::cout << "{\"experiment\":\"continuous_vad_positive\",\"firstHardCutMs\":9800}\n";
    for (const int step : {1500,750,500}) {
        SegmentStabilityManager stability;
        int confirmedAt=-1;
        for(int n=0;n<3;++n) {
            auto update=stability.observe("Eu vou aceitar. Agora podemos continuar",false);
            if(!update.confirmed.empty()) confirmedAt=n*step;
        }
        require(confirmedAt==2*step);
        const auto revision=stability.observe("Eu não vou aceitar. Agora podemos continuar",true);
        require(revision.conflict);
        std::cout<<"{\"experiment\":\"repeated_partial_policy\",\"stepMs\":"<<step<<",\"commitAfterFirstHypothesisMs\":"<<confirmedAt<<",\"laterNegationConflict\":true}\n";
    }
    SegmentStabilityManager finalOnly;
    require(!finalOnly.observe("Eu não vou aceitar.",true).confirmed.empty());
    LanguageStabilizer language;
    require(!language.observe(std::nullopt,{"pt",.99,500}).stable);
    require(language.observe(std::nullopt,{"pt",.99,1000}).stable);
    std::cout<<"{\"experiment\":\"final_and_language_gates\",\"finalNeedsThreeHypotheses\":false,\"language500msAccepted\":false,\"language1000msAccepted\":true}\n";
    std::cout<<"{\"result\":\"PASS\",\"scope\":\"7 controlled production-policy cases; not end-to-end latency or speech quality\"}\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
