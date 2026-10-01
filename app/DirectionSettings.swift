import Foundation

enum TranslationLanguage: String, Codable, CaseIterable, Identifiable {
    case automatic = "", portuguese = "pt", english = "en"
    var id: String { rawValue }
    var label: String {
        switch self {
        case .automatic: return "Automático"
        case .portuguese: return "Português brasileiro"
        case .english: return "Inglês"
        }
    }
}

struct DirectionSettings: Codable, Equatable {
    var sourceLanguage: TranslationLanguage = .automatic
    var targetLanguage: TranslationLanguage = .automatic
    var identifySpeakers = true
    var speechOutput = true
    var voiceIndex = 0
    var endpointSilenceMs = 300
    var maxQueuedSpeechSeconds = 6

    static let outgoing = DirectionSettings()
    static let incoming = DirectionSettings(targetLanguage: .portuguese)

    // Only PT/EN are supported. Apply a manual inversion atomically so the
    // intermediate same-language pair cannot make the picker reject a choice.
    mutating func selectSourceLanguage(_ language: TranslationLanguage) {
        sourceLanguage = language
        if language != .automatic && targetLanguage == language {
            targetLanguage = language == .portuguese ? .english : .portuguese
        }
    }

    mutating func selectTargetLanguage(_ language: TranslationLanguage) {
        targetLanguage = language
        if language != .automatic && sourceLanguage == language {
            sourceLanguage = language == .portuguese ? .english : .portuguese
        }
    }

    var validationError: String? {
        if sourceLanguage != .automatic && sourceLanguage == targetLanguage {
            return "Escolha idiomas diferentes para origem e tradução."
        }
        if !(0...2).contains(voiceIndex) { return "Selecione uma das vozes locais disponíveis." }
        if !(200...1000).contains(endpointSilenceMs) { return "A pausa deve ficar entre 200 e 1.000 ms." }
        if !(3...15).contains(maxQueuedSpeechSeconds) { return "O limite da fila deve ficar entre 3 e 15 segundos." }
        return nil
    }

    var routeLabel: String {
        let source = sourceLanguage == .automatic ? "Auto" : sourceLanguage == .portuguese ? "PT-BR" : "EN"
        let target = targetLanguage == .automatic ? "idioma oposto" : targetLanguage == .portuguese ? "PT-BR" : "EN"
        return "\(source) → \(target)"
    }

    func voiceLabel(_ index: Int) -> String {
        let pt = ["Faber", "Jeff", "Edresson"]
        let en = ["Lessac", "Ryan", "Amy"]
        guard (0...2).contains(index) else { return "Voz indisponível" }
        switch targetLanguage {
        case .portuguese: return pt[index]
        case .english: return en[index]
        case .automatic: return "\(pt[index]) / \(en[index])"
        }
    }

    func engineJSON(glossaryJSON: String? = nil) throws -> String {
        if let issue = validationError { throw DirectionSettingsError.invalid(issue) }
        var value: [String: Any] = ["sourceLanguage": sourceLanguage.rawValue,
                                  "targetLanguage": targetLanguage.rawValue,
                                  "identifySpeakers": identifySpeakers,
                                  "speechOutput": speechOutput, "voiceIndex": voiceIndex,
                                  "endpointSilenceMs": endpointSilenceMs,
                                  "maxQueuedSpeechSeconds": maxQueuedSpeechSeconds]
        if let glossaryJSON { value["glossaryJSON"] = glossaryJSON }
        return String(decoding: try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys]), as: UTF8.self)
    }
}

enum DirectionSettingsError: LocalizedError {
    case invalid(String)
    var errorDescription: String? {
        switch self { case .invalid(let message): return message }
    }
}

/// This stores configuration only; captured/transcribed content never enters defaults.
struct DirectionPreferences: Codable, Equatable {
    var outgoing = DirectionSettings.outgoing
    var incoming = DirectionSettings.incoming
    static let storageKey = "processingDirections.v1"

    func loadRoute(incoming participant: Bool) -> DirectionSettings {
        participant ? incoming : outgoing
    }

    @discardableResult
    mutating func select(_ value: DirectionSettings, incoming participant: Bool,
                         sessionActive: Bool, defaults: UserDefaults) -> Bool {
        guard !sessionActive, value.validationError == nil else { return false }
        if participant { incoming = value } else { outgoing = value }
        if let data = try? JSONEncoder().encode(self) { defaults.set(data, forKey: Self.storageKey) }
        return true
    }

    static func load(from defaults: UserDefaults) -> DirectionPreferences {
        guard let data = defaults.data(forKey: storageKey),
              let restored = try? JSONDecoder().decode(Self.self, from: data),
              restored.outgoing.validationError == nil,
              restored.incoming.validationError == nil else { return Self() }
        return restored
    }
}
