#include "../../engine/vad/VadSegmenter.hpp"
#include "../../engine/audio/PCMPlayback.hpp"
#include "../../engine/common/LatencyStatistics.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace lvt;
static int passed=0;
static void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
    ++passed;std::cout<<"PASS "<<message<<'\n';
}
int main() { try {
    const auto origin=hostclock::now();
    check(!hostclock::milliseconds(0,origin) && !hostclock::milliseconds(origin,origin-1),"Missing or reversed clocks are unavailable, never zero latency");
    check(std::abs(*hostclock::milliseconds(origin,hostclock::advance(origin,.123)) - 123)<.01,"Monotonic tick conversion preserves milliseconds");
    VadSegmenter vad(true);std::array<float,320> frame{};std::optional<SpeechSegment> result;
    for(int i=0;i<65;++i)result=vad.process(frame,i<50,16000,i*320,hostclock::advance(origin,i*.02));
    check(result && result->timing.endpointReason==EndpointReason::silence,"A silence endpoint has an explicit reason");
    check(result->timing.speechEndFrame==16000 && std::abs(*hostclock::milliseconds(origin,result->timing.speechEndHostTime)-1000)<.01,"Last positive VAD frame excludes trailing silence");
    check(std::abs(*hostclock::milliseconds(origin,result->timing.windowEndHostTime)-1300)<.01,"Audio window end is separate from estimated speech end");
    vad.clear();for(int i=0;i<490;++i)result=vad.process(frame,true,16000,i*320,0);
    check(result && result->timing.endpointReason==EndpointReason::windowLimit && !result->timing.speechEndHostTime,"Hard cut and absent hardware clock cannot claim pause latency");
    vad.clear();std::array<float,960> deviceFrame{};
    vad.process(deviceFrame,true,48000,48000,origin);
    check(vad.current().timing.speechEndFrame==16320,"48 kHz input uses the same 16 kHz timeline as ASR annotations");
    std::vector<float> pcm(16000,0);std::fill(pcm.begin()+4000,pcm.begin()+8000,.1f);
    check(audioOnsetFrame(pcm,16000)==4000,"Audio onset ignores 250 ms of leading silence");
    auto impulse=std::vector<float>(16000,0);impulse[4000]=1;
    check(audioOnsetFrame(impulse,16000)==std::numeric_limits<std::size_t>::max(),"Single-window transient is not an audible onset");
    PCMPlayback playback;playback.enable();SynthesizedAudio audio;
    audio.sampleRate=16000;audio.pcm=pcm;audio.translation.identity={7,11,origin,0};
    audio.translation.identity.timing.speechEndFrame=12345;
    check(playback.enqueue(audio,16000) && std::abs(playback.pendingSeconds()-1)<1e-6,"Playback accounts for seconds, not only queue slots");
    std::array<float,1600> output{};
    for(int i=0;i<10;++i)playback.render(output.data(),output.size(),16000,hostclock::advance(origin,i*.1));
    PlaybackEvent e;int starts=0,onsets=0,ends=0;std::uint64_t onset=0;
    while(playback.popEvent(e)) {
        check(e.identity.segment==11 && e.identity.timing.speechEndFrame==12345,"Playback preserves timing and unit identity");
        if(e.started)++starts;else if(e.audible){++onsets;onset=e.hostTime;}else ++ends;
    }
    check(starts==1 && onsets==1 && ends==1,"Start, onset and end are emitted exactly once across callbacks");
    check(std::abs(*hostclock::milliseconds(origin,onset)-250)<=10,"Onset uses the hardware timestamp and sample offset");
    check(playback.pendingSeconds()==0,"Duration accounting drains without rounding drift");
    playback.releaseRetired();check(playback.enqueue(audio,16000),"Retired slots can be measured again");
    playback.invalidate();playback.render(output.data(),output.size(),16000);
    check(playback.pendingSeconds()==0 && std::all_of(output.begin(),output.end(),[](float x){return x==0;}),"STOP hides stale backlog and silences PCM");
    playback.clearStopped();check(!playback.popEvent(e),"STOP clears all timing events");
    LatencyStatistics stats;for(int i=0;i<29;++i)stats.add(i);
    check(stats.snapshot().count==29 && stats.snapshot().p95==0,"Percentiles are withheld below thirty measurements");
    stats.add(29);check(stats.snapshot().p95==28,"P95 is calculated from complete paired samples");
    std::cout<<"RESULT "<<passed<<" passed, 0 failed\n";
} catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
