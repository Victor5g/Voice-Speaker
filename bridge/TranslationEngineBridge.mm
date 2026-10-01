#import "TranslationEngineBridge.h"
#import <AVFoundation/AVFoundation.h>
#import <mach/mach.h>
#import <sys/resource.h>
#include "../engine/audio/AudioCore.hpp"
#include "../engine/common/MemoryBudget.hpp"
#include "../engine/translation/Glossary.hpp"
#include <memory>
#include "AudioDeviceRouting.hpp"

@implementation TranslationEngineBridge {
    AVAudioEngine *_engine;
    AVAudioEngine *_captureEngine;
    NSString *_inputUID, *_outputUID, *_sourceLanguage;
    AudioObjectID _inputDevice, _outputDevice;
    BOOL _explicitRouting;
    AVAudioSourceNode *_source;
    std::shared_ptr<lvt::AudioCore> _core;
    NSString *_modelPath, *_modelHash, *_resourcesPath;
    NSString *_asrBackend;
    NSString *_processingJSON;
    lvt::ProcessingOptions _processingOptions;
    BOOL _running;
    BOOL _tapInstalled;
    NSTimeInterval _previousWall;
    double _previousCPU;
    id _configurationObserver;
    id _captureConfigurationObserver;
    NSString *_audioError;
    NSUInteger _configurationRestarts;
    double _inputRate;
    dispatch_source_t _memoryPressureSource;
}
- (instancetype)init {
    if ((self = [super init])) {
        _core = std::make_shared<lvt::AudioCore>();
        _memoryPressureSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
            DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL, dispatch_get_main_queue());
        __weak TranslationEngineBridge *weakSelf = self;
        dispatch_source_set_event_handler(_memoryPressureSource, ^{
            TranslationEngineBridge *strongSelf = weakSelf;
            if (!strongSelf) return;
            unsigned long flags = dispatch_source_get_data(strongSelf->_memoryPressureSource);
            lvt::MemoryBudget::shared().setPressure(flags & DISPATCH_MEMORYPRESSURE_CRITICAL ? 2 : flags & DISPATCH_MEMORYPRESSURE_WARN ? 1 : 0);
        });
        dispatch_resume(_memoryPressureSource);
    }
    return self;
}
+ (NSArray<NSDictionary<NSString *, id> *> *)availableAudioDevices { return lvt::devices::available(); }
- (void)configureInputDeviceUID:(NSString *)inputUID outputDeviceUID:(NSString *)outputUID sourceLanguage:(NSString *)language {
    if (!_running) { _inputUID = [inputUID copy]; _outputUID = [outputUID copy]; _sourceLanguage = [language copy]; }
}
- (BOOL)running { return _running && _engine.isRunning && (!_captureEngine || _captureEngine.isRunning); }
- (BOOL)voiceProcessingEnabled { return !_explicitRouting && _engine && _engine.inputNode.voiceProcessingEnabled; }
- (BOOL)startWithError:(NSError **)error {
    if (_running) return YES;
    _engine = [[AVAudioEngine alloc] init];
    // Selecting a language must not disable voice processing on the default
    // hardware graph. Only an explicit device route requires separate graphs.
    _explicitRouting = _inputUID.length || _outputUID.length;
    _inputDevice = lvt::devices::resolve(_inputUID, true);
    _outputDevice = lvt::devices::resolve(_outputUID, false);
    if (!_inputDevice || !_outputDevice) {
        if (error) *error = [NSError errorWithDomain:@"LocalVoiceTranslator.Audio" code:2 userInfo:@{NSLocalizedDescriptionKey: @"O dispositivo selecionado foi desconectado. Escolha novamente os caminhos de áudio."}];
        [self stop]; return NO;
    }
    if (_explicitRouting) {
        // Independent graphs keep each HAL device and clock explicit. In call mode
        // use headphones: no VPIO aggregate or acoustic echo cancellation is claimed.
        _captureEngine = [[AVAudioEngine alloc] init];
        if (![_captureEngine.inputNode.AUAudioUnit setDeviceID:_inputDevice error:error] ||
            ![_engine.outputNode.AUAudioUnit setDeviceID:_outputDevice error:error]) { [self stop]; return NO; }
    }
    _audioError = nil;
    _configurationRestarts = 0;
    __weak TranslationEngineBridge *weakSelf = self;
    _configurationObserver = [NSNotificationCenter.defaultCenter addObserverForName:AVAudioEngineConfigurationChangeNotification object:_engine queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
        TranslationEngineBridge *strongSelf = weakSelf;
        if (!strongSelf || !strongSelf->_running || note.object != strongSelf->_engine) return;
        if (strongSelf->_explicitRouting) {
            [strongSelf stop];
            strongSelf->_audioError = @"O caminho de áudio mudou. Confira os dispositivos e inicie novamente.";
            return;
        }
        if (strongSelf->_engine.isRunning) return;
        // VPIO's private aggregate device may finish configuring after start() returns.
        // Restart the existing graph, not another aggregate/device, once formats settle.
        AVAudioFormat *format = [strongSelf->_engine.inputNode outputFormatForBus:0];
        if (format.sampleRate != strongSelf->_inputRate || strongSelf->_configurationRestarts >= 3) {
            [strongSelf stop];
            strongSelf->_audioError = @"O dispositivo de áudio mudou de formato. Inicie a escuta novamente.";
            return;
        }
        ++strongSelf->_configurationRestarts;
        NSError *restartError = nil;
        if (![strongSelf->_engine startAndReturnError:&restartError]) {
            [strongSelf stop];
            strongSelf->_audioError = restartError.localizedDescription;
        }
    }];
    if (_captureEngine) {
        _captureConfigurationObserver = [NSNotificationCenter.defaultCenter addObserverForName:AVAudioEngineConfigurationChangeNotification object:_captureEngine queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
            TranslationEngineBridge *strongSelf = weakSelf;
            if (!strongSelf || !strongSelf->_running || note.object != strongSelf->_captureEngine) return;
            if (!strongSelf->_captureEngine.isRunning ||
                strongSelf->_captureEngine.inputNode.AUAudioUnit.deviceID != strongSelf->_inputDevice ||
                [strongSelf->_captureEngine.inputNode outputFormatForBus:0].sampleRate != strongSelf->_inputRate) {
                [strongSelf stop];
                strongSelf->_audioError = @"O dispositivo de entrada mudou. Confira os caminhos e inicie novamente.";
            }
        }];
    }
    AVAudioInputNode *input = (_captureEngine ?: _engine).inputNode;
    // Enable before querying formats or connecting nodes. Output on this same graph
    // is VoiceProcessingIO's far-end reference; capture is never paused for playback.
    if (!_explicitRouting) {
        if (![input setVoiceProcessingEnabled:YES error:error]) { [self stop]; return NO; }
        input.voiceProcessingBypassed = NO;
        input.voiceProcessingAGCEnabled = YES;
        input.voiceProcessingOtherAudioDuckingConfiguration = {NO, AVAudioVoiceProcessingOtherAudioDuckingLevelMin};
    }
    AVAudioFormat *inputFormat = [input outputFormatForBus:0];
    AVAudioFormat *hardwareOutput = [_engine.outputNode inputFormatForBus:0];
    if (inputFormat.sampleRate <= 0 || inputFormat.channelCount == 0 || hardwareOutput.sampleRate <= 0) {
        if (error) *error = [NSError errorWithDomain:@"LocalVoiceTranslator.Audio" code:1 userInfo:@{NSLocalizedDescriptionKey: @"Nenhum dispositivo de áudio disponível."}];
        [self stop]; return NO;
    }
    auto core = _core;
    double inputRate = inputFormat.sampleRate;
    _inputRate = inputRate;
    // VPIO requires matching client-side input/output sample rates. Its initial
    // output format can be 44.1 kHz while the microphone is 48 kHz on this Mac.
    // Set the app graph explicitly; never change global device sample rates.
    double outputRate = _explicitRouting ? hardwareOutput.sampleRate : inputRate;
    AVAudioFormat *mono = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:outputRate channels:1];
    _source = [[AVAudioSourceNode alloc] initWithFormat:mono renderBlock:^OSStatus(BOOL *silence, const AudioTimeStamp *timestamp, AVAudioFrameCount count, AudioBufferList *output) {
        if (output->mNumberBuffers && output->mBuffers[0].mData) {
            core->render((float *)output->mBuffers[0].mData, count, outputRate, timestamp ? timestamp->mHostTime : 0);
            *silence = NO;
        }
        return noErr;
    }];
    [_engine attachNode:_source];
    [_engine connect:_source to:_engine.mainMixerNode format:mono];
    AVAudioFormat *outputClientFormat = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:outputRate channels:hardwareOutput.channelCount];
    [_engine connect:_engine.mainMixerNode to:_engine.outputNode format:outputClientFormat];
    mach_timebase_info_data_t inputTimebase{}; mach_timebase_info(&inputTimebase);
    const double inputTicksPerSecond = 1e9 * inputTimebase.denom / inputTimebase.numer;
    [input installTapOnBus:0 bufferSize:1024 format:inputFormat block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
        if (!buffer.floatChannelData) return;
        const auto channels = buffer.format.channelCount;
        if (channels == 1) { core->capture(buffer.floatChannelData[0], buffer.frameLength, inputRate, when.hostTime); return; }
        // Calls conventionally use channels 1/2, including a 16-channel virtual
        // cable. Average those channels so a voice panned right is not lost.
        std::array<float, lvt::audioBlockSize> monoInput{};
        for (AVAudioFrameCount offset = 0; offset < buffer.frameLength;) {
            const auto count = std::min<AVAudioFrameCount>(monoInput.size(), buffer.frameLength - offset);
            for (AVAudioFrameCount i = 0; i < count; ++i) {
                const auto index = offset + i;
                monoInput[i] = buffer.format.isInterleaved
                    ? .5f * (buffer.floatChannelData[0][index * channels] + buffer.floatChannelData[0][index * channels + 1])
                    : .5f * (buffer.floatChannelData[0][index] + buffer.floatChannelData[1][index]);
            }
            core->capture(monoInput.data(), count, inputRate, when.hostTime ? when.hostTime + std::uint64_t(offset * inputTicksPerSecond / inputRate) : 0);
            offset += count;
        }
    }];
    _tapInstalled = YES;
    if (_modelPath) _core->configureASR(_modelPath.UTF8String, _modelHash.UTF8String);
    _core->configureASRBackend(_asrBackend.length ? _asrBackend.UTF8String : "");
    if (_resourcesPath) _core->configureConversation(_resourcesPath.UTF8String, outputRate);
    _core->setSourceLanguage(_sourceLanguage.length ? _sourceLanguage.UTF8String : "");
    _core->configureProcessing(_processingOptions);
    _core->start();
    if (_captureEngine) {
        [_captureEngine prepare];
        if (![_captureEngine startAndReturnError:error]) { [self stop]; return NO; }
    }
    [_engine prepare];
    if (![_engine startAndReturnError:error]) { [self stop]; return NO; }
    _core->setPlaybackDeviceLatency(_engine.outputNode.presentationLatency * 1000);
    _running = YES;
    return YES;
}
- (void)requestStop { _core->requestStop(); }
- (void)stop {
    _running = NO;
    auto retiring = _core;
    retiring->requestStop();
    if (_configurationObserver) { [NSNotificationCenter.defaultCenter removeObserver:_configurationObserver]; _configurationObserver = nil; }
    if (_captureConfigurationObserver) { [NSNotificationCenter.defaultCenter removeObserver:_captureConfigurationObserver]; _captureConfigurationObserver = nil; }
    [_engine stop];
    [_captureEngine stop];
    if (_tapInstalled) { [(_captureEngine ?: _engine).inputNode removeTapOnBus:0]; _tapInstalled = NO; }
    _source = nil; _captureEngine = nil; _engine = nil;
    _core = std::make_shared<lvt::AudioCore>();
    // Capture/render callbacks have stopped; model destruction and worker joins
    // happen off the main thread. The next session has a separate owner.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ retiring->stop(); });
}
- (void)dealloc { if (_memoryPressureSource) dispatch_source_cancel(_memoryPressureSource); [self stop]; }
- (void)playTestTone { if (_running) _core->requestTestTone(); }
- (void)configureASRBackend:(NSString *)backend {
    if (!_running) _asrBackend = [backend copy];
}
- (BOOL)configureProcessingOptions:(NSString *)json error:(NSError **)error {
    try {
        if(_running)throw std::runtime_error("Pare a sessão antes de mudar os processamentos.");
        auto options=lvt::ProcessingOptions::fromJSON(json.UTF8String);
        if(!options.glossaryJSON.empty())(void)lvt::Glossary::fromJSON(options.glossaryJSON);
        _processingOptions=std::move(options);_processingJSON=[json copy];return YES;
    }catch(const std::exception& failure){
        if(error)*error=[NSError errorWithDomain:@"LocalVoiceTranslator.Configuration" code:1 userInfo:@{NSLocalizedDescriptionKey:@(failure.what())}];
        return NO;
    }
}
- (void)configureASRModel:(NSString *)path sha256:(NSString *)sha256 {
    if (!_running) { _modelPath = [path copy]; _modelHash = [sha256 copy]; }
}
- (void)configureConversationResources:(NSString *)path { if (!_running) _resourcesPath = [path copy]; }
- (NSArray<NSDictionary<NSString *, id> *> *)drainConversationEvents {
    NSMutableArray *result = [NSMutableArray array];
    lvt::ConversationEvent event;
    while (_core->popConversationEvent(event)) {
        // Sound-onset events update numeric metrics in the coordinator. Do not
        // replace a conversation row's playback state with a diagnostic state.
        if (event.state == "audio-onset") continue;
        const auto& item = event.result;
        [result addObject:@{@"session": @(item.identity.session), @"segment": @(item.identity.segment),
            @"revision": @(item.identity.revision), @"speaker": @(item.identity.speaker),
            @"sourceSegment": @(item.identity.sourceSegment),
            @"text": @(item.original.c_str()), @"translation": @(item.translated.c_str()),
            @"language": @(item.sourceLanguage.c_str()), @"targetLanguage": @(item.targetLanguage.c_str()),
            @"state": @(event.state.c_str()), @"error": @(event.error.c_str()), @"voice": @(event.voice.c_str()),
            @"captureHostTime": @(item.identity.captureHostTime), @"playbackHostTime": @(event.playbackHostTime),
            @"asrLatencyMs": @(item.asrLatencyMs), @"translationLatencyMs": @(item.translationLatencyMs),
            @"ttsLatencyMs": @(event.synthesisLatencyMs)}];
    }
    return result;
}
- (NSArray<NSDictionary<NSString *, id> *> *)drainTranscripts {
    NSMutableArray *result = [NSMutableArray array];
    lvt::ASRResult item;
    while (_core->popTranscript(item)) {
        [result addObject:@{@"text": @(item.text.c_str()), @"language": @(item.detectedLanguage.c_str()),
                           @"targetLanguage": @(item.targetLanguage.c_str()), @"languageStatus": @(item.languageStatus.c_str()),
                            @"ptProbability": @(item.ptProbability), @"enProbability": @(item.enProbability),
                            @"probability": @(item.detectedProbability), @"durationMs": @(item.durationMs),
                            @"latencyMs": @(item.latencyMs)}];
    }
    return result;
}
- (NSDictionary<NSString *, id> *)snapshot {
    if (_running && _explicitRouting &&
        (lvt::devices::resolve(_inputUID, true) != _inputDevice || lvt::devices::resolve(_outputUID, false) != _outputDevice ||
         _captureEngine.inputNode.AUAudioUnit.deviceID != _inputDevice || _engine.outputNode.AUAudioUnit.deviceID != _outputDevice ||
         !_captureEngine.isRunning)) {
        [self stop];
        _audioError = @"Um dispositivo do caminho de áudio foi removido ou alterado. A sessão foi interrompida.";
    }
    auto a = _core->snapshot();
    const auto memory = lvt::MemoryBudget::shared().snapshot();
    task_vm_info_data_t vm{};
    mach_msg_type_number_t vmCount = TASK_VM_INFO_COUNT;
    const bool vmValid = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &vmCount) == KERN_SUCCESS;
    struct rusage usage{};
    const bool usageValid = getrusage(RUSAGE_SELF, &usage) == 0;
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t threadCount = 0;
    const bool threadsValid = task_threads(mach_task_self(), &threads, &threadCount) == KERN_SUCCESS;
    if (threadsValid) {
        for (mach_msg_type_number_t i = 0; i < threadCount; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
        vm_deallocate(mach_task_self(), (vm_address_t)threads, threadCount * sizeof(thread_t));
    }
    double now = NSProcessInfo.processInfo.systemUptime;
    double cpu = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
    id cpuPercent = NSNull.null;
    if (_previousWall && now > _previousWall && usageValid) cpuPercent = @(100 * (cpu - _previousCPU) / (now - _previousWall));
    _previousWall = now; _previousCPU = cpu;
    return @{
        @"inputDeviceUID": _inputUID ?: @"", @"outputDeviceUID": _outputUID ?: @"",
        @"sourceLanguage": _sourceLanguage ?: @"", @"explicitRouting": @(_explicitRouting),
        @"running": @(self.running), @"aec": @(self.voiceProcessingEnabled),
        @"audioError": _audioError ?: NSNull.null, @"configurationRestarts": @(_configurationRestarts),
        @"noiseSuppression": @(self.voiceProcessingEnabled && !_engine.inputNode.voiceProcessingBypassed),
        @"referenceDepth": @(a.referenceDepth), @"referenceDropped": @(a.referenceDropped),
        @"selfPlaybackBlocks": @(a.selfPlaybackBlocks), @"acceptedBlocks": @(a.acceptedBlocks),
        @"vadSpeaking": @(a.vad.speaking), @"vadFrames": @(a.vad.frames), @"vadVoicedFrames": @(a.vad.voicedFrames),
        @"vadSegments": @(a.vad.segments), @"vadErrors": @(a.vad.errors), @"vadLatencyMs": @(a.vad.lastLatencyMs),
        @"vadDepth": @(a.vad.depth), @"vadDropped": @(a.vad.dropped),
        @"segmentsDepth": @(a.vad.segmentsQueue.currentDepth), @"segmentsCapacity": @(a.vad.segmentsQueue.capacity),
        @"segmentsDropped": @(a.vad.segmentsQueue.droppedItems), @"segmentsOldestMs": @(a.vad.segmentsQueue.oldestItemAgeMs),
        @"asrState": @(a.asr.state), @"metalActive": @(a.asr.metal), @"asrBackend": @(a.asr.backend), @"asrCompleted": @(a.asr.completed),
        @"speakerCount": @(a.asr.speakers),
        @"diarizationLoaded": @(a.asr.diarization.loaded), @"separationLoaded": @(a.asr.diarization.separationLoaded),
        @"diarizationMs": @(a.asr.diarization.latencyMs), @"embeddingMs": @(a.asr.diarization.embeddingMs),
        @"overlapMs": @(a.asr.diarization.overlapMs), @"overlapPercent": @(a.asr.diarization.overlapPercent),
        @"overlapWindows": @(a.asr.diarization.overlapWindows), @"separationCalls": @(a.asr.diarization.separationCalls),
        @"playbackUnitsSuppressed": @(a.asr.diarization.playbackUnitsSuppressed),
        @"separationMs": @(a.asr.diarization.separationMs), @"loadedVoice": @(a.conversation.loadedVoice.c_str()),
        @"asrLatencyMs": @(a.asr.lastLatencyMs), @"asrRTF": @(a.asr.rtf), @"asrError": @(a.asr.error.c_str()),
        @"translationState": @(a.conversation.translationState), @"ttsState": @(a.conversation.ttsState),
        @"translationLatencyMs": @(a.conversation.translationMs), @"ttsLatencyMs": @(a.conversation.ttsMs),
        @"ttsAudioDurationMs": @(a.conversation.ttsAudioDurationMs), @"ttsVoiceLoadMs": @(a.conversation.ttsVoiceLoadMs),
        @"ttsCachedVoices": @(a.conversation.cachedVoices.c_str()), @"ttsWarmupMs": @(a.conversation.ttsWarmupMs),
        @"ttsCacheHits": @(a.conversation.voiceCacheHits), @"ttsCacheMisses": @(a.conversation.voiceCacheMisses),
        @"partialIntervalMs": @(a.vad.partialIntervalMs), @"partialPublished": @(a.vad.partialPublished),
        @"partialReplaced": @(a.vad.partialReplaced), @"asrObsoleteJobs": @(a.asr.obsoleteJobs),
        @"asrEmptyUnits": @(a.asr.emptyUnits), @"asrEstimatedDemandMs": @(a.asr.estimatedDemandMs),
        @"languageDetections": @(a.asr.languageDetections), @"rejectedNoSpeech": @(a.asr.rejectedNoSpeech),
        @"rejectedDigitalSilence": @(a.asr.rejectedDigitalSilence),
        @"identifySpeakers": @(_processingOptions.identifySpeakers), @"speechOutput": @(_processingOptions.speechOutput),
        @"endpointSilenceMs": @(_processingOptions.endpointSilenceMs), @"manualVoiceIndex": @(_processingOptions.voiceIndex),
        @"maxQueuedSpeechSeconds": @(_processingOptions.maxQueuedSpeechSeconds),
        @"targetLanguage": @(_processingOptions.targetLanguage.c_str()),
        @"audioSkipped": @(a.conversation.audioSkipped),
        @"chunkLagMs": a.conversation.chunkLag.count ? @(a.conversation.chunkLag.last) : NSNull.null,
        @"chunkLagCount": @(a.conversation.chunkLag.count),
        @"chunkLagP50Ms": a.conversation.chunkLag.count>=30 ? @(a.conversation.chunkLag.p50) : NSNull.null,
        @"chunkLagP95Ms": a.conversation.chunkLag.count>=30 ? @(a.conversation.chunkLag.p95) : NSNull.null,
        @"asrPendingWaitMs": a.asr.pendingWait.count ? @(a.asr.pendingWait.last) : NSNull.null,
        @"asrInferenceWaitMs": a.asr.inferenceWait.count ? @(a.asr.inferenceWait.last) : NSNull.null,
        @"asrInferenceWaitP95Ms": a.asr.inferenceWait.count >= 30 ? @(a.asr.inferenceWait.p95) : NSNull.null,
        @"asrMelMs": a.asr.mel.count ? @(a.asr.mel.last) : NSNull.null,
        @"asrLanguageMs": a.asr.language.count ? @(a.asr.language.last) : NSNull.null,
        @"asrDecodeMs": a.asr.decode.count ? @(a.asr.decode.last) : NSNull.null,
        @"translationWaitMs": a.conversation.translationWait.count ? @(a.conversation.translationWait.last) : NSNull.null,
        @"ttsWaitMs": a.conversation.ttsWait.count ? @(a.conversation.ttsWait.last) : NSNull.null,
        @"playbackWaitMs": a.conversation.playbackWait.count ? @(a.conversation.playbackWait.last) : NSNull.null,
        @"playbackPendingSeconds": @(a.conversation.playbackSeconds),
        @"endpointEstimatedMs": a.conversation.endpoint.count ? @(a.conversation.endpoint.last) : NSNull.null,
        @"afterVadEndMs": a.conversation.afterVadEnd.count ? @(a.conversation.afterVadEnd.last) : NSNull.null,
        @"afterVadEndCount": @(a.conversation.afterVadEnd.count),
        @"afterVadEndP50Ms": a.conversation.afterVadEnd.count >= 30 ? @(a.conversation.afterVadEnd.p50) : NSNull.null,
        @"afterVadEndP95Ms": a.conversation.afterVadEnd.count >= 30 ? @(a.conversation.afterVadEnd.p95) : NSNull.null,
        @"translatedCount": @(a.conversation.translated), @"synthesizedCount": @(a.conversation.synthesized),
        @"translationDepth": @(a.conversation.translationQueue.currentDepth), @"translationCapacity": @(a.conversation.translationQueue.capacity),
        @"translationDropped": @(a.conversation.translationQueue.droppedItems), @"ttsDepth": @(a.conversation.ttsQueue.currentDepth),
        @"translationOldestMs": @(a.conversation.translationQueue.oldestItemAgeMs), @"ttsOldestMs": @(a.conversation.ttsQueue.oldestItemAgeMs),
        @"ttsCapacity": @(a.conversation.ttsQueue.capacity), @"ttsDropped": @(a.conversation.ttsQueue.droppedItems),
        @"playbackDepth": @(a.playbackDepth), @"playbackDropped": @(a.playbackDropped), @"eventDrops": @(a.conversation.eventDrops),
        @"conversationError": @(a.conversation.error.c_str()),
        @"revisionConflicts": @(a.conversation.revisionConflicts), @"recoveredContinuations": @(a.conversation.recoveredContinuations),
        @"firstAudioCount": @(a.conversation.firstAudio.count), @"firstAudioMs": a.conversation.firstAudio.count ? @(a.conversation.firstAudio.last) : NSNull.null,
        @"firstAudioP50Ms": a.conversation.firstAudio.count >= 30 ? @(a.conversation.firstAudio.p50) : NSNull.null,
        @"firstAudioP95Ms": a.conversation.firstAudio.count >= 30 ? @(a.conversation.firstAudio.p95) : NSNull.null,
        @"e2eCount": @(a.conversation.endToEnd.count), @"e2eMs": a.conversation.endToEnd.count ? @(a.conversation.endToEnd.last) : NSNull.null,
        @"e2eP50Ms": a.conversation.endToEnd.count >= 30 ? @(a.conversation.endToEnd.p50) : NSNull.null,
        @"e2eP95Ms": a.conversation.endToEnd.count >= 30 ? @(a.conversation.endToEnd.p95) : NSNull.null,
        @"memoryPressure": @(memory.pressure), @"memoryReservedBytes": @(memory.reserved),
        @"memoryBudgetBytes": @(memory.limit), @"memoryAdmissionDenied": @(memory.denied),
        @"transcriptDepth": @(a.asr.resultsQueue.currentDepth), @"transcriptDropped": @(a.asr.resultsQueue.droppedItems),
        @"inputLevel": @(a.inputLevel), @"outputLevel": @(a.outputLevel), @"captureAgeMs": @(a.captureAgeMs),
        @"inputFrames": @(a.inputFrames), @"outputFrames": @(a.outputFrames),
        @"captureCallbacks": @(a.captureCallbacks), @"playbackCallbacks": @(a.playbackCallbacks),
        @"audioDepth": @(a.depth), @"audioCapacity": @64, @"audioDropped": @(a.dropped), @"audioHighWater": @(a.highWater),
        @"ramBytes": vmValid ? @(vm.phys_footprint) : NSNull.null,
        @"peakRSSBytes": usageValid ? @(usage.ru_maxrss) : NSNull.null,
        @"cpuPercent": cpuPercent, @"threads": threadsValid ? @(threadCount) : NSNull.null,
        @"thermalState": @(NSProcessInfo.processInfo.thermalState),
        @"inputLatencyMs": _running ? @((_captureEngine ?: _engine).inputNode.presentationLatency * 1000) : NSNull.null,
        @"outputLatencyMs": _running ? @(_engine.outputNode.presentationLatency * 1000) : NSNull.null
    };
}
@end
