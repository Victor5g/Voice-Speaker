#include "../../engine/vad/VadSegmenter.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "webrtc/common_audio/vad/include/webrtc_vad.h"
#include <sherpa-onnx/c-api/c-api.h>
#include <iostream>
int main() {
 auto input=lvt::readWav16k("tests/data/conversation_ptbr_en.wav");input.resize(input.size()+16000);
 VadInst* vad=nullptr;WebRtcVad_Create(&vad);WebRtcVad_Init(vad);WebRtcVad_set_mode(vad,2);
 lvt::VadSegmenter segmenter(true);std::array<float,320> floats;std::array<std::int16_t,320> pcm;
 for(std::uint64_t frame=0;frame<29*input.size();frame+=320){
  for(std::size_t i=0;i<320;++i){floats[i]=input[(frame+i)%input.size()];pcm[i]=std::int16_t(floats[i]*32767);}
  auto segment=segmenter.process(floats,WebRtcVad_Process(vad,16000,pcm.data(),320)==1,16000,frame,0);
  if(segment && segment->firstFrame==8671200){
   std::cout<<segment->firstFrame<<" "<<segment->samples.size()<<"\n";
   SherpaOnnxWriteWave(segment->samples.data(),segment->samples.size(),16000,"build/soak-repro-exact.wav");
   break;
  }
 }
 WebRtcVad_Free(vad);
}
