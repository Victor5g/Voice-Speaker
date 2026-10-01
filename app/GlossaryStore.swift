import Foundation
import Combine

enum GlossaryMode: String, Codable, CaseIterable, Identifiable {
    case always, context, normal
    var id: String { rawValue }
    var label: String {
        switch self { case .always: return "Sempre preservar"; case .context: return "Conforme o contexto"; case .normal: return "Traduzir normalmente" }
    }
}
struct GlossaryCategory: Codable, Identifiable, Equatable {
    var id: String
    var name: String
    var enabled: Bool
}
struct GlossaryEntry: Codable, Identifiable, Equatable {
    var id = UUID().uuidString
    var term: String
    var forms: [String] = []
    var categories: [String] = []
    var mode: GlossaryMode = .context
    var directions = ["en-pt"]
    var context: [String] = []
    var excludeContext: [String] = []
    var enabled = true
}
struct GlossaryDocument: Codable, Equatable {
    var schemaVersion = 1
    var enabled = true
    var categories: [GlossaryCategory]
    var entries: [GlossaryEntry]
    func validated() throws -> GlossaryDocument {
        func fail(_ message: String) throws { throw NSError(domain: "Glossary", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
        func validText(_ value: String) -> Bool {
            !value.isEmpty && value == value.trimmingCharacters(in: .whitespacesAndNewlines) &&
                value.utf8.count <= 160 && !value.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) })
        }
        func validList(_ values: [String]) -> Bool { values.count <= 128 && values.allSatisfy(validText) }
        func normalized(_ value: String) -> String { value.precomposedStringWithCanonicalMapping.lowercased() }
        guard schemaVersion == 1, categories.count <= 128, entries.count <= 5000 else { try fail("Formato ou capacidade do glossário inválidos."); return self }
        let categoryIDs = Set(categories.map(\.id))
        guard categoryIDs.count == categories.count, Set(categories.map { normalized($0.name) }).count == categories.count,
              categories.allSatisfy({ validText($0.id) && validText($0.name) }) else { try fail("Categorias inválidas ou duplicadas."); return self }
        guard Set(entries.map(\.id)).count == entries.count else { try fail("IDs de termos duplicados."); return self }
        // Identical terms share categories instead of creating competing rules.
        var owners: [String: String] = [:]
        for entry in entries {
            guard validText(entry.id), validText(entry.term), validList(entry.forms), validList(entry.context), validList(entry.excludeContext),
                  validList(entry.categories), !entry.categories.isEmpty, entry.categories.allSatisfy({ categoryIDs.contains($0) }),
                  validList(entry.directions), !entry.directions.isEmpty, entry.directions.allSatisfy({ ["en-pt", "pt-en"].contains($0) }) else {
                try fail("Regra inválida para \(entry.term). Apenas EN → PT-BR e PT-BR → EN são suportados."); return self
            }
            for form in [entry.term] + entry.forms {
                let key = normalized(form)
                if let owner = owners[key], owner != entry.id {
                    try fail("Termo ou variante duplicada: \(form). Reúna as categorias em uma única entrada.")
                }
                owners[key] = entry.id
            }
        }
        return self
    }
}

@MainActor final class GlossaryStore: ObservableObject {
    static let shared = GlossaryStore()
    static let preferenceKey = "translation.glossary.v1"
    @Published private(set) var document: GlossaryDocument
    @Published private(set) var revision = 0
    @Published var errorMessage: String?
    private let defaults: UserDefaults
    private let seedData: Data?
    private let encoder: JSONEncoder
    init(defaults: UserDefaults = .standard, seedData: Data? = nil) {
        self.defaults = defaults
        self.seedData = seedData
        encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .prettyPrinted, .withoutEscapingSlashes]
        document = GlossaryDocument(categories: [], entries: [])
        do {
            let data: Data
            if let saved = defaults.data(forKey: Self.preferenceKey) { data = saved }
            else if let seedData { data = seedData }
            else if let url = Bundle.main.url(forResource: "glossary-defaults", withExtension: "json") { data = try Data(contentsOf: url) }
            else { data = try Data(contentsOf: URL(fileURLWithPath: FileManager.default.currentDirectoryPath).appendingPathComponent("app/Resources/glossary-defaults.json")) }
            guard data.count <= 2 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
            document = try JSONDecoder().decode(GlossaryDocument.self, from: data).validated()
        } catch { errorMessage = "Não foi possível carregar o glossário: \(error.localizedDescription)" }
    }
    /// Validate factory data before replacing any edited or corrupted preference.
    func restoreDefaults() throws {
        let data: Data
        if let seedData { data = seedData }
        else if let url = Bundle.main.url(forResource: "glossary-defaults", withExtension: "json") {
            data = try Data(contentsOf: url)
        } else {
            data = try Data(contentsOf: URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
                .appendingPathComponent("app/Resources/glossary-defaults.json"))
        }
        guard data.count <= 2 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
        let restored = try JSONDecoder().decode(GlossaryDocument.self, from: data).validated()
        defaults.removeObject(forKey: Self.preferenceKey)
        document = restored
        errorMessage = nil
        revision += 1
    }
    func engineJSON() -> String {
        // A missing/corrupt preference fails native configuration explicitly.
        guard errorMessage == nil, let data = try? encoder.encode(document), let json = String(data: data, encoding: .utf8) else { return "{\"error\":\"Glossary unavailable\"}" }
        return json
    }
    func save(_ updated: GlossaryDocument) throws {
        let valid = try updated.validated()
        let data = try encoder.encode(valid)
        guard data.count <= 2 * 1024 * 1024 else { throw CocoaError(.fileWriteOutOfSpace) }
        defaults.set(data, forKey: Self.preferenceKey)
        document = valid
        errorMessage = nil
        revision += 1
    }
    func update(_ entry: GlossaryEntry) throws {
        var next = document
        if let index = next.entries.firstIndex(where: { $0.id == entry.id }) { next.entries[index] = entry }
        else { next.entries.append(entry) }
        try save(next)
    }
    func remove(_ id: String) throws { var next = document; next.entries.removeAll { $0.id == id }; try save(next) }
    func setEnabled(_ enabled: Bool) throws { var next = document; next.enabled = enabled; try save(next) }
    func setCategory(_ id: String, enabled: Bool) throws {
        var next = document
        guard let index = next.categories.firstIndex(where: { $0.id == id }) else { return }
        next.categories[index].enabled = enabled
        try save(next)
    }
    func updateCategory(_ category: GlossaryCategory) throws {
        var next = document
        if let index = next.categories.firstIndex(where: { $0.id == category.id }) { next.categories[index] = category }
        else { next.categories.append(category) }
        try save(next)
    }
    func removeCategory(_ id: String, reassigningTo replacement: String? = nil) throws {
        var next = document
        guard next.categories.contains(where: { $0.id == id }) else { return }
        if let replacement, replacement == id || !next.categories.contains(where: { $0.id == replacement }) {
            throw NSError(domain: "Glossary", code: 2, userInfo: [NSLocalizedDescriptionKey: "Escolha outra categoria existente para os termos vinculados."])
        }
        next.categories.removeAll { $0.id == id }
        for index in next.entries.indices where next.entries[index].categories.contains(id) {
            next.entries[index].categories.removeAll { $0 == id }
            if next.entries[index].categories.isEmpty {
                guard let replacement else {
                    throw NSError(domain: "Glossary", code: 2, userInfo: [NSLocalizedDescriptionKey: "Há termos apenas nesta categoria. Escolha outra categoria antes de remover; os termos serão mantidos."])
                }
                next.entries[index].categories = [replacement]
            }
        }
        try save(next)
    }
    func exportData() throws -> Data { try encoder.encode(document.validated()) }
    func importData(_ data: Data) throws {
        guard data.count <= 2 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
        try save(JSONDecoder().decode(GlossaryDocument.self, from: data))
    }
}
