#import "../../app/NativeCallAudio.h"
#include "../../bridge/AudioDeviceRouting.hpp"
#include <atomic>
#include <cmath>
#include <sys/wait.h>
#include <unistd.h>
struct State { float amplitude; std::atomic<float> peak{0}; std::atomic<unsigned long> received{0}; };
static OSStatus writePCM(AudioObjectID,const AudioTimeStamp*,const AudioBufferList*,const AudioTimeStamp*,AudioBufferList* out,const AudioTimeStamp*,void* ctx) {
    for(unsigned b=0;b<out->mNumberBuffers;++b)if(out->mBuffers[b].mData) {
        float* pcm=(float*)out->mBuffers[b].mData;
        for(unsigned i=0;i<out->mBuffers[b].mDataByteSize/sizeof(float);++i)pcm[i]=((State*)ctx)->amplitude;
    }return noErr;
}
static OSStatus readPCM(AudioObjectID,const AudioTimeStamp*,const AudioBufferList* in,const AudioTimeStamp*,AudioBufferList*,const AudioTimeStamp*,void* ctx) {
    State& s=*(State*)ctx;float peak=s.peak;
    for(unsigned b=0;b<in->mNumberBuffers;++b)if(in->mBuffers[b].mData) {
        const float* pcm=(const float*)in->mBuffers[b].mData;
        for(unsigned i=0;i<in->mBuffers[b].mDataByteSize/sizeof(float);++i)peak=std::max(peak,std::abs(pcm[i]));
        s.received+=in->mBuffers[b].mDataByteSize/sizeof(float);
    }s.peak=peak;return noErr;
}
int main(int argc,char** argv) { @autoreleasepool {
    const bool child=argc>1;NativeCallAudioSession* native=nil;
    if(!child) {
        native=[NativeCallAudioSession new];NSError* error=nil;
        NSString* physical=lvt::devices::resolve(@"BuiltInHeadphoneOutputDevice",false)?@"BuiltInHeadphoneOutputDevice":@"BuiltInSpeakerDevice";
        if(![native prepareWithOutputDeviceUID:physical error:&error]) {
            fprintf(stderr,"FAIL prepare: %s\n",error.localizedDescription.UTF8String);return 1;
        }
        NativeCallAudioSession* other=[NativeCallAudioSession new];
        if([other prepareWithOutputDeviceUID:physical error:&error] || !native.active) { [native close];fprintf(stderr,"FAIL concurrent owner was not protected\n");return 1; }
    }
    const auto mic=lvt::devices::resolve([NativeCallAudioSession microphoneUID],true);
    const auto speakers=lvt::devices::resolve([NativeCallAudioSession speakersUID],false);
    if(!mic || !speakers) { [native close];fprintf(stderr,"FAIL public devices not visible\n");return 2; }
    const auto input=child?mic:speakers,output=child?speakers:mic;
    State state{child?.02f:.01f};AudioDeviceIOProcID read=nullptr,write=nullptr;
    OSStatus status=AudioDeviceCreateIOProcID(input,readPCM,&state,&read);
    if(!status)status=AudioDeviceCreateIOProcID(output,writePCM,&state,&write);
    if(!status)status=AudioDeviceStart(input,read);
    if(!status)status=AudioDeviceStart(output,write);
    pid_t helper=-1;
    if(!status && !child) { helper=fork();if(helper==0) { execl(argv[0],argv[0],"child",nullptr);_exit(127); } }
    if(!status)for(int i=0;i<(child?120:90);++i)usleep(100000);
    if(write){AudioDeviceStop(output,write);AudioDeviceDestroyIOProcID(output,write);}
    if(read){AudioDeviceStop(input,read);AudioDeviceDestroyIOProcID(input,read);}
    // Remove our devices while the simulated call client still has them open.
    if(!child)[native close];
    int childStatus=0;if(helper>0)waitpid(helper,&childStatus,0);
    const float expected=child?.01f:.02f;
    // HAL sample-rate conversion overshoots a DC step by ~5%. The two carriers
    // differ by 2x; this band permits that transient but rejects crosstalk/mixing.
    const bool ok=!status && state.received>10000 && std::abs(state.peak-expected)<expected*.1f && (!child?helper>0 && WIFEXITED(childStatus) && WEXITSTATUS(childStatus)==0:true);
    NSDictionary* result=@{@"role":child?@"call-client":@"translator",@"result":ok?@"PASS":@"FAIL",@"status":@(status),@"receivedSamples":@(state.received.load()),@"peak":@(state.peak.load()),@"expectedPeak":@(expected),@"publicMicrophone":@(mic),@"publicSpeakers":@(speakers)};
    puts([[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil] encoding:NSUTF8StringEncoding].UTF8String);fflush(stdout);
    [native close];
    if(!child) {
        usleep(200000);
        const bool cleaned=!lvt::devices::resolve([NativeCallAudioSession microphoneUID],true) && !lvt::devices::resolve([NativeCallAudioSession speakersUID],false);
        printf("{\"cleanup\":\"%s\",\"scope\":\"Only the two public aggregates and taps created by this test were removed; defaults unchanged\"}\n",cleaned?"PASS":"FAIL");
        if(!cleaned)return 1;
    }
    return ok?0:1;
}}
