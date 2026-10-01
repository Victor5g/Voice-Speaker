import SwiftUI

struct EngineView: View {
    @EnvironmentObject var state: AppState
    var onConfigureLanguages: () -> Void = {}
    private var incomingState = SwiftUI.State<Bool>(initialValue: false)
    private var incoming: Bool { incomingState.wrappedValue }
    private var metrics: [String: Any] { incoming && state.audioRouting.mode == .call ? state.incomingSnapshot : state.snapshot }
    private func number(_ key: String) -> Double? { (metrics[key] as? NSNumber)?.doubleValue }
    private func integer(_ key: String) -> String { number(key).map { String(format: "%.0f", $0) } ?? "NOT MEASURED" }
    private func metric(_ key: String, suffix: String = "", scale: Double = 1) -> String {
        guard let value = number(key) else { return "NOT MEASURED" }
        return String(format: "%.1f", value / scale) + suffix
    }
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                if state.audioRouting.mode == .call {
                    Picker("Estatísticas de", selection: incomingState.projectedValue) {
                        Text("Você · \(state.directionPreferences.outgoing.routeLabel)").tag(false)
                        Text("Participantes · \(state.directionPreferences.incoming.routeLabel)").tag(true)
                    }.pickerStyle(.segmented)
                    Text("Este seletor muda apenas as estatísticas exibidas. Configure os idiomas no botão abaixo. CPU e memória representam o app inteiro.").font(.caption).foregroundStyle(.secondary)
                }
                Button("Configurar idiomas e vozes", action: onConfigureLanguages)
                    .accessibilityIdentifier("engine.configureLanguages")
                HStack { Text("Engine").font(.largeTitle.weight(.semibold)); Spacer(); Text("AO VIVO · 1 s").font(.caption).foregroundStyle(.secondary) }
                PerformanceRecordingView()
                HStack {
                    GroupBox("CPU · % de um núcleo") { Text(metric("cpuPercent", suffix: "%")).font(.title2).frame(maxWidth: .infinity, alignment: .leading); Sparkline(values: state.cpuHistory) }
                    GroupBox("Memória física") { Text(metric("ramBytes", suffix: " MB", scale: 1_048_576)).font(.title2).frame(maxWidth: .infinity, alignment: .leading); Sparkline(values: state.ramHistory) }
                }
                GroupBox("System") {
                    Grid(alignment: .leading, horizontalSpacing: 30, verticalSpacing: 10) {
                        row("Peak RSS", metric("peakRSSBytes", suffix: " MB", scale: 1_048_576))
                        row("Threads", metric("threads"))
                        row("Thermal state", ["Nominal", "Fair", "Serious", "Critical"][min(max(Int(number("thermalState") ?? 0), 0), 3)])
                        row("Metal", number("metalActive") == 1 ? "Active · Whisper" : "Inactive")
                        row("Encoder ASR", number("asrState") == 2 ? ["Metal", "Core ML · CPU/ANE permitidos", "Core ML · automático", "Core ML · CPU/GPU permitidos"][min(max(Int(number("asrBackend") ?? 0), 0), 3)] : "Não carregado")
                        row("Memory pressure", ["Normal", "Warning", "Critical"][min(max(Int(number("memoryPressure") ?? 0), 0), 2)])
                        row("Orçamento de memória", metric("memoryBudgetBytes", suffix: " GiB", scale: 1_073_741_824))
                        row("Reserva de inferência · estimativa", metric("memoryReservedBytes", suffix: " MB", scale: 1_048_576))
                        row("Inferências negadas por memória", metric("memoryAdmissionDenied"))
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
                }
                GroupBox("Audio") {
                    Grid(alignment: .leading, horizontalSpacing: 30, verticalSpacing: 10) {
                        row("Capture", state.running ? "Active" : "Stopped")
                        row("Input level · RMS", metric("inputLevel"))
                        row("Output level · RMS", metric("outputLevel"))
                        row("Input latency", metric("inputLatencyMs", suffix: " ms"))
                        row("Output latency", metric("outputLatencyMs", suffix: " ms"))
                        row("AEC", number("aec") == 1 ? "VoiceProcessingIO enabled" : "Inactive")
                        row("Noise suppression", number("noiseSuppression") == 1 ? "VoiceProcessingIO enabled" : "Inactive")
                        row("Playback reference · queued / dropped", metric("referenceDepth") + " / " + metric("referenceDropped"))
                        row("Self-playback · blocks suppressed", metric("selfPlaybackBlocks"))
                        row("Self-playback · falas filtradas", metric("playbackUnitsSuppressed"))
                        row("Frames input / output", metric("inputFrames") + " / " + metric("outputFrames"))
                        row("Audio queue · current / capacity", metric("audioDepth") + " / " + metric("audioCapacity"))
                        row("Audio queue · dropped / high water", metric("audioDropped") + " / " + metric("audioHighWater"))
                        row("Capture → worker · última amostra", metric("captureAgeMs", suffix: " ms"))
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
                    Button("Testar alto-falante · tom de 350 ms") { state.testPlayback() }.disabled(!state.running).padding(.top, 8)
                }
                GroupBox("Pipeline") {
                    Grid(alignment: .leading, horizontalSpacing: 30, verticalSpacing: 10) {
                        row("VAD · WebRTC", number("vadSpeaking") == 1 ? "Fala detectada" : "Aguardando fala")
                        row("VAD · última classificação", metric("vadLatencyMs", suffix: " ms"))
                        row("VAD · quadros / segmentos / erros", metric("vadFrames") + " / " + metric("vadSegments") + " / " + metric("vadErrors"))
                        row("VAD queue · current / dropped", metric("vadDepth") + " / " + metric("vadDropped"))
                        row("Segments queue · current / capacity", metric("segmentsDepth") + " / " + metric("segmentsCapacity"))
                        row("Segments queue · dropped / oldest", metric("segmentsDropped") + " / " + metric("segmentsOldestMs", suffix: " ms"))
                        row("ASR model", state.asrModel?.name ?? "Not bundled")
                        row("ASR state", ["Stopped", "Loading / SHA-256", "Ready", "Error"][min(max(Int(number("asrState") ?? 0), 0), 3)])
                        row("ASR · última inferência", metric("asrLatencyMs", suffix: " ms"))
                        row("ASR RTF", metric("asrRTF"))
                        row("Direção", state.directionSettings(incoming: incoming && state.audioRouting.mode == .call).routeLabel)
                        row("Identificação de idioma · chamadas", integer("languageDetections"))
                        row("Trechos rejeitados sem fala suficiente", integer("rejectedNoSpeech"))
                        row("Trechos rejeitados · silêncio digital", integer("rejectedDigitalSilence"))
                        row("Identificação de participantes", number("identifySpeakers").map { $0 == 1 ? "Ativada" : "Desativada · transcrição permanece ativa" } ?? "NOT MEASURED")
                        row("Síntese e reprodução", number("speechOutput").map { $0 == 1 ? "Ativadas" : "Somente texto" } ?? "NOT MEASURED")
                        row("Voz manual · índice", integer("manualVoiceIndex"))
                        row("Espera configurada após pausa", metric("endpointSilenceMs", suffix: " ms"))
                        row("ASR results · queued / dropped", metric("transcriptDepth") + " / " + metric("transcriptDropped"))
                        if let error = metrics["asrError"] as? String, !error.isEmpty { row("ASR error", error) }
                        row("Participantes · ECAPA512", metric("speakerCount"))
                        row("ECAPA512 / Pyannote 3.0", number("diarizationLoaded") == 1 ? "Carregados · CPU" : "Não carregados")
                        row("Diarização / embeddings", metric("diarizationMs", suffix: " ms") + " / " + metric("embeddingMs", suffix: " ms"))
                        row("Overlap · última janela", metric("overlapPercent", suffix: "%"))
                        row("Overlap · inferência / janelas positivas", metric("overlapMs", suffix: " ms") + " / " + metric("overlapWindows"))
                        row("Conv-TasNet", number("separationLoaded") == 1 ? "Carregado · CPU" : "Não carregado · sob demanda")
                        row("Separação · chamadas / última inferência", metric("separationCalls") + " / " + metric("separationMs", suffix: " ms"))
                        row("Tradução · CPU INT8", ["Stopped", "Loading / SHA-256", "Ready", "Error"][min(max(Int(number("translationState") ?? 0), 0), 3)])
                        row("Tradução · fila / capacidade / descartes", metric("translationDepth") + " / " + metric("translationCapacity") + " / " + metric("translationDropped"))
                        row("Tradução · última inferência", metric("translationLatencyMs", suffix: " ms"))
                        row("Tradução / TTS · idade da fila", metric("translationOldestMs", suffix: " ms") + " / " + metric("ttsOldestMs", suffix: " ms"))
                        row("Voz carregada", (metrics["loadedVoice"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "Nenhuma")
                        row("TTS · última inferência", metric("ttsLatencyMs", suffix: " ms"))
                        row("TTS · duração do áudio gerado", metric("ttsAudioDurationMs", suffix: " s", scale: 1000))
                        row("TTS · carregar voz neste trecho", metric("ttsVoiceLoadMs", suffix: " ms"))
                        row("TTS · preparação inicial", metric("ttsWarmupMs", suffix: " ms"))
                        row("TTS · vozes em memória (até 2)", (metrics["ttsCachedVoices"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "Nenhuma")
                        row("TTS · reutilizações / cargas durante fala", "\(integer("ttsCacheHits")) / \(integer("ttsCacheMisses"))")
                        row("TTS · fila / capacidade / descartes", metric("ttsDepth") + " / " + metric("ttsCapacity") + " / " + metric("ttsDropped"))
                        row("Reprodução · fila / descartes", metric("playbackDepth") + " / " + metric("playbackDropped"))
                        row("Reprodução · áudio pendente", metric("playbackPendingSeconds", suffix: " s"))
                        row("Reprodução · limite configurado", metric("maxQueuedSpeechSeconds", suffix: " s"))
                        row("Áudios omitidos · texto mantido", integer("audioSkipped"))
                        row("Atraso por trecho até reprodução", metric("chunkLagMs", suffix: " ms"))
                        row("Atraso por trecho · P50 / P95", metric("chunkLagP50Ms", suffix: " ms") + " / " + metric("chunkLagP95Ms", suffix: " ms"))
                        row("Atraso por trecho · amostras", integer("chunkLagCount"))
                        row("ASR · espera de trabalho / modelo", metric("asrPendingWaitMs", suffix: " ms") + " / " + metric("asrInferenceWaitMs", suffix: " ms"))
                        row("ASR · espera de modelo P95", metric("asrInferenceWaitP95Ms", suffix: " ms"))
                        row("ASR · intervalo das parciais", metric("partialIntervalMs", suffix: " ms"))
                        row("Parciais · publicadas / substituídas", integer("partialPublished") + " / " + integer("partialReplaced"))
                        row("Revisões após confirmação · total / continuação recuperada", integer("revisionConflicts") + " / " + integer("recoveredContinuations"))
                        row("ASR · trabalhos obsoletos / sem texto", integer("asrObsoleteJobs") + " / " + integer("asrEmptyUnits"))
                        row("ASR · preparação mel", metric("asrMelMs", suffix: " ms"))
                        row("ASR · encoder + identificação de idioma", metric("asrLanguageMs", suffix: " ms"))
                        row((metrics["sourceLanguage"] as? String)?.isEmpty == false ? "ASR · encoder + decodificação (origem manual)" : "ASR · decodificação (origem automática)", metric("asrDecodeMs", suffix: " ms"))
                        row("Espera · tradução / TTS / reprodução", metric("translationWaitMs", suffix: " ms") + " / " + metric("ttsWaitMs", suffix: " ms") + " / " + metric("playbackWaitMs", suffix: " ms"))
                        row("Corte após fim VAD · estimado", metric("endpointEstimatedMs", suffix: " ms"))
                        row("Fim VAD → som · estimado", metric("afterVadEndMs", suffix: " ms"))
                        row("Fim VAD → som · P50 / P95", metric("afterVadEndP50Ms", suffix: " ms") + " / " + metric("afterVadEndP95Ms", suffix: " ms"))
                        row("Amostras · fim VAD → som", metric("afterVadEndCount"))
                        row("Primeiro áudio traduzido", metric("firstAudioMs", suffix: " ms"))
                        row("Fim da reprodução · E2E por unidade", metric("e2eMs", suffix: " ms"))
                        row("Primeiro áudio · P50 / P95", metric("firstAudioP50Ms", suffix: " ms") + " / " + metric("firstAudioP95Ms", suffix: " ms"))
                        row("E2E · P50 / P95", metric("e2eP50Ms", suffix: " ms") + " / " + metric("e2eP95Ms", suffix: " ms"))
                        row("Amostras · primeiro áudio / E2E", metric("firstAudioCount") + " / " + metric("e2eCount"))
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
                    Text("Inferência mede o processamento de um trecho. Primeiro áudio inclui a fala capturada, confirmação do texto e filas; E2E inclui também a reprodução. 1000 ms correspondem a 1 segundo.").font(.caption).foregroundStyle(.secondary)
                    Text("Origem automática: o encoder está incluído no tempo de identificação de idioma. Origem manual: não há essa detecção e o encoder passa a integrar o tempo de decodificação; ele continua necessário. Compare o tempo total ASR entre os modos.").font(.caption).foregroundStyle(.secondary)
                    Text("Fim VAD → som usa o último frame classificado como fala e o início detectado do PCM, somente em trechos encerrados por silêncio. Inclui espera pela saída. Não é uma medição acústica da última palavra; não comprova a meta de 2 segundos. Percentis exigem 30 amostras.").font(.caption).foregroundStyle(.secondary)
                }
                HStack {
                    GroupBox("Primeiro áudio · ms") { Sparkline(values: incoming && state.audioRouting.mode == .call ? (state.incomingHistories["firstAudioMs"] ?? []) : state.firstAudioHistory) }
                    GroupBox("E2E por unidade · ms") { Sparkline(values: incoming && state.audioRouting.mode == .call ? (state.incomingHistories["e2eMs"] ?? []) : state.e2eHistory) }
                }
                HStack {
                    GroupBox("ASR · ms") { Sparkline(values: incoming && state.audioRouting.mode == .call ? (state.incomingHistories["asrLatencyMs"] ?? []) : state.asrHistory) }
                    GroupBox("Fila de reconhecimento") { Sparkline(values: incoming && state.audioRouting.mode == .call ? (state.incomingHistories["segmentsDepth"] ?? []) : state.asrQueueHistory) }
                }
                GroupBox("Participantes · último evento da sessão") {
                    if state.participants.isEmpty { Text("Nenhum participante reconhecido").foregroundStyle(.secondary) }
                    ForEach(state.participants.values.sorted(by: { $0.id < $1.id })) { participant in
                        HStack {
                            Text("Participante \(participant.id)")
                            Text(participant.language == "pt" ? "PT-BR" : participant.language.uppercased()).foregroundStyle(.teal)
                            Spacer()
                            Text(participant.state == "playing" ? "Reproduzindo" : participant.state == "played" ? "Reprodução concluída" : participant.state == "text-only" ? "Somente texto" : participant.state == "audio-skipped" ? "Áudio omitido" : "Processando fala")
                            if !participant.voice.isEmpty { Text(participant.voice).font(.caption).foregroundStyle(.secondary) }
                        }
                    }
                }
            }.padding(28)
        }.textSelection(.enabled)
    }
    private func row(_ label: String, _ value: String) -> some View {
        GridRow { Text(label).foregroundStyle(.secondary); Text(value).monospacedDigit() }
    }
}
