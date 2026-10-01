import SwiftUI
struct SettingsView: View {
    @EnvironmentObject var state: AppState
    private var _showGlossary = SwiftUI.State(initialValue: false)
    private var _confirmReset = SwiftUI.State(initialValue: false)
    private func binding<T>(_ key: WritableKeyPath<DirectionSettings, T>, incoming: Bool) -> Binding<T> {
        Binding(get: { state.directionSettings(incoming: incoming)[keyPath: key] }, set: { value in
            var settings = state.directionSettings(incoming: incoming)
            settings[keyPath: key] = value
            state.selectDirectionSettings(settings, incoming: incoming)
        })
    }
    private func direction(_ title: String, incoming: Bool) -> some View {
        let settings = state.directionSettings(incoming: incoming)
        return GroupBox(title) {
            VStack(alignment: .leading, spacing: 12) {
                Text(incoming ? "Áudio recebido dos participantes da chamada. No modo Presencial, este grupo não é usado." :
                    "Seu microfone na chamada. No modo Presencial, estas opções valem para todas as vozes captadas.")
                    .font(.caption).foregroundStyle(.secondary)
                Toggle("Identificação de falantes", isOn: binding(\.identifySpeakers, incoming: incoming))
                    .accessibilityIdentifier(incoming ? "participants.identifySpeakers" : "you.identifySpeakers")
                Text(settings.identifySpeakers ? "Atribui e mantém uma voz por participante durante a sessão." :
                    "Dispensa a identificação de participantes. A transcrição continua ativa.")
                    .font(.caption).foregroundStyle(.secondary)
                Picker("Voz de saída", selection: binding(\.voiceIndex, incoming: incoming)) {
                    ForEach(0..<3) { index in Text(settings.voiceLabel(index)).tag(index) }
                }.disabled(settings.identifySpeakers || !settings.speechOutput)
                    .accessibilityIdentifier(incoming ? "participants.voice" : "you.voice")
                if settings.identifySpeakers {
                    Text("A voz é automática enquanto a identificação de falantes estiver ligada.")
                        .font(.caption).foregroundStyle(.secondary)
                }
                DisclosureGroup("Avançado · velocidade e saída") {
                    VStack(alignment: .leading, spacing: 10) {
                        Toggle("Reproduzir tradução em voz", isOn: binding(\.speechOutput, incoming: incoming))
                        Text("Desligado: mantém a tradução em texto, sem síntese nem reprodução.")
                            .font(.caption).foregroundStyle(.secondary)
                        Stepper("Espera após pausa: \(settings.endpointSilenceMs) ms",
                                value: binding(\.endpointSilenceMs, incoming: incoming), in: 200...1000, step: 20)
                        Text("Uma espera menor encerra os trechos mais cedo, mas pode dividir uma frase durante uma pausa natural. Padrão: 300 ms.")
                            .font(.caption).foregroundStyle(.secondary)
                        Stepper("Limite de áudio na fila: \(settings.maxQueuedSpeechSeconds) s",
                                value: binding(\.maxQueuedSpeechSeconds, incoming: incoming), in: 3...15)
                        Text("Novos áudios que aumentariam a fila acima do limite ficam só em texto. Uma fala longa pode tocar inteira quando a saída está livre; nunca cortamos o áudio em reprodução.")
                            .font(.caption).foregroundStyle(.secondary)
                    }.padding(.top, 8)
                }
            }.padding(8)
        }.disabled(state.processingSettingsLocked)
    }
    var body: some View {
        ScrollView {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                Text("Idiomas e vozes").font(.title2.weight(.semibold))
                Spacer()
                Button("Restaurar padrões…") { _confirmReset.wrappedValue = true }
                    .disabled(state.processingSettingsLocked)
                    .accessibilityIdentifier("settings.restoreDefaults")
            }
            .alert("Restaurar todas as configurações?", isPresented: _confirmReset.projectedValue) {
                Button("Cancelar", role: .cancel) {}
                Button("Restaurar", role: .destructive) { state.restoreDefaultSettings() }
            } message: {
                Text("Idiomas, vozes, identificação de falantes, pausas, filas, perfil, aceleração e caminhos de áudio voltarão ao padrão. O glossário original substituirá seus termos e categorias personalizados. O áudio nativo da chamada será desativado; você precisará prepará-lo novamente. Antes de restaurar, volte ao microfone e aos fones normais no app da chamada. Modelos e arquivos exportados serão mantidos.")
            }
            if let message = state.settingsResetMessage {
                Label(message, systemImage: "checkmark.circle").font(.caption).foregroundStyle(.secondary)
            }
            Text("Escolha o idioma de cada fala e da tradução que o outro lado vai ouvir.").foregroundStyle(.secondary)
            if state.processingSettingsLocked {
                HStack {
                    Label(state.running || state.requestingPermission ? "Pare a sessão para editar os idiomas." : "Aguarde a preparação do áudio para editar.", systemImage: "lock")
                        .font(.caption).foregroundStyle(.secondary)
                    if state.running || state.requestingPermission {
                        Button("Parar sessão para editar") { state.stop() }
                    }
                }
            }
            if let error = state.error { Text(error).foregroundStyle(.red).font(.caption) }
            LanguageDirectionView(incoming: false)
            LanguageDirectionView(incoming: true)
            Text("As escolhas são salvas automaticamente. PT-BR e inglês são os idiomas disponíveis; ao inverter um idioma manual, o outro campo acompanha.")
                .font(.caption).foregroundStyle(.secondary)
            if state.audioRouting.mode != .call {
                Label("Modo Presencial: apenas VOCÊ é usado para todas as vozes do microfone. Para usar os dois lados, selecione Chamada na aba Áudio.", systemImage: "info.circle")
                    .font(.caption).foregroundStyle(.secondary)
            }
            DisclosureGroup("Vozes e processamento") {
                VStack(alignment: .leading, spacing: 14) {
                    direction("Voz enviada aos participantes", incoming: false)
                    direction("Voz que você ouve", incoming: true)
                }.padding(.top, 8)
            }
            Button("Editar glossário…") { _showGlossary.wrappedValue = true }
                .disabled(state.processingSettingsLocked)
                .sheet(isPresented: _showGlossary.projectedValue) { GlossaryView() }
            Picker("Perfil", selection: Binding(get: { state.selectedProfile }, set: { state.selectProfile($0) })) {
                ForEach(state.availableProfiles) { profile in Text(profile.id).tag(profile.id) }
            }.disabled(state.running || state.requestingPermission || state.availableProfiles.count < 2)
            LabeledContent("Reconhecimento local", value: state.asrModel?.name ?? "Modelo não preparado")
            Picker("Aceleração do reconhecimento", selection: Binding(get: { state.selectedASRBackend }, set: { state.selectASRBackend($0) })) {
                Text("Padrão · Metal").tag("")
                Text("Metal").tag("metal")
                Text("Core ML + Metal · CPU/ANE").tag("coreml-ane")
                Text("Core ML + Metal · automático").tag("coreml-all")
                Text("Core ML + Metal · CPU/GPU").tag("coreml-gpu")
            }.disabled(state.running || state.requestingPermission)
            Text("Core ML é experimental: pode mudar a precisão das palavras. CPU/ANE permite o Neural Engine, sem garantir execução exclusiva nele. A primeira carga pode demorar mais.")
                .font(.caption).foregroundStyle(.secondary)
            Text("Balanced é o perfil aprovado. Eco ainda não passou consistentemente pelos testes de precisão. Quality não passou pelo orçamento de memória.").foregroundStyle(.secondary)
            LabeledContent("Armazenamento de conversa", value: "Desativado")
        }.padding(24)
        }
    }
}

/// Both entry points use the same bindings and persisted direction settings.
private struct LanguageDirectionView: View {
    @EnvironmentObject var state: AppState
    let incoming: Bool
    private var settings: DirectionSettings { state.directionSettings(incoming: incoming) }
    var body: some View {
        GroupBox(incoming ? "PARTICIPANTES → VOCÊ" : "VOCÊ → PARTICIPANTES") {
            VStack(alignment: .leading, spacing: 10) {
                Picker(incoming ? "Eles falam em" : "Eu falo em", selection: Binding(
                    get: { settings.sourceLanguage },
                    set: { state.selectSourceLanguage($0, incoming: incoming) })) {
                    Text("Detectar automaticamente").tag(TranslationLanguage.automatic)
                    Text("Português brasileiro").tag(TranslationLanguage.portuguese)
                    Text("Inglês").tag(TranslationLanguage.english)
                }.pickerStyle(.menu)
                    .accessibilityIdentifier(incoming ? "participants.sourceLanguage" : "you.sourceLanguage")
                Picker(incoming ? "Eu ouço em" : "Eles ouvem em", selection: Binding(
                    get: { settings.targetLanguage },
                    set: { state.selectTargetLanguage($0, incoming: incoming) })) {
                    Text("Automático · outro idioma").tag(TranslationLanguage.automatic)
                    Text("Português brasileiro").tag(TranslationLanguage.portuguese)
                    Text("Inglês").tag(TranslationLanguage.english)
                }.pickerStyle(.menu)
                    .accessibilityIdentifier(incoming ? "participants.targetLanguage" : "you.targetLanguage")
                Text(incoming ? "Áudio de todos os participantes da chamada → tradução nos seus fones." :
                    "Seu microfone → tradução enviada à chamada para todos ouvirem.")
                    .font(.caption).foregroundStyle(.secondary)
                if !settings.speechOutput {
                    Text("Saída em voz desligada: esta tradução aparece somente em texto.")
                        .font(.caption).foregroundStyle(.secondary)
                }
                if settings.sourceLanguage == .automatic && settings.targetLanguage != .automatic {
                    Text("Falas já no idioma escolhido para ouvir não são traduzidas novamente.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }.frame(maxWidth: .infinity, alignment: .leading).padding(8)
        }.disabled(state.processingSettingsLocked)
    }
}
