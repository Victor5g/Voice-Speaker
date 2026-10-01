import XCTest

@MainActor
final class RestoreSettingsTests: XCTestCase {
    private func storage() -> UserDefaults {
        let name = "local.LocalVoiceTranslator.restore-tests.\(UUID().uuidString)"
        let preferences = UserDefaults(suiteName: name)!
        addTeardownBlock { preferences.removePersistentDomain(forName: name) }
        return preferences
    }
    private func seed() throws -> Data {
        try JSONEncoder().encode(GlossaryDocument(
            categories: [GlossaryCategory(id: "media", name: "Mídia", enabled: true)],
            entries: [GlossaryEntry(term: "unboxing", categories: ["media"], mode: .always)]))
    }

    func testRestoreResetsBothDirectionsAudioBackendGlossaryAndPersists() throws {
        let preferences = storage(), factoryData = try seed()
        let glossary = GlossaryStore(defaults: preferences, seedData: factoryData)
        let factory = glossary.document
        let state = AppState(preferencesDefaults: preferences)
        defer { state.stop() }
        let defaultModel = state.asrModel?.path
        state.selectDirectionSettings(DirectionSettings(sourceLanguage: .portuguese, targetLanguage: .english,
            identifySpeakers: false, speechOutput: false, voiceIndex: 2, endpointSilenceMs: 800,
            maxQueuedSpeechSeconds: 12), incoming: false)
        state.selectDirectionSettings(DirectionSettings(sourceLanguage: .english, targetLanguage: .portuguese,
            identifySpeakers: false, voiceIndex: 1, endpointSilenceMs: 220), incoming: true)
        state.selectASRBackend("coreml-ane")
        state.selectAudioRouting(AudioRouting(mode: .call, microphone: "custom-mic", listeningOutput: "custom-output",
            callInput: "custom-in", callOutput: "custom-out", headphonesConfirmed: true))
        try glossary.update(GlossaryEntry(term: "custom term", categories: ["media"]))
        try glossary.setCategory("media", enabled: false)
        try glossary.setEnabled(false)
        preferences.set("keep", forKey: "unrelated.preference")
        state.startPerformanceRecording()
        let recordedSamples = state.performanceRecording.sampleCount

        XCTAssertTrue(state.restoreDefaultSettings(glossary: glossary))
        XCTAssertEqual(state.directionPreferences, DirectionPreferences())
        XCTAssertEqual(state.audioRouting, AudioRouting())
        XCTAssertEqual(state.selectedProfile, "Balanced")
        XCTAssertEqual(state.selectedASRBackend, "")
        XCTAssertEqual(state.asrModel?.path, defaultModel)
        XCTAssertFalse(state.nativeCallAudioReady)
        XCTAssertFalse(state.running)
        XCTAssertEqual(glossary.document, factory)
        XCTAssertNil(glossary.errorMessage)
        XCTAssertNil(state.error)
        XCTAssertNotNil(state.settingsResetMessage)
        XCTAssertNil(preferences.data(forKey: DirectionPreferences.storageKey))
        XCTAssertNil(preferences.data(forKey: GlossaryStore.preferenceKey))
        XCTAssertEqual(preferences.string(forKey: "unrelated.preference"), "keep")
        XCTAssertEqual(state.performanceRecording.sampleCount, recordedSamples)
        XCTAssertFalse(state.performanceRecording.isRecording)
        let reopened = AppState(preferencesDefaults: preferences)
        defer { reopened.stop() }
        XCTAssertEqual(reopened.directionPreferences, DirectionPreferences())
        XCTAssertEqual(GlossaryStore(defaults: preferences, seedData: factoryData).document, factory)
    }

    func testRestoreCannotChangeActiveOrPermissionPendingSession() throws {
        let preferences = storage()
        let glossary = GlossaryStore(defaults: preferences, seedData: try seed())
        try glossary.setEnabled(false)
        let state = AppState(preferencesDefaults: preferences)
        defer { state.stop() }
        state.selectSourceLanguage(.english, incoming: false)
        let original = state.directionPreferences
        let saved = preferences.data(forKey: GlossaryStore.preferenceKey)
        state.running = true
        XCTAssertFalse(state.restoreDefaultSettings(glossary: glossary))
        XCTAssertTrue(state.running)
        state.running = false
        state.requestingPermission = true
        XCTAssertFalse(state.restoreDefaultSettings(glossary: glossary))
        XCTAssertTrue(state.requestingPermission)
        XCTAssertEqual(state.directionPreferences, original)
        XCTAssertEqual(preferences.data(forKey: GlossaryStore.preferenceKey), saved)
        XCTAssertFalse(glossary.document.enabled)
        XCTAssertNil(state.settingsResetMessage)
    }

    func testInvalidFactoryGlossaryDoesNotPartiallyResetSettings() throws {
        let preferences = storage()
        let validStore = GlossaryStore(defaults: preferences, seedData: try seed())
        try validStore.setEnabled(false)
        let glossary = GlossaryStore(defaults: preferences, seedData: Data("invalid factory JSON".utf8))
        let original = glossary.document
        let state = AppState(preferencesDefaults: preferences)
        defer { state.stop() }
        state.selectSourceLanguage(.english, incoming: false)
        state.selectASRBackend("coreml-ane")
        state.selectAudioRouting(AudioRouting(mode: .call, microphone: "chosen"))
        let routes = state.directionPreferences, audio = state.audioRouting
        let saved = preferences.data(forKey: GlossaryStore.preferenceKey)
        XCTAssertFalse(state.restoreDefaultSettings(glossary: glossary))
        XCTAssertEqual(state.directionPreferences, routes)
        XCTAssertEqual(state.audioRouting, audio)
        XCTAssertEqual(state.selectedASRBackend, "coreml-ane")
        XCTAssertEqual(glossary.document, original)
        XCTAssertEqual(preferences.data(forKey: GlossaryStore.preferenceKey), saved)
        XCTAssertNotNil(state.error)
        XCTAssertNil(state.settingsResetMessage)
    }

    func testRestoreRecoversCorruptSavedGlossaryUsingFactoryData() throws {
        let preferences = storage(), factory = try seed()
        preferences.set(Data("bad stored JSON".utf8), forKey: GlossaryStore.preferenceKey)
        let glossary = GlossaryStore(defaults: preferences, seedData: factory)
        XCTAssertNotNil(glossary.errorMessage)
        let state = AppState(preferencesDefaults: preferences)
        defer { state.stop() }
        XCTAssertTrue(state.restoreDefaultSettings(glossary: glossary))
        XCTAssertNil(glossary.errorMessage)
        XCTAssertEqual(glossary.document, try JSONDecoder().decode(GlossaryDocument.self, from: factory))
        XCTAssertNil(preferences.data(forKey: GlossaryStore.preferenceKey))
    }
}
