import XCTest
// Hostless XCTest target compiles the production AppState, bridge and audio core.
// The app target retains App Sandbox; real UI/audio validation is separate.

@MainActor
final class SessionTests: XCTestCase {
    private var performanceContext: PerformanceRecording.Context {
        .init(call: true, profile: "Balanced", model: "Whisper small", modelHash: "fixture",
              requestedBackend: "coreml-ane", session: 1)
    }
    func testPerformanceReportSeparatesPathsAndExcludesContent() {
        var recording = PerformanceRecording()
        recording.start(uptime: 10)
        recording.append(outgoing: ["asrLatencyMs": 42, "text": "SECRET", "translation": "SECRET",
                                   "inputDeviceUID": "SECRET", "asrError": "SECRET", "unknownMetric": 999,
                                   "firstAudioP95Ms": NSNull(), "cpuPercent": Double.nan,
                                   "revisionConflicts": 2, "recoveredContinuations": 1],
                         incoming: ["asrLatencyMs": 91], context: performanceContext, uptime: 10)
        recording.stop(uptime: 11)
        let report = recording.report()
        XCTAssertFalse(report.contains("SECRET"))
        XCTAssertFalse(report.contains("unknownMetric="))
        XCTAssertTrue(report.contains("[outgoing PT-BR -> EN]"))
        XCTAssertTrue(report.contains("[incoming EN -> PT-BR]"))
        XCTAssertTrue(report.contains("asrLatencyMs=42"))
        XCTAssertTrue(report.contains("asrLatencyMs=91"))
        XCTAssertTrue(report.contains("asrErrorPresent=1"))
        XCTAssertTrue(report.contains("firstAudioP95Ms=NOT MEASURED"))
        XCTAssertTrue(report.contains("cpuPercent=NOT MEASURED"))
        XCTAssertTrue(report.contains("revisionConflicts=2"))
        XCTAssertTrue(report.contains("recoveredContinuations=1"))
    }
    func testPerformanceRecordingCadenceLimitsAndNoSilentReplacement() {
        var recording = PerformanceRecording(maxDuration: 3)
        recording.start(uptime: 10)
        for time in [10.0, 10.1, 10.9, 11, 12, 13, 14] {
            recording.append(outgoing: [:], incoming: [:], context: performanceContext, uptime: time)
        }
        XCTAssertEqual(recording.sampleCount, 3)
        XCTAssertEqual(recording.stopReason, .duration)
        XCTAssertFalse(recording.isRecording)
        recording.start(uptime: 15)
        XCTAssertEqual(recording.sampleCount, 3)
        XCTAssertFalse(recording.isRecording)
        var small = PerformanceRecording(maxBytes: 1)
        small.start(uptime: 10)
        small.append(outgoing: [:], incoming: [:], context: performanceContext, uptime: 10)
        XCTAssertEqual(small.stopReason, .size)
        XCTAssertEqual(small.sampleCount, 0)
        XCTAssertEqual(small.byteCount, 0)
    }
    func testPerformanceReportUTF8ExportAndWriteFailure() throws {
        let project = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let file = project.appendingPathComponent("build/performance-test-\(UUID().uuidString).txt")
        defer { try? FileManager.default.removeItem(at: file) }
        var recording = PerformanceRecording()
        var context = performanceContext; context.call = false
        recording.start(uptime: 10)
        recording.append(outgoing: ["ttsLatencyMs": 70], incoming: ["ttsLatencyMs": 999], context: context, uptime: 10)
        recording.stop(uptime: 11)
        try recording.write(to: file)
        let text = try String(contentsOf: file, encoding: .utf8)
        XCTAssertEqual(text, recording.report())
        XCTAssertTrue(text.contains("relatório de desempenho"))
        XCTAssertTrue(text.contains("ttsLatencyMs=70"))
        XCTAssertFalse(text.contains("ttsLatencyMs=999"))
        XCTAssertFalse(text.contains("[incoming"))
        XCTAssertThrowsError(try recording.write(to: file.appendingPathComponent("invalid.txt")))
        XCTAssertEqual(recording.sampleCount, 1)
    }
    func testStopKeepsExplicitPerformanceReportAndClearAllowsNewRecording() {
        let state = AppState()
        state.startPerformanceRecording()
        XCTAssertTrue(state.performanceRecording.isRecording)
        XCTAssertEqual(state.performanceRecording.sampleCount, 1)
        state.clearPerformanceRecording()
        XCTAssertTrue(state.performanceRecording.isRecording)
        state.stop()
        XCTAssertFalse(state.performanceRecording.isRecording)
        XCTAssertEqual(state.performanceRecording.stopReason, .session)
        XCTAssertEqual(state.performanceRecording.sampleCount, 1)
        XCTAssertTrue(state.snapshot.isEmpty)
        state.startPerformanceRecording()
        XCTAssertFalse(state.performanceRecording.isRecording)
        state.clearPerformanceRecording()
        state.startPerformanceRecording()
        XCTAssertTrue(state.performanceRecording.isRecording)
        state.stopPerformanceRecording()
        XCTAssertEqual(state.performanceRecording.stopReason, .user)
    }
    func testASRBackendCanOnlyChangeWhileStopped() {
        let state = AppState()
        state.selectASRBackend("coreml-ane")
        XCTAssertEqual(state.selectedASRBackend, "coreml-ane")
        state.running = true
        state.selectASRBackend("coreml-gpu")
        XCTAssertEqual(state.selectedASRBackend, "coreml-ane")
        state.running = false
        state.requestingPermission = true
        state.selectASRBackend("metal")
        XCTAssertEqual(state.selectedASRBackend, "coreml-ane")
        state.requestingPermission = false
        state.selectASRBackend("unsupported")
        XCTAssertEqual(state.selectedASRBackend, "coreml-ane")
        state.selectASRBackend("")
        XCTAssertEqual(state.selectedASRBackend, "")
    }
    private var routeDevices: [AudioDevice] { [
        AudioDevice(id: "mic", name: "Microphone", inputChannels: 1, outputChannels: 0, isVirtual: false),
        AudioDevice(id: "phones", name: "Headphones", inputChannels: 0, outputChannels: 2, isVirtual: false),
        AudioDevice(id: "from-call", name: "Virtual A", inputChannels: 2, outputChannels: 2, isVirtual: true),
        AudioDevice(id: "to-call", name: "Virtual B", inputChannels: 2, outputChannels: 2, isVirtual: true)
    ] }
    private var callRoute: AudioRouting { AudioRouting(mode: .call, microphone: "mic", listeningOutput: "phones", callInput: "from-call", callOutput: "to-call", headphonesConfirmed: true) }
    func testMissingVirtualAudioExplainsEmptyCallSelectors() {
        var route = AudioRouting(); route.mode = .call
        let physical = routeDevices.filter { !$0.isVirtual }
        XCTAssertTrue(AudioRouting.callDevices(in: physical).isEmpty)
        XCTAssertTrue(route.validationError(devices: physical)?.contains("não preparado") == true)
        route.discoverTranslatorDevices(physical)
        XCTAssertTrue(route.callInput.isEmpty && route.callOutput.isEmpty)
    }
    func testTranslatorAudioDiscoveryKeepsDirectionsAndUserChoices() {
        let devices = [
            AudioDevice(id: AudioRouting.translatorMicrophoneUID, name: "LocalVoiceTranslator Microphone", inputChannels: 2, outputChannels: 2, isVirtual: true),
            AudioDevice(id: AudioRouting.translatorSpeakersUID, name: "LocalVoiceTranslator Speakers", inputChannels: 2, outputChannels: 2, isVirtual: true)
        ]
        var route = AudioRouting(mode: .call)
        route.discoverTranslatorDevices(devices)
        XCTAssertEqual(route.callOutput, AudioRouting.translatorMicrophoneUID)
        XCTAssertEqual(route.callInput, AudioRouting.translatorSpeakersUID)
        route.callOutput = "disconnected-user-choice"
        route.discoverTranslatorDevices(devices)
        XCTAssertEqual(route.callOutput, "disconnected-user-choice")
        XCTAssertFalse(route.headphonesConfirmed)
    }
    func testNativeDevicesArePreferredOnlyForEmptySelections() {
        let native = [
            AudioDevice(id: NativeCallAudioSession.microphoneUID(), name: "Native microphone", inputChannels: 2, outputChannels: 2, isVirtual: true),
            AudioDevice(id: NativeCallAudioSession.speakersUID(), name: "Native speakers", inputChannels: 2, outputChannels: 2, isVirtual: true)
        ]
        XCTAssertTrue(AudioRouting.nativeDevicesAvailable(in: native + routeDevices))
        XCTAssertFalse(AudioRouting.nativeDevicesAvailable(in: routeDevices))
        XCTAssertFalse(AudioRouting.nativeDevicesAvailable(in: Array(native.prefix(1))))
        var incomplete = native; incomplete[1].inputChannels = 0
        XCTAssertFalse(AudioRouting.nativeDevicesAvailable(in: incomplete))
        var route = AudioRouting(mode: .call, microphone: "mic", listeningOutput: "phones", headphonesConfirmed: true)
        route.discoverTranslatorDevices(native + routeDevices)
        XCTAssertEqual(route.callOutput, NativeCallAudioSession.microphoneUID())
        XCTAssertEqual(route.callInput, NativeCallAudioSession.speakersUID())
        XCTAssertNil(route.validationError(devices: native + routeDevices))
        var reversed = route
        reversed.callOutput = route.callInput; reversed.callInput = route.callOutput
        XCTAssertNotNil(reversed.validationError(devices: native + routeDevices))
        route.callInput = "from-call"
        route.discoverTranslatorDevices(native)
        XCTAssertEqual(route.callInput, "from-call")
    }
    func testNativePreparationRequiresPhysicalChoicesBeforeCreatingDevices() async {
        let state = AppState()
        state.selectAudioRouting(AudioRouting(mode: .call))
        await state.prepareNativeCallAudio()
        XCTAssertFalse(state.nativeCallAudioReady)
        XCTAssertFalse(state.requestingPermission)
        XCTAssertFalse(state.running)
        XCTAssertNotNil(state.error)
        state.closeNativeCallAudio()
        XCTAssertFalse(state.nativeCallAudioReady)
    }
    func testNativePreparationDoesNotRequireVirtualDevicesThatItWillCreate() {
        let physical = routeDevices.filter { !$0.isVirtual }
        var route = AudioRouting(mode: .call)
        XCTAssertTrue(route.nativePreparationError(devices: physical)?.contains("Seu microfone") == true)
        route.microphone = "mic"
        XCTAssertTrue(route.nativePreparationError(devices: physical)?.contains("Ouvir traduções") == true)
        route.listeningOutput = "phones"
        XCTAssertTrue(route.nativePreparationError(devices: physical)?.contains("Marque") == true)
        route.headphonesConfirmed = true
        XCTAssertNil(route.nativePreparationError(devices: physical))
        XCTAssertTrue(route.callInput.isEmpty && route.callOutput.isEmpty)
        XCTAssertNotNil(route.nativePreparationError(devices: physical.filter { $0.id != "phones" }))
    }
    func testNativePreparationExplainsCombinedInputOutputDevice() {
        let combined = AudioDevice(id: "bluetooth", name: "Headset", inputChannels: 1, outputChannels: 2, isVirtual: false)
        var route = callRoute; route.listeningOutput = combined.id
        XCTAssertTrue(route.nativePreparationError(devices: routeDevices + [combined])?.contains("Bluetooth") == true)
        route.listeningOutput = "from-call"
        XCTAssertNotNil(route.nativePreparationError(devices: routeDevices))
    }
    func testCallRequiresFourDevicesAndKeepsDirectionsSeparate() {
        XCTAssertNil(callRoute.validationError(devices: routeDevices))
        var route = callRoute
        route.callOutput = route.callInput
        XCTAssertNotNil(route.validationError(devices: routeDevices))
        route = callRoute; route.callInput = "mic"
        XCTAssertNotNil(route.validationError(devices: routeDevices))
        route = callRoute; route.microphone = "to-call"
        XCTAssertNotNil(route.validationError(devices: routeDevices))
        route = callRoute; route.listeningOutput = "from-call"
        XCTAssertNotNil(route.validationError(devices: routeDevices))
    }
    func testDisconnectedCallDeviceDoesNotFallBackToDefault() {
        for missing in routeDevices {
            XCTAssertNotNil(callRoute.validationError(devices: routeDevices.filter { $0.id != missing.id }))
        }
        var route = callRoute; route.callOutput = ""
        XCTAssertNotNil(route.validationError(devices: routeDevices))
    }
    func testCustomRoutingRequiresHeadphonesAndDefaultPreservesLocalMode() {
        XCTAssertNil(AudioRouting().validationError(devices: []))
        var route = callRoute; route.headphonesConfirmed = false
        XCTAssertNotNil(route.validationError(devices: routeDevices))
        route = AudioRouting(microphone: "mic", listeningOutput: "phones")
        XCTAssertNotNil(route.validationError(devices: routeDevices))
        route.headphonesConfirmed = true
        XCTAssertNil(route.validationError(devices: routeDevices))
    }
    func testRoutingCannotChangeDuringCaptureOrPermissionRequest() {
        let state = AppState()
        state.running = true; state.selectAudioRouting(callRoute)
        XCTAssertEqual(state.audioRouting.mode, .inPerson)
        state.running = false; state.requestingPermission = true; state.selectAudioRouting(callRoute)
        XCTAssertEqual(state.audioRouting.mode, .inPerson)
        state.stop(); state.selectAudioRouting(callRoute)
        XCTAssertEqual(state.audioRouting, callRoute)
        state.stop()
        XCTAssertEqual(state.audioRouting, callRoute)
        XCTAssertTrue(state.incomingSnapshot.isEmpty)
    }
    func testMissingResourcesFailBeforeMicrophonePermissionOrCapture() async {
        // This hostless target intentionally has no application model resources.
        let state = AppState()
        XCTAssertNil(state.asrModel)
        await state.start()
        XCTAssertFalse(state.running)
        XCTAssertFalse(state.requestingPermission)
        XCTAssertNotNil(state.error)
        XCTAssertEqual(state.number("inputFrames"), 0)
        state.stop()
    }
    func testStopClearsSessionAndHistory() {
        let state = AppState()
        state.cpuHistory = [1, 2]
        state.ramHistory = [100]
        state.error = "previous error"
        state.transcripts = [AppState.Transcript(text: "unit test fixture", language: "pt")]
        state.participants = [7: AppState.Participant(id: 7)]
        state.firstAudioHistory = [123]
        state.stop()
        XCTAssertFalse(state.running)
        XCTAssertFalse(state.requestingPermission)
        XCTAssertNil(state.error)
        XCTAssertTrue(state.cpuHistory.isEmpty)
        XCTAssertTrue(state.ramHistory.isEmpty)
        XCTAssertTrue(state.snapshot.isEmpty)
        XCTAssertTrue(state.transcripts.isEmpty)
        XCTAssertTrue(state.participants.isEmpty)
        XCTAssertTrue(state.firstAudioHistory.isEmpty)
    }
    func testProcessMetricsAreMeasured() {
        let state = AppState()
        state.refresh()
        XCTAssertGreaterThan(state.number("ramBytes") ?? 0, 0)
        XCTAssertGreaterThan(state.number("threads") ?? 0, 0)
        XCTAssertEqual(state.number("inputFrames"), 0)
        state.stop()
    }
    func testHistoryIsBounded() {
        let state = AppState()
        for _ in 0..<80 { state.refresh() }
        XCTAssertLessThanOrEqual(state.cpuHistory.count, 60)
        XCTAssertLessThanOrEqual(state.ramHistory.count, 60)
        state.stop()
    }
    func testApprovedProfilesPreserveBalancedSelectionAndResourceContainment() throws {
        let project = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let name = "profile-test-" + UUID().uuidString
        let resources = project.appendingPathComponent("build/" + name)
        let outside = project.appendingPathComponent("build/" + name + "-outside.bin")
        try FileManager.default.createDirectory(at: resources, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: resources); try? FileManager.default.removeItem(at: outside) }
        for filename in ["balanced.bin", "eco.bin"] { try Data("unit fixture".utf8).write(to: resources.appendingPathComponent(filename)) }
        try Data("outside resource root".utf8).write(to: outside)
        let hash = String(repeating: "a", count: 64)
        let manifest: [String: Any] = ["models": [
            ["name": "Eco weights", "type": "asr", "filename": "eco.bin", "sha256": hash],
            ["name": "Balanced weights", "type": "asr", "filename": "balanced.bin", "sha256": hash],
            ["name": "Outside", "type": "asr", "filename": "../" + outside.lastPathComponent, "sha256": hash]
        ]]
        let selection: [String: Any] = ["filename": "balanced.bin", "sha256": hash, "profiles": [
            ["id": "Balanced", "filename": "balanced.bin", "sha256": hash],
            ["id": "Eco", "filename": "eco.bin", "sha256": hash],
            ["id": "Escape", "filename": "../" + outside.lastPathComponent, "sha256": hash]
        ]]
        try JSONSerialization.data(withJSONObject: manifest).write(to: resources.appendingPathComponent("manifest.json"))
        try JSONSerialization.data(withJSONObject: selection).write(to: resources.appendingPathComponent("selection.json"))
        let profiles = ModelCatalog.bundledProfiles(resourceRoot: resources)
        XCTAssertEqual(profiles.map(\.id), ["Balanced", "Eco"])
        XCTAssertEqual(profiles.first?.model.name, "Balanced weights")
        XCTAssertTrue(profiles.last?.model.path.hasSuffix("/eco.bin") == true)
    }
}
