import SwiftUI

struct ConversationView: View {
    @EnvironmentObject var state: AppState
    var onConfigureLanguages: () -> Void = {}
    var body: some View {
        VStack(spacing: 26) {
            HStack {
                Text("Conversation").font(.title2.weight(.semibold))
                Spacer()
                Label("Somente neste Mac", systemImage: "lock").font(.caption).foregroundStyle(.secondary)
            }
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Você · \(state.directionPreferences.outgoing.routeLabel)").fontWeight(.semibold)
                    if state.audioRouting.mode == .call {
                        Text("Participantes · \(state.directionPreferences.incoming.routeLabel)").font(.subheadline)
                    }
                }
                Spacer()
                Button("Alterar idiomas", action: onConfigureLanguages)
                    .accessibilityIdentifier("conversation.configureLanguages")
            }.font(.title3)
            if !state.transcripts.isEmpty {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 20) {
                        ForEach(state.transcripts) { item in
                            VStack(alignment: .leading, spacing: 8) {
                                if !item.audioPath.isEmpty { Text(item.audioPath).font(.caption.weight(.semibold)).foregroundStyle(.teal) }
                                HStack { Text(speakerLabel(item)).font(.caption).foregroundStyle(.secondary); Spacer(); Text(item.languageLabel).font(.caption.weight(.semibold)).foregroundStyle(.teal) }
                                Text(item.text).textSelection(.enabled)
                                if !item.translation.isEmpty { Text(item.translation).foregroundStyle(.teal).textSelection(.enabled) }
                                Label(statusLabel(item.processingState), systemImage: item.processingState == "playing" ? "speaker.wave.2.fill" : "text.bubble").font(.caption).foregroundStyle(.secondary)
                                if !item.error.isEmpty { Text(item.error).font(.caption).foregroundStyle(.orange) }
                                if item.languageStatus == "unsupported" { Text("Idioma não suportado").font(.caption).foregroundStyle(.secondary) }
                                else if item.targetLanguage.isEmpty && item.processingState != "same-language" { Text("Evidência insuficiente para definir a direção").font(.caption).foregroundStyle(.secondary) }
                            }.padding(16).frame(maxWidth: .infinity, alignment: .leading).background(.quaternary.opacity(0.4), in: RoundedRectangle(cornerRadius: 12))
                        }
                    }
                }
            } else {
            Spacer()
            Image(systemName: state.running ? "waveform" : "waveform.circle")
                .font(.system(size: 60, weight: .ultraLight)).foregroundStyle(.teal)
            VStack(spacing: 10) {
                Text(state.running ? (state.audioRouting.mode == .call ? "Chamada e microfone ativos" : "Microfone ativo") : "Sua conversa, neste Mac.").font(.title2.weight(.medium))
                Text(state.asrModel == nil ? "Modelos de reconhecimento não preparados." : "A tradução segue os idiomas e a saída definidos em Ajustes.\nA captura continua enquanto os trechos são processados.")
                    .multilineTextAlignment(.center).foregroundStyle(.secondary).lineSpacing(4)
            }
            Spacer()
            }
            if state.running {
                ProgressView(value: min((state.number("inputLevel") ?? 0) * 5, 1)).frame(width: 180).accessibilityLabel("Nível do microfone")
            }
            if let error = state.error { Text(error).foregroundStyle(.red).multilineTextAlignment(.center).textSelection(.enabled) }
            Button {
                if state.running || state.requestingPermission { state.stop() }
                else { Task { await state.start() } }
            } label: {
                Label(state.running || state.requestingPermission ? "STOP" : "Iniciar escuta", systemImage: state.running ? "stop.fill" : "mic.fill")
                    .frame(width: 180).padding(.vertical, 7)
            }.buttonStyle(.borderedProminent).controlSize(.large)
            Text("STOP encerra a captura e limpa a sessão.").font(.caption).foregroundStyle(.secondary)
        }.padding(36)
    }
    private func speakerLabel(_ item: AppState.Transcript) -> String {
        let settings = state.directionSettings(incoming: item.audioPath == "Chamada → você")
        if !settings.identifySpeakers { return "Voz padrão desta direção" }
        return item.speaker > 0 ? "Participante \(item.speaker)" : "Participante não identificado"
    }
    private func statusLabel(_ state: String) -> String {
        ["direction-filtered": "Idioma fora deste caminho", "recognized": "Reconhecido", "translating": "Traduzindo", "translated": "Traduzido", "synthesizing": "Preparando voz",
         "text-only": "Tradução somente em texto", "audio-skipped": "Áudio omitido para limitar o atraso; texto preservado", "same-language": "Já está no idioma de destino",
         "queued": "Aguardando reprodução", "playing": "Reproduzindo", "played": "Reproduzido", "dropped": "Descartado",
         "error": "Falha no processamento", "unavailable": "Indisponível por memória", "unsupported": "Idioma não suportado", "uncertain": "Idioma incerto", "partial": "Texto parcial", "revision-conflict": "Revisão após confirmação", "revision-preserved": "Áudio preservado; continuação confirmada", "overlap-unresolved": "Falas simultâneas sem separação confiável"][state] ?? state
    }
}
