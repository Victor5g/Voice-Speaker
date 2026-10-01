import XCTest

@MainActor
final class DirectionSettingsTests: XCTestCase {
    private func isolatedDefaults() -> (String, UserDefaults) {
        let name = "local.LocalVoiceTranslator.direction-tests.\(UUID().uuidString)"
        return (name, UserDefaults(suiteName: name)!)
    }

    func testDefaultsKeepAutomaticDetectionAndIndependentDirections() throws {
        let defaults = DirectionPreferences()
        XCTAssertEqual(defaults.outgoing.sourceLanguage, .automatic)
        XCTAssertEqual(defaults.incoming.sourceLanguage, .automatic)
        XCTAssertEqual(defaults.outgoing.targetLanguage, .automatic)
        XCTAssertEqual(defaults.incoming.targetLanguage, .portuguese)
        XCTAssertTrue(defaults.outgoing.identifySpeakers)
        XCTAssertTrue(defaults.incoming.speechOutput)
        XCTAssertEqual(defaults.outgoing.endpointSilenceMs, 300)
        XCTAssertEqual(defaults.outgoing.maxQueuedSpeechSeconds, 6)
    }

    func testPersistedRoutesRestoreManualChoicesWithoutChangingOtherDirection() throws {
        let (name, storage) = isolatedDefaults()
        defer { storage.removePersistentDomain(forName: name) }
        var preferences = DirectionPreferences()
        let custom = DirectionSettings(sourceLanguage: .portuguese, targetLanguage: .english,
                                       identifySpeakers: false, speechOutput: false, voiceIndex: 2, endpointSilenceMs: 240)
        XCTAssertTrue(preferences.select(custom, incoming: false, sessionActive: false, defaults: storage))
        XCTAssertEqual(preferences.incoming, .incoming)
        let restored = DirectionPreferences.load(from: storage)
        XCTAssertEqual(restored.outgoing, custom)
        XCTAssertEqual(restored.incoming, .incoming)
        var incoming = restored.incoming
        incoming.sourceLanguage = .english; incoming.voiceIndex = 1; incoming.identifySpeakers = false
        XCTAssertTrue(preferences.select(incoming, incoming: true, sessionActive: false, defaults: storage))
        XCTAssertEqual(DirectionPreferences.load(from: storage).outgoing, custom)
        XCTAssertEqual(DirectionPreferences.load(from: storage).incoming, incoming)
    }

    func testJSONMappingKeepsManualLanguageVoiceAndTextOnlyPerEngine() throws {
        let outgoing = DirectionSettings(sourceLanguage: .portuguese, targetLanguage: .english,
                                         identifySpeakers: false, speechOutput: false, voiceIndex: 2, endpointSilenceMs: 220)
        let raw = try outgoing.engineJSON(glossaryJSON: "{\"enabled\":false}")
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(raw.utf8)) as? [String: Any])
        XCTAssertEqual(json["sourceLanguage"] as? String, "pt")
        XCTAssertEqual(json["targetLanguage"] as? String, "en")
        XCTAssertEqual(json["identifySpeakers"] as? Bool, false)
        XCTAssertEqual(json["speechOutput"] as? Bool, false)
        XCTAssertEqual(json["voiceIndex"] as? Int, 2)
        XCTAssertEqual(json["endpointSilenceMs"] as? Int, 220)
        XCTAssertEqual(json["maxQueuedSpeechSeconds"] as? Int, 6)
        XCTAssertEqual(json["glossaryJSON"] as? String, "{\"enabled\":false}")
        let incoming = try JSONSerialization.jsonObject(with: Data(DirectionSettings.incoming.engineJSON().utf8)) as! [String: Any]
        XCTAssertEqual(incoming["sourceLanguage"] as? String, "")
        XCTAssertEqual(incoming["targetLanguage"] as? String, "pt")
        XCTAssertEqual(incoming["voiceIndex"] as? Int, 0)
        XCTAssertEqual(incoming["speechOutput"] as? Bool, true)
    }

    func testInvalidStoredSettingsFailToDefaultsAndInvalidEditsDoNotPersist() throws {
        let (name, storage) = isolatedDefaults()
        defer { storage.removePersistentDomain(forName: name) }
        storage.set(Data("invalid-json".utf8), forKey: DirectionPreferences.storageKey)
        XCTAssertEqual(DirectionPreferences.load(from: storage), DirectionPreferences())
        var preferences = DirectionPreferences()
        for invalid in [DirectionSettings(sourceLanguage: .english, targetLanguage: .english),
                        DirectionSettings(voiceIndex: 3), DirectionSettings(endpointSilenceMs: 199),
                        DirectionSettings(endpointSilenceMs: 1001), DirectionSettings(maxQueuedSpeechSeconds: 2),
                        DirectionSettings(maxQueuedSpeechSeconds: 16)] {
            XCTAssertNotNil(invalid.validationError)
            XCTAssertThrowsError(try invalid.engineJSON())
            XCTAssertFalse(preferences.select(invalid, incoming: false, sessionActive: false, defaults: storage))
            var corrupt = DirectionPreferences(); corrupt.outgoing = invalid
            storage.set(try JSONEncoder().encode(corrupt), forKey: DirectionPreferences.storageKey)
            XCTAssertEqual(DirectionPreferences.load(from: storage), DirectionPreferences())
        }
        XCTAssertEqual(preferences, DirectionPreferences())
    }

    func testAppStateGuardsBusySessionAndRestoresAfterRestart() {
        let (name, storage) = isolatedDefaults()
        defer { storage.removePersistentDomain(forName: name) }
        let state = AppState(preferencesDefaults: storage)
        var choice = state.directionSettings(incoming: false)
        choice.identifySpeakers = false; choice.voiceIndex = 1
        state.running = true
        state.selectDirectionSettings(choice, incoming: false)
        XCTAssertEqual(state.directionPreferences, DirectionPreferences())
        state.running = false; state.requestingPermission = true
        state.selectDirectionSettings(choice, incoming: false)
        XCTAssertEqual(state.directionPreferences, DirectionPreferences())
        state.requestingPermission = false
        state.selectDirectionSettings(choice, incoming: false)
        XCTAssertEqual(state.directionPreferences.outgoing, choice)
        XCTAssertEqual(state.directionPreferences.incoming, .incoming)
        state.stop()
        let next = AppState(preferencesDefaults: storage)
        XCTAssertEqual(next.directionPreferences.outgoing, choice)
        next.stop()
    }

    func testVoiceLabelsMatchDestinationAndStablePairIndices() {
        XCTAssertEqual(DirectionSettings(targetLanguage: .english).voiceLabel(2), "Amy")
        XCTAssertEqual(DirectionSettings(targetLanguage: .portuguese).voiceLabel(2), "Edresson")
        XCTAssertEqual(DirectionSettings().voiceLabel(1), "Jeff / Ryan")
    }

    func testLanguagePairCanBeReversedBySelectingSourceFirst() {
        var settings = DirectionSettings(sourceLanguage: .portuguese, targetLanguage: .english)
        settings.selectSourceLanguage(.english)
        XCTAssertEqual(settings.sourceLanguage, .english)
        XCTAssertEqual(settings.targetLanguage, .portuguese)
        XCTAssertNil(settings.validationError)
        settings.selectTargetLanguage(.portuguese)
        XCTAssertEqual(settings.sourceLanguage, .english)
        XCTAssertEqual(settings.targetLanguage, .portuguese)
        XCTAssertNil(settings.validationError)
    }

    func testLanguagePairCanBeReversedBySelectingTargetFirst() {
        var settings = DirectionSettings(sourceLanguage: .portuguese, targetLanguage: .english)
        settings.selectTargetLanguage(.portuguese)
        XCTAssertEqual(settings.sourceLanguage, .english)
        XCTAssertEqual(settings.targetLanguage, .portuguese)
        XCTAssertNil(settings.validationError)
        settings.selectSourceLanguage(.english)
        XCTAssertEqual(settings.sourceLanguage, .english)
        XCTAssertEqual(settings.targetLanguage, .portuguese)
        XCTAssertNil(settings.validationError)
    }

    func testIncomingManualPortugueseAdjustsDestinationAndPreservesOtherSettings() {
        var settings = DirectionSettings.incoming
        settings.identifySpeakers = false
        settings.speechOutput = false
        settings.voiceIndex = 2
        settings.endpointSilenceMs = 240
        settings.maxQueuedSpeechSeconds = 4
        settings.selectSourceLanguage(.portuguese)
        XCTAssertEqual(settings, DirectionSettings(sourceLanguage: .portuguese, targetLanguage: .english,
                                                   identifySpeakers: false, speechOutput: false, voiceIndex: 2,
                                                   endpointSilenceMs: 240, maxQueuedSpeechSeconds: 4))
        XCTAssertNil(settings.validationError)
    }

    func testAutomaticLanguageSelectionPreservesTheOtherField() {
        var settings = DirectionSettings(sourceLanguage: .portuguese, targetLanguage: .english)
        settings.selectSourceLanguage(.automatic)
        XCTAssertEqual(settings.sourceLanguage, .automatic)
        XCTAssertEqual(settings.targetLanguage, .english)
        settings.selectTargetLanguage(.portuguese)
        XCTAssertEqual(settings.sourceLanguage, .automatic)
        XCTAssertEqual(settings.targetLanguage, .portuguese)
        settings.selectTargetLanguage(.automatic)
        XCTAssertEqual(settings.sourceLanguage, .automatic)
        XCTAssertEqual(settings.targetLanguage, .automatic)
        settings.selectSourceLanguage(.english)
        XCTAssertEqual(settings.sourceLanguage, .english)
        XCTAssertEqual(settings.targetLanguage, .automatic)
        settings.selectTargetLanguage(.automatic)
        XCTAssertEqual(settings.sourceLanguage, .english)
        XCTAssertNil(settings.validationError)
    }

    func testAppStateLanguageSelectionsPersistAndKeepDirectionsIndependent() {
        let (name, storage) = isolatedDefaults()
        defer { storage.removePersistentDomain(forName: name) }
        let state = AppState(preferencesDefaults: storage)
        state.selectSourceLanguage(.portuguese, incoming: false)
        state.selectTargetLanguage(.english, incoming: false)
        let outgoing = state.directionPreferences.outgoing
        XCTAssertEqual(outgoing.sourceLanguage, .portuguese)
        XCTAssertEqual(outgoing.targetLanguage, .english)
        XCTAssertEqual(state.directionPreferences.incoming, .incoming)

        state.selectSourceLanguage(.portuguese, incoming: true)
        XCTAssertEqual(state.directionPreferences.incoming.sourceLanguage, .portuguese)
        XCTAssertEqual(state.directionPreferences.incoming.targetLanguage, .english)
        XCTAssertEqual(state.directionPreferences.outgoing, outgoing)
        state.selectTargetLanguage(.portuguese, incoming: true)
        XCTAssertEqual(state.directionPreferences.incoming.sourceLanguage, .english)
        XCTAssertEqual(state.directionPreferences.incoming.targetLanguage, .portuguese)
        XCTAssertEqual(state.directionPreferences.outgoing, outgoing)

        state.stop()
        let restored = AppState(preferencesDefaults: storage)
        XCTAssertEqual(restored.directionPreferences, state.directionPreferences)
        restored.stop()
    }

    func testAppStateLanguageSelectionsAreBlockedWhileRunningOrAwaitingPermission() {
        let (name, storage) = isolatedDefaults()
        defer { storage.removePersistentDomain(forName: name) }
        let state = AppState(preferencesDefaults: storage)
        for awaitingPermission in [false, true] {
            state.running = !awaitingPermission
            state.requestingPermission = awaitingPermission
            for incoming in [false, true] {
                state.selectSourceLanguage(.portuguese, incoming: incoming)
                state.selectTargetLanguage(.english, incoming: incoming)
                XCTAssertEqual(state.directionPreferences, DirectionPreferences())
                XCTAssertEqual(DirectionPreferences.load(from: storage), DirectionPreferences())
            }
        }
        state.requestingPermission = false
        state.selectSourceLanguage(.portuguese, incoming: true)
        XCTAssertEqual(state.directionPreferences.incoming.sourceLanguage, .portuguese)
        XCTAssertEqual(state.directionPreferences.incoming.targetLanguage, .english)
        state.stop()
    }

    func testPerformanceReportUsesConfiguredDirectionsAndNewNumericMetricsOnly() {
        var report = PerformanceRecording()
        var context = PerformanceRecording.Context(call: true, profile: "Balanced", model: "fixture",
                                                   modelHash: "fixture", requestedBackend: "metal", session: 1)
        context.outgoingRoute = DirectionSettings(sourceLanguage: .english, targetLanguage: .portuguese).routeLabel
        context.incomingRoute = DirectionSettings().routeLabel
        report.start(uptime: 10)
        report.append(outgoing: ["identifySpeakers": false, "languageDetections": 0, "audioSkipped": 2,
                                 "rejectedDigitalSilence": 3, "rejectedNoSpeech": 1,
                                 "asrLanguageMs": NSNull(), "asrDecodeMs": 200, "sourceLanguage": "en",
                                 "chunkLagMs": 1700, "targetLanguage": "pt", "text": "PRIVATE"],
                      incoming: ["speechOutput": false, "endpointSilenceMs": 300], context: context, uptime: 10)
        let text = report.report()
        XCTAssertTrue(text.contains("[outgoing EN → PT-BR]"))
        XCTAssertTrue(text.contains("[incoming Auto → idioma oposto]"))
        XCTAssertTrue(text.contains("identifySpeakers=0"))
        XCTAssertTrue(text.contains("languageDetections=0"))
        XCTAssertTrue(text.contains("rejectedDigitalSilence=3"))
        XCTAssertTrue(text.contains("rejectedNoSpeech=1"))
        XCTAssertTrue(text.contains("asrLanguageMs=NOT MEASURED"))
        XCTAssertTrue(text.contains("asrDecodeMs=200"))
        XCTAssertTrue(text.contains("sourceLanguage=en"))
        XCTAssertTrue(text.contains("O encoder continua necessário."))
        XCTAssertTrue(text.contains("audioSkipped=2"))
        XCTAssertTrue(text.contains("chunkLagMs=1700"))
        XCTAssertTrue(text.contains("targetLanguage=pt"))
        XCTAssertFalse(text.contains("PRIVATE"))
    }
}
