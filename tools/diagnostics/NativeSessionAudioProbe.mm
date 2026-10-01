#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/AudioHardwareTapping.h>
#include "../../bridge/AudioDeviceRouting.hpp"
#include <unistd.h>
#include <atomic>
#include <cmath>
struct IOState { std::atomic<std::uint64_t> inputSamples{0}, outputSamples{0}; std::atomic<float> peak{0}; };
static OSStatus producer(AudioObjectID,const AudioTimeStamp*,const AudioBufferList*,const AudioTimeStamp*,AudioBufferList* output,const AudioTimeStamp*,void* context) {
    auto& state=*static_cast<IOState*>(context);
    for(UInt32 b=0;b<output->mNumberBuffers;++b) {
        auto& buffer=output->mBuffers[b];auto* pcm=static_cast<float*>(buffer.mData);
        if(!pcm)continue;
        for(UInt32 i=0;i<buffer.mDataByteSize/sizeof(float);++i)pcm[i]=.01f*std::sin(float((state.outputSamples.load()+i)%48000)*.0432f);
        state.outputSamples+=buffer.mDataByteSize/sizeof(float);
    }return noErr;
}
static OSStatus consumer(AudioObjectID,const AudioTimeStamp*,const AudioBufferList* input,const AudioTimeStamp*,AudioBufferList*,const AudioTimeStamp*,void* context) {
    auto& state=*static_cast<IOState*>(context);
    for(UInt32 b=0;b<input->mNumberBuffers;++b) {
        const auto& buffer=input->mBuffers[b];const auto* pcm=static_cast<const float*>(buffer.mData);
        if(!pcm)continue;
        float peak=state.peak;for(UInt32 i=0;i<buffer.mDataByteSize/sizeof(float);++i)peak=std::max(peak,std::abs(pcm[i]));
        state.peak=peak;state.inputSamples+=buffer.mDataByteSize/sizeof(float);
    }return noErr;
}
int main(int argc,char** argv) { @autoreleasepool {
    const bool io=argc>1 && std::string(argv[1])=="--io";
    // Capability probe only. Private objects, no I/O, no recording, no defaults.
    AudioObjectID carrier=0, tap=0, microphone=0;
    NSMutableDictionary* report=[NSMutableDictionary dictionary];
    const auto output=lvt::devices::resolve(@"",false);
    NSString* outputUID=lvt::devices::text(output,kAudioDevicePropertyDeviceUID);
    NSString* carrierUID=[@"local.lvt.probe.carrier." stringByAppendingString:NSUUID.UUID.UUIDString];
    NSDictionary* carrierSpec=@{@kAudioAggregateDeviceNameKey:@"LocalVoiceTranslator Native Probe",
        @kAudioAggregateDeviceUIDKey:carrierUID,@kAudioAggregateDeviceIsPrivateKey:@YES,
        @kAudioAggregateDeviceSubDeviceListKey:@[@{@kAudioSubDeviceUIDKey:outputUID}],
        @kAudioAggregateDeviceMainSubDeviceKey:outputUID};
    report[@"createPrivateCarrier"]=@(AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)carrierSpec,&carrier));
    if(carrier) {
        // Self process only; a tap cannot observe another app in this diagnostic.
        const pid_t pid=getpid(); AudioObjectID process=0; UInt32 size=sizeof(process);
        AudioObjectPropertyAddress address{kAudioHardwarePropertyTranslatePIDToProcessObject,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        report[@"selfProcess"]=@(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,sizeof(pid),&pid,&size,&process));
        CATapDescription* description=[[CATapDescription alloc] initWithProcesses:@[@(process)] andDeviceUID:carrierUID withStream:0];
        description.name=@"LocalVoiceTranslator Self Audio Probe"; [description setPrivate:YES];
        description.muteBehavior=io?CATapMuted:CATapUnmuted;
        report[@"createPrivateTap"]=@(AudioHardwareCreateProcessTap(description,&tap));
        if(tap) {
            NSDictionary* spec=@{@kAudioAggregateDeviceNameKey:@"LocalVoiceTranslator Native Microphone Probe",
                @kAudioAggregateDeviceUIDKey:[@"local.lvt.probe.mic." stringByAppendingString:NSUUID.UUID.UUIDString],
                @kAudioAggregateDeviceIsPrivateKey:@YES,
                @kAudioAggregateDeviceTapListKey:@[@{@kAudioSubTapUIDKey:description.UUID.UUIDString} ]};
            report[@"createPrivateMicrophone"]=@(AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)spec,&microphone));
        }
        usleep(200000);
        report[@"carrierOutputChannels"]=@(lvt::devices::channels(carrier,kAudioObjectPropertyScopeOutput));
        if(microphone) report[@"microphoneInputChannels"]=@(lvt::devices::channels(microphone,kAudioObjectPropertyScopeInput));
        if(io && microphone && lvt::devices::channels(microphone,kAudioObjectPropertyScopeInput)==2) {
            IOState state;AudioDeviceIOProcID read=nullptr,write=nullptr;
            report[@"createConsumer"]=@(AudioDeviceCreateIOProcID(microphone,consumer,&state,&read));
            report[@"createProducer"]=@(AudioDeviceCreateIOProcID(carrier,producer,&state,&write));
            if(read && write) {
                const OSStatus readStatus=AudioDeviceStart(microphone,read);report[@"startConsumer"]=@(readStatus);
                if(readStatus==noErr) {
                    report[@"startProducer"]=@(AudioDeviceStart(carrier,write));
                    for(int i=0;i<100;++i)usleep(100000);
                }
                AudioDeviceStop(carrier,write);AudioDeviceStop(microphone,read);
            }
            if(write)AudioDeviceDestroyIOProcID(carrier,write);
            if(read)AudioDeviceDestroyIOProcID(microphone,read);
            report[@"inputSamples"]=@(state.inputSamples.load());report[@"outputSamples"]=@(state.outputSamples.load());report[@"peak"]=@(state.peak.load());
        }
    }
    if(microphone) report[@"destroyMicrophone"]=@(AudioHardwareDestroyAggregateDevice(microphone));
    if(tap) report[@"destroyTap"]=@(AudioHardwareDestroyProcessTap(tap));
    if(carrier) report[@"destroyCarrier"]=@(AudioHardwareDestroyAggregateDevice(carrier));
    report[@"scope"]=io?@"Private Core Audio objects; synthetic self-process audio only, muted carrier. No public devices, third-party driver or default-device changes. System audio permission may be requested. No PCM recording.":@"Private Core Audio objects only. No audio I/O, public devices, third-party driver, permission changes or default-device changes. This does not establish a usable virtual microphone.";
    NSData* json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil];
    puts([[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
}}
