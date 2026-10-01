#include "../../engine/audio/WavReader.hpp"
#include "../../engine/vad/VadSegmenter.hpp"
#include "webrtc/common_audio/vad/include/webrtc_vad.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <memory>
// Same WebRTC mode and production segmenter, faster-than-real-time boundary
// analysis only. This is not an inference or latency measurement.
int main(int argc,char** argv) { try {
    if(argc!=2)throw std::runtime_error("Expected fixed WAV");
    auto pcm=lvt::readWav16k(argv[1]);
    VadInst* raw=nullptr;if(WebRtcVad_Create(&raw))throw std::runtime_error("Cannot create VAD");
    std::unique_ptr<VadInst,decltype(&WebRtcVad_Free)> vad(raw,WebRtcVad_Free);
    WebRtcVad_Init(vad.get());WebRtcVad_set_mode(vad.get(),2);
    lvt::VadSegmenter segmenter(true);std::array<std::int16_t,320> input{};
    for(std::size_t cursor=0;cursor+320<=pcm.size();cursor+=320) {
        for(std::size_t i=0;i<320;++i)input[i]=std::int16_t(std::clamp(pcm[cursor+i],-1.f,1.f)*32767);
        const auto decision=WebRtcVad_Process(vad.get(),16000,input.data(),320);
        if(decision<0)throw std::runtime_error("VAD failed");
        if(auto unit=segmenter.process({pcm.data()+cursor,320},decision==1,16000,cursor,0))
            std::cout<<nlohmann::json{{"firstFrame",unit->firstFrame},{"endFrame",unit->firstFrame+unit->samples.size()},
                {"speechEndFrame",unit->timing.speechEndFrame},{"reason",int(unit->timing.endpointReason)}}.dump()<<'\n';
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
