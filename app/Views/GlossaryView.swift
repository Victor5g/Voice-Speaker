import SwiftUI
import UniformTypeIdentifiers

private struct GlossaryJSONFile: FileDocument {
    static var readableContentTypes: [UTType] { [.json] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}

struct GlossaryView: View {
    @ObservedObject private var store = GlossaryStore.shared
    @Environment(\.dismiss) private var dismiss
    private var _search = SwiftUI.State(initialValue: "")
    private var search: String { get { _search.wrappedValue } nonmutating set { _search.wrappedValue = newValue } }
    private var _selectedCategory = SwiftUI.State(initialValue: "")
    private var selectedCategory: String { get { _selectedCategory.wrappedValue } nonmutating set { _selectedCategory.wrappedValue = newValue } }
    private var _editing = SwiftUI.State<GlossaryEntry?>(initialValue: nil)
    private var editing: GlossaryEntry? { get { _editing.wrappedValue } nonmutating set { _editing.wrappedValue = newValue } }
    private var _categoryEditor = SwiftUI.State<GlossaryCategory?>(initialValue: nil)
    private var categoryEditor: GlossaryCategory? { get { _categoryEditor.wrappedValue } nonmutating set { _categoryEditor.wrappedValue = newValue } }
    private var _importing = SwiftUI.State(initialValue: false)
    private var importing: Bool { get { _importing.wrappedValue } nonmutating set { _importing.wrappedValue = newValue } }
    private var _exporting = SwiftUI.State(initialValue: false)
    private var exporting: Bool { get { _exporting.wrappedValue } nonmutating set { _exporting.wrappedValue = newValue } }
    private var _exportFile = SwiftUI.State(initialValue: GlossaryJSONFile(data: Data()))
    private var exportFile: GlossaryJSONFile { get { _exportFile.wrappedValue } nonmutating set { _exportFile.wrappedValue = newValue } }
    private var _message = SwiftUI.State<String?>(initialValue: nil)
    private var message: String? { get { _message.wrappedValue } nonmutating set { _message.wrappedValue = newValue } }
    private var visible: [GlossaryEntry] {
        store.document.entries.filter { entry in
            (selectedCategory.isEmpty || entry.categories.contains(selectedCategory)) &&
            (search.isEmpty || ([entry.term] + entry.forms + store.document.categories.filter { entry.categories.contains($0.id) }.map(\.name)).contains { $0.localizedCaseInsensitiveContains(search) })
        }.sorted { $0.term.localizedStandardCompare($1.term) == .orderedAscending }
    }
    private func perform(_ action: () throws -> Void) { do { try action() } catch { message = error.localizedDescription } }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("Glossário").font(.title2)
                Spacer()
                Text("\(store.document.entries.count) termos únicos").foregroundStyle(.secondary)
                Button("Concluir") { dismiss() }
            }
            Toggle("Preservar termos conforme minhas regras", isOn: Binding(get: { store.document.enabled }, set: { value in perform { try store.setEnabled(value) } }))
            Text("Alterações valem na próxima sessão. Termos só são preservados nas direções marcadas. Uma categoria ativa é suficiente quando o termo pertence a várias.").font(.caption).foregroundStyle(.secondary)
            HStack {
                TextField("Pesquisar termo, variante ou categoria", text: _search.projectedValue)
                Picker("Categoria", selection: _selectedCategory.projectedValue) {
                    Text("Todas").tag("")
                    ForEach(store.document.categories) { Text($0.name).tag($0.id) }
                }.frame(maxWidth: 300)
            }
            DisclosureGroup("Gerenciar categorias") {
                Button("Adicionar categoria") { categoryEditor = GlossaryCategory(id: UUID().uuidString, name: "", enabled: true) }
                ScrollView {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading) {
                        ForEach(store.document.categories) { category in
                            HStack {
                                Toggle(category.name, isOn: Binding(get: { store.document.categories.first { $0.id == category.id }?.enabled ?? false }, set: { value in perform { try store.setCategory(category.id, enabled: value) } }))
                                Button("Editar") { categoryEditor = category }
                            }
                        }
                    }
                }.frame(maxHeight: 140)
            }
            List(visible) { entry in
                HStack {
                    Toggle("Ativar \(entry.term)", isOn: Binding(get: { store.document.entries.first { $0.id == entry.id }?.enabled ?? false }, set: { value in var copy = entry; copy.enabled = value; perform { try store.update(copy) } })).labelsHidden()
                    VStack(alignment: .leading) {
                        Text(entry.term).fontWeight(.medium)
                        Text(entry.mode.label + " · " + entry.directions.joined(separator: ", ")).font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Button("Editar") { editing = entry }
                    Button("Remover", role: .destructive) { perform { try store.remove(entry.id) } }
                }
            }
            HStack {
                Button("Adicionar termo") { editing = GlossaryEntry(term: "", categories: store.document.categories.first.map { [$0.id] } ?? []) }
                Button("Importar JSON…") { importing = true }
                Button("Exportar JSON…") { perform { exportFile = GlossaryJSONFile(data: try store.exportData()); exporting = true } }
                Spacer()
            }
            if let error = message ?? store.errorMessage { Text(error).foregroundStyle(.red).textSelection(.enabled) }
            Text("Importar substitui o glossário inteiro após validar o arquivo. O glossário salva preferências; não inclui áudio ou conversas.").font(.caption).foregroundStyle(.secondary)
        }.padding(20).frame(minWidth: 760, minHeight: 580)
        .sheet(item: _editing.projectedValue) { entry in GlossaryEntryEditor(entry: entry, categories: store.document.categories) { updated in try store.update(updated) } }
        .sheet(item: _categoryEditor.projectedValue) { category in
            GlossaryCategoryEditor(category: category, document: store.document, save: { try store.updateCategory($0) }, remove: { replacement in
                try store.removeCategory(category.id, reassigningTo: replacement)
                if selectedCategory == category.id { selectedCategory = "" }
            })
        }
        .fileImporter(isPresented: _importing.projectedValue, allowedContentTypes: [.json]) { result in
            perform {
                let url = try result.get(); let access = url.startAccessingSecurityScopedResource()
                defer { if access { url.stopAccessingSecurityScopedResource() } }
                let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
                guard size <= 2 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
                try store.importData(Data(contentsOf: url))
            }
        }
        .fileExporter(isPresented: _exporting.projectedValue, document: exportFile, contentType: .json, defaultFilename: "LocalVoiceTranslator-glossario") { result in
            if case .failure(let error) = result { message = error.localizedDescription }
        }
    }
}

private struct GlossaryCategoryEditor: View {
    @Environment(\.dismiss) private var dismiss
    private var _category: SwiftUI.State<GlossaryCategory>
    private var category: GlossaryCategory { _category.wrappedValue }
    private var _replacement = SwiftUI.State(initialValue: "")
    private var _error = SwiftUI.State<String?>(initialValue: nil)
    let document: GlossaryDocument
    let save: (GlossaryCategory) throws -> Void
    let remove: (String?) throws -> Void
    init(category: GlossaryCategory, document: GlossaryDocument,
         save: @escaping (GlossaryCategory) throws -> Void, remove: @escaping (String?) throws -> Void) {
        _category = SwiftUI.State(initialValue: category)
        self.document = document; self.save = save; self.remove = remove
    }
    private var exists: Bool { document.categories.contains { $0.id == category.id } }
    private var orphans: Int { document.entries.filter { Set($0.categories) == Set([category.id]) }.count }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(exists ? "Editar categoria" : "Adicionar categoria").font(.title2)
            TextField("Nome", text: _category.projectedValue.name)
            Toggle("Categoria ativa", isOn: _category.projectedValue.enabled)
            if exists {
                Text("Renomear mantém as regras e os termos associados. Remover não apaga termos.")
                    .font(.caption).foregroundStyle(.secondary)
                if orphans > 0 {
                    Text("\(orphans) termos pertencem apenas a esta categoria. Escolha onde mantê-los caso remova a categoria.")
                    Picker("Mover termos para", selection: _replacement.projectedValue) {
                        Text("Selecione outra categoria").tag("")
                        ForEach(document.categories.filter { $0.id != category.id }) { Text($0.name).tag($0.id) }
                    }
                }
                Button("Remover categoria", role: .destructive) {
                    do {
                        try remove(_replacement.wrappedValue.isEmpty ? nil : _replacement.wrappedValue)
                        dismiss()
                    } catch { _error.wrappedValue = error.localizedDescription }
                }
            }
            if let error = _error.wrappedValue { Text(error).foregroundStyle(.red) }
            HStack {
                Button("Cancelar") { dismiss() }
                Spacer()
                Button("Salvar") {
                    do { try save(category); dismiss() }
                    catch { _error.wrappedValue = error.localizedDescription }
                }.keyboardShortcut(.defaultAction)
            }
        }.padding(24).frame(width: 540)
    }
}

private struct GlossaryEntryEditor: View {
    @Environment(\.dismiss) private var dismiss
    private var _entry: SwiftUI.State<GlossaryEntry>
    private var entry: GlossaryEntry { get { _entry.wrappedValue } nonmutating set { _entry.wrappedValue = newValue } }
    let categories: [GlossaryCategory]
    let save: (GlossaryEntry) throws -> Void
    private var _error = SwiftUI.State<String?>(initialValue: nil)
    private var error: String? { get { _error.wrappedValue } nonmutating set { _error.wrappedValue = newValue } }
    init(entry: GlossaryEntry, categories: [GlossaryCategory], save: @escaping (GlossaryEntry) throws -> Void) {
        _entry = SwiftUI.State(initialValue: entry)
        self.categories = categories
        self.save = save
    }
    private func values(_ key: WritableKeyPath<GlossaryEntry, [String]>) -> Binding<String> {
        Binding(get: { entry[keyPath: key].joined(separator: ", ") }, set: { entry[keyPath: key] = $0.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty } })
    }
    private func member(_ item: String, _ key: WritableKeyPath<GlossaryEntry, [String]>) -> Binding<Bool> {
        Binding(get: { entry[keyPath: key].contains(item) }, set: { enabled in
            entry[keyPath: key].removeAll { $0 == item }; if enabled { entry[keyPath: key].append(item) }
        })
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Regra de preservação").font(.title2)
            Form {
                TextField("Termo", text: _entry.projectedValue.term)
                TextField("Variantes e plurais, separados por vírgula", text: values(\.forms))
                Picker("Tratamento", selection: _entry.projectedValue.mode) { ForEach(GlossaryMode.allCases) { Text($0.label).tag($0) } }
                Toggle("Regra ativa", isOn: _entry.projectedValue.enabled)
                HStack {
                    Toggle("EN → PT-BR", isOn: member("en-pt", \.directions))
                    Toggle("PT-BR → EN", isOn: member("pt-en", \.directions))
                }
                if entry.mode == .context {
                    TextField("Preservar quando a frase contiver", text: values(\.context))
                    TextField("Traduzir quando a frase contiver", text: values(\.excludeContext))
                    Text("Informe palavras ou expressões separadas por vírgula. Uma exclusão tem prioridade; sem contexto correspondente, o termo é traduzido.").font(.caption).foregroundStyle(.secondary)
                }
            }
            Text("Categorias").font(.headline)
            ScrollView {
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading) {
                    ForEach(categories) { category in Toggle(category.name, isOn: member(category.id, \.categories)) }
                }
            }.frame(maxHeight: 160)
            if let error { Text(error).foregroundStyle(.red) }
            HStack {
                Button("Cancelar") { dismiss() }
                Spacer()
                Button("Salvar") { do { try save(entry); dismiss() } catch { self.error = error.localizedDescription } }.keyboardShortcut(.defaultAction)
            }
        }.padding(24).frame(width: 650)
    }
}
