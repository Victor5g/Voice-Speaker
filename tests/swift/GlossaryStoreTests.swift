import XCTest

@MainActor final class GlossaryStoreTests: XCTestCase {
    private func seed() -> Data {
        let doc = GlossaryDocument(categories: [GlossaryCategory(id: "media", name: "Mídia", enabled: true)], entries: [GlossaryEntry(term: "unboxing", categories: ["media"], mode: .always)])
        return try! JSONEncoder().encode(doc)
    }
    private func defaults() -> UserDefaults {
        let name = "local.LocalVoiceTranslator.glossary-tests.\(UUID().uuidString)"
        let store = UserDefaults(suiteName: name)!
        addTeardownBlock { store.removePersistentDomain(forName: name) }
        return store
    }
    func testEditAndCategoryPersistenceWithoutConversation() throws {
        let preferences = defaults()
        let store = GlossaryStore(defaults: preferences, seedData: seed())
        var term = store.document.entries[0]
        term.forms = ["unboxings"]
        term.directions = ["pt-en"]
        try store.update(term)
        try store.setCategory("media", enabled: false)
        let reopened = GlossaryStore(defaults: preferences, seedData: seed())
        XCTAssertEqual(reopened.document.entries[0].forms, ["unboxings"])
        XCTAssertEqual(reopened.document.entries[0].directions, ["pt-en"])
        XCTAssertFalse(reopened.document.categories[0].enabled)
        XCTAssertNil(reopened.errorMessage)
        XCTAssertTrue(reopened.engineJSON().contains("unboxings"))
    }
    func testInvalidImportPreservesPreferencesAndDocument() throws {
        let preferences = defaults()
        let store = GlossaryStore(defaults: preferences, seedData: seed())
        try store.save(store.document)
        let before = preferences.data(forKey: GlossaryStore.preferenceKey)
        let document = store.document
        var invalid = document
        invalid.entries[0].directions = ["en-fr"]
        XCTAssertThrowsError(try store.importData(JSONEncoder().encode(invalid)))
        XCTAssertEqual(store.document, document)
        XCTAssertEqual(preferences.data(forKey: GlossaryStore.preferenceKey), before)
    }
    func testCRUDAndRoundTrip() throws {
        let store = GlossaryStore(defaults: defaults(), seedData: seed())
        let entry = GlossaryEntry(term: "feedback", categories: ["media"], mode: .always)
        try store.update(entry)
        let exported = try store.exportData()
        try store.remove(entry.id)
        XCTAssertEqual(store.document.entries.count, 1)
        try store.importData(exported)
        XCTAssertEqual(store.document.entries.count, 2)
        try store.setEnabled(false)
        XCTAssertFalse(store.document.enabled)
    }
    func testDuplicateTermsRejected() throws {
        let store = GlossaryStore(defaults: defaults(), seedData: seed())
        XCTAssertThrowsError(try store.update(GlossaryEntry(term: "UNBOXING", categories: ["media"])))
        XCTAssertEqual(store.document.entries.count, 1)
    }
    func testCorruptPersistedDataFailsExplicitly() {
        let preferences = defaults()
        preferences.set(Data("bad JSON".utf8), forKey: GlossaryStore.preferenceKey)
        let store = GlossaryStore(defaults: preferences, seedData: seed())
        XCTAssertNotNil(store.errorMessage)
        XCTAssertTrue(store.engineJSON().contains("error"))
    }
    func testCategoryCreateRenameAndRestoreKeepsEntryAssociations() throws {
        let preferences = defaults()
        let store = GlossaryStore(defaults: preferences, seedData: seed())
        let original = store.document.entries[0]
        try store.updateCategory(GlossaryCategory(id: "MY-CATEGORY", name: "Trabalho", enabled: true))
        var entry = original; entry.categories.append("MY-CATEGORY")
        try store.update(entry)
        try store.updateCategory(GlossaryCategory(id: "MY-CATEGORY", name: "Tecnologia profissional", enabled: false))
        let reopened = GlossaryStore(defaults: preferences, seedData: seed())
        XCTAssertEqual(reopened.document.categories.last?.name, "Tecnologia profissional")
        XCTAssertEqual(reopened.document.entries[0].categories, ["media", "MY-CATEGORY"])
        XCTAssertEqual(reopened.document.entries[0].id, original.id)
        XCTAssertFalse(reopened.document.categories.last!.enabled)
    }
    func testCategoryRemovalRequiresExplicitDestinationForOrphansAndIsAtomic() throws {
        let preferences = defaults()
        let store = GlossaryStore(defaults: preferences, seedData: seed())
        try store.save(store.document)
        let original = store.document
        let saved = preferences.data(forKey: GlossaryStore.preferenceKey)
        XCTAssertThrowsError(try store.removeCategory("media"))
        XCTAssertEqual(store.document, original)
        XCTAssertEqual(preferences.data(forKey: GlossaryStore.preferenceKey), saved)
        try store.updateCategory(GlossaryCategory(id: "other", name: "Outros", enabled: true))
        XCTAssertThrowsError(try store.removeCategory("media", reassigningTo: "media"))
        XCTAssertThrowsError(try store.removeCategory("media", reassigningTo: "unknown"))
        try store.removeCategory("media", reassigningTo: "other")
        XCTAssertEqual(store.document.categories.map(\.id), ["other"])
        XCTAssertEqual(store.document.entries.count, 1)
        XCTAssertEqual(store.document.entries[0].categories, ["other"])
        XCTAssertEqual(store.document.entries[0].term, original.entries[0].term)
        XCTAssertEqual(GlossaryStore(defaults: preferences, seedData: seed()).document, store.document)
    }
    func testRemovingSharedCategoryPreservesOtherCategoriesWithoutReassigning() throws {
        let store = GlossaryStore(defaults: defaults(), seedData: seed())
        try store.updateCategory(GlossaryCategory(id: "other", name: "Outros", enabled: true))
        var entry = store.document.entries[0]; entry.categories.append("other")
        try store.update(entry)
        try store.removeCategory("media")
        XCTAssertEqual(store.document.entries[0].categories, ["other"])
        XCTAssertEqual(store.document.entries.count, 1)
    }
    func testWhitespaceControlAndListBoundsRejectedBeforeSaving() throws {
        let store = GlossaryStore(defaults: defaults(), seedData: seed())
        let original = store.document
        for invalid in [" ", " feedback", "feedback ", "feed\u{0000}back", "feed\nback"] {
            var entry = original.entries[0]; entry.term = invalid
            XCTAssertThrowsError(try store.update(entry))
        }
        var entry = original.entries[0]
        entry.categories = Array(repeating: "media", count: 129)
        XCTAssertThrowsError(try store.update(entry))
        entry = original.entries[0]
        entry.directions = Array(repeating: "en-pt", count: 129)
        XCTAssertThrowsError(try store.update(entry))
        XCTAssertEqual(store.document, original)
    }
    func testAliasAndCanonicalUnicodeDuplicatesCannotCreateCompetingRules() throws {
        let store = GlossaryStore(defaults: defaults(), seedData: seed())
        var entry = store.document.entries[0]; entry.forms = ["unboxings"]
        try store.update(entry)
        XCTAssertThrowsError(try store.update(GlossaryEntry(term: "UNBOXINGS", categories: ["media"])))
        try store.update(GlossaryEntry(term: "Pokémon", categories: ["media"]))
        XCTAssertThrowsError(try store.update(GlossaryEntry(term: "POKE\u{0301}MON", categories: ["media"])))
        XCTAssertThrowsError(try store.updateCategory(GlossaryCategory(id: "other", name: "MI\u{0301}DIA", enabled: true)))
        XCTAssertEqual(store.document.entries.count, 2)
    }
}
