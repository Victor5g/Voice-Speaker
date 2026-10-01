#import "NativeCallAudio.h"
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/AudioHardwareTapping.h>
#include "../bridge/AudioDeviceRouting.hpp"
#include <unistd.h>

@implementation NativeCallAudioSession {
    AudioObjectID _microphone, _speakers, _sendTap, _receiveTap;
    NSString* _outputDeviceUID;
}
+ (NSString*)microphoneUID { return @"local.localvoicetranslator.native.microphone"; }
+ (NSString*)speakersUID { return @"local.localvoicetranslator.native.speakers"; }
- (BOOL)active { return _microphone && _speakers && _sendTap && _receiveTap; }
- (NSString*)outputDeviceUID { return _outputDeviceUID ?: @""; }
- (void)close {
    // Call only after the app's audio engines stop. Never remove objects by name.
    if (_microphone) { AudioHardwareDestroyAggregateDevice(_microphone); _microphone = 0; }
    if (_speakers) { AudioHardwareDestroyAggregateDevice(_speakers); _speakers = 0; }
    if (_sendTap) { AudioHardwareDestroyProcessTap(_sendTap); _sendTap = 0; }
    if (_receiveTap) { AudioHardwareDestroyProcessTap(_receiveTap); _receiveTap = 0; }
    _outputDeviceUID = nil;
}
- (void)dealloc { [self close]; }
- (BOOL)prepareWithOutputDeviceUID:(NSString*)uid error:(NSError**)error {
    if (@available(macOS 14.2, *)) {
        if (self.active && [_outputDeviceUID isEqual:uid]) return YES;
        if (self.active) {
            if (error) *error = [NSError errorWithDomain:@"LocalVoiceNativeAudio" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Desative os dispositivos da chamada antes de mudar os fones."}];
            return NO;
        }
        NSString* reason = nil;
        const auto physical = lvt::devices::resolve(uid, false);
        if (!uid.length || !physical || lvt::devices::channels(physical, kAudioObjectPropertyScopeOutput) != 2 ||
            lvt::devices::channels(physical, kAudioObjectPropertyScopeInput) != 0) {
            reason = @"O modo nativo exige uma saída estéreo separada do microfone, como External Headphones. Dispositivos com entrada e saída no mesmo UID ainda não são suportados.";
        }
        for (NSDictionary* device in lvt::devices::available()) {
            if ([device[@"uid"] isEqual:[NativeCallAudioSession microphoneUID]] || [device[@"uid"] isEqual:[NativeCallAudioSession speakersUID]])
                reason = @"Já existem dispositivos nativos LocalVoiceTranslator. Feche a outra instância do app. Após encerramento anormal, remova apenas esses dois agregados na Configuração de Áudio MIDI.";
        }
        auto carrier = [&](NSString* name, NSString* deviceUID, AudioObjectID* output) {
            NSDictionary* spec = @{@kAudioAggregateDeviceNameKey:name, @kAudioAggregateDeviceUIDKey:deviceUID,
                @kAudioAggregateDeviceIsPrivateKey:@NO,
                @kAudioAggregateDeviceSubDeviceListKey:@[@{@kAudioSubDeviceUIDKey:uid, @kAudioSubDeviceInputChannelsKey:@0, @kAudioSubDeviceOutputChannelsKey:@2}],
                @kAudioAggregateDeviceMainSubDeviceKey:uid};
            return AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)spec, output);
        };
        OSStatus status = noErr;
        if (!reason) status = carrier(@"LocalVoiceTranslator Microphone", [NativeCallAudioSession microphoneUID], &_microphone);
        if (!reason && !status) status = carrier(@"LocalVoiceTranslator Speakers", [NativeCallAudioSession speakersUID], &_speakers);
        if (!reason && !status) {
            const pid_t pid = getpid(); AudioObjectID process = 0; UInt32 size = sizeof(process);
            AudioObjectPropertyAddress address{kAudioHardwarePropertyTranslatePIDToProcessObject, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(pid), &pid, &size, &process);
            if (!status && !process) status = kAudioHardwareBadObjectError;
            auto attach = [&](AudioObjectID device, NSString* deviceUID, bool outgoing, AudioObjectID* tap) -> OSStatus {
                CATapDescription* description = outgoing
                    ? [[CATapDescription alloc] initWithProcesses:@[@(process)] andDeviceUID:deviceUID withStream:0]
                    : [[CATapDescription alloc] initExcludingProcesses:@[@(process)] andDeviceUID:deviceUID withStream:0];
                description.name = outgoing ? @"LocalVoice translated microphone" : @"LocalVoice incoming call";
                [description setPrivate:NO]; description.muteBehavior = CATapMuted;
                if (@available(macOS 26.0, *)) description.processRestoreEnabled = NO;
                auto result = AudioHardwareCreateProcessTap(description, tap);
                if (result) return result;
                CFArrayRef taps = (__bridge CFArrayRef)@[description.UUID.UUIDString];
                AudioObjectPropertyAddress tapList{kAudioAggregateDevicePropertyTapList, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
                return AudioObjectSetPropertyData(device, &tapList, 0, nullptr, sizeof(taps), &taps);
            };
            if (!status) status = attach(_microphone, [NativeCallAudioSession microphoneUID], true, &_sendTap);
            if (!status) status = attach(_speakers, [NativeCallAudioSession speakersUID], false, &_receiveTap);
            // HAL updates stream topology asynchronously. No inference/audio here.
            for (int attempt = 0; !status && attempt < 20; ++attempt) {
                if (lvt::devices::channels(_microphone, kAudioObjectPropertyScopeInput) == 2 &&
                    lvt::devices::channels(_speakers, kAudioObjectPropertyScopeInput) == 2) break;
                usleep(25000);
            }
            if (!status && (lvt::devices::channels(_microphone, kAudioObjectPropertyScopeInput) != 2 ||
                lvt::devices::channels(_speakers, kAudioObjectPropertyScopeInput) != 2)) status = kAudioHardwareUnsupportedOperationError;
        }
        if (reason || status) {
            [self close];
            if (error) *error = [NSError errorWithDomain:@"LocalVoiceNativeAudio" code:status ?: 1 userInfo:@{NSLocalizedDescriptionKey:reason ?: [NSString stringWithFormat:@"Não foi possível preparar o áudio nativo da chamada (Core Audio %d). Nenhum driver foi instalado.", status]}];
            return NO;
        }
        _outputDeviceUID = [uid copy]; return YES;
    }
    if (error) *error = [NSError errorWithDomain:@"LocalVoiceNativeAudio" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Áudio nativo de chamada exige macOS 14.2 ou posterior."}];
    return NO;
}
@end
