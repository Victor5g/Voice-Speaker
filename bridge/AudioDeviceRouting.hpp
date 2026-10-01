#pragma once
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>
#include <vector>

namespace lvt::devices {
inline NSString* text(AudioObjectID object, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef value = nullptr; UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) != noErr || !value) return @"";
    return CFBridgingRelease(value);
}
inline UInt32 channels(AudioObjectID object, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(object, &address, 0, nullptr, &size) != noErr || size < sizeof(AudioBufferList)) return 0;
    std::vector<std::uint8_t> bytes(size);
    auto* buffers = reinterpret_cast<AudioBufferList*>(bytes.data());
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, buffers) != noErr) return 0;
    UInt32 count = 0;
    for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i) count += buffers->mBuffers[i].mNumberChannels;
    return count;
}
inline NSArray<NSDictionary<NSString*, id>*>* available() {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    NSMutableArray* result = [NSMutableArray array];
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) return result;
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.data()) != noErr) return result;
    for (auto id : ids) {
        AudioObjectPropertyAddress transportAddress{kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        UInt32 transport = 0, transportSize = sizeof(transport);
        AudioObjectGetPropertyData(id, &transportAddress, 0, nullptr, &transportSize, &transport);
        NSString* uid = text(id, kAudioDevicePropertyDeviceUID);
        if (uid.length) [result addObject:@{@"id": @(id), @"uid": uid, @"name": text(id, kAudioObjectPropertyName),
            @"inputChannels": @(channels(id, kAudioObjectPropertyScopeInput)),
            @"outputChannels": @(channels(id, kAudioObjectPropertyScopeOutput)),
            @"virtual": @(transport == kAudioDeviceTransportTypeVirtual)}];
    }
    return result;
}
inline AudioObjectID resolve(NSString* uid, bool input) {
    if (uid.length) {
        for (NSDictionary* device in available())
            if ([device[@"uid"] isEqual:uid] && [device[input ? @"inputChannels" : @"outputChannels"] unsignedIntValue])
                return [device[@"id"] unsignedIntValue];
        return kAudioObjectUnknown;
    }
    AudioObjectPropertyAddress address{input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectID id = kAudioObjectUnknown; UInt32 size = sizeof(id);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &id);
    return id;
}
}
