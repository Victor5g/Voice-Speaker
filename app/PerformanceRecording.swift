import Foundation

/// Opt-in, bounded statistics only. Never accepts conversation events or audio.
struct PerformanceRecording: Sendable {
    struct Context: Sendable {
        var call: Bool
        var profile: String
        var model: String
        var modelHash: String
        var requestedBackend: String
        var session: Int
        var outgoingRoute = "PT-BR -> EN"
        var incomingRoute = "EN -> PT-BR"
        var inPersonRoute = "PT-BR <-> EN"
    }
    enum StopReason: String, Sendable {
        case user = "Interrompida pelo usuário"
        case session = "Sessão de áudio encerrada"
        case duration = "Limite de duração atingido"
        case size = "Limite de tamanho atingido"
    }
    // Explicit allowlist: future text/error/device fields must not leak into reports.
    static let numericKeys = """
    explicitRouting running aec configurationRestarts noiseSuppression referenceDepth referenceDropped
    selfPlaybackBlocks acceptedBlocks vadSpeaking vadFrames vadVoicedFrames vadSegments vadErrors vadLatencyMs
    vadDepth vadDropped segmentsDepth segmentsCapacity segmentsDropped segmentsOldestMs asrState metalActive
    asrBackend asrCompleted speakerCount diarizationLoaded separationLoaded diarizationMs embeddingMs overlapMs
    overlapPercent overlapWindows separationCalls playbackUnitsSuppressed separationMs asrLatencyMs asrRTF
    translationState ttsState translationLatencyMs ttsLatencyMs ttsAudioDurationMs ttsVoiceLoadMs ttsWarmupMs
    ttsCacheHits ttsCacheMisses partialIntervalMs partialPublished partialReplaced asrObsoleteJobs asrEmptyUnits
    asrEstimatedDemandMs asrPendingWaitMs asrInferenceWaitMs asrInferenceWaitP95Ms asrMelMs asrLanguageMs asrDecodeMs
    translationWaitMs ttsWaitMs playbackWaitMs playbackPendingSeconds endpointEstimatedMs afterVadEndMs
    afterVadEndCount afterVadEndP50Ms afterVadEndP95Ms translatedCount synthesizedCount translationDepth
    translationCapacity translationDropped ttsDepth translationOldestMs ttsOldestMs ttsCapacity ttsDropped
    playbackDepth playbackDropped eventDrops firstAudioCount firstAudioMs firstAudioP50Ms firstAudioP95Ms
    revisionConflicts recoveredContinuations
    identifySpeakers speechOutput endpointSilenceMs manualVoiceIndex maxQueuedSpeechSeconds
    languageDetections rejectedNoSpeech rejectedDigitalSilence audioSkipped chunkLagMs chunkLagCount chunkLagP50Ms chunkLagP95Ms
    e2eCount e2eMs e2eP50Ms e2eP95Ms memoryPressure memoryReservedBytes memoryBudgetBytes memoryAdmissionDenied
    transcriptDepth transcriptDropped inputLevel outputLevel captureAgeMs inputFrames outputFrames
    captureCallbacks playbackCallbacks audioDepth audioCapacity audioDropped audioHighWater ramBytes peakRSSBytes
    cpuPercent threads thermalState inputLatencyMs outputLatencyMs
    """.split(whereSeparator: \.isWhitespace).map(String.init).sorted()
    private let maxDuration: TimeInterval
    private let maxBytes: Int
    private var samples: [String] = []
    private var startedUptime: TimeInterval = 0
    private var lastSampleUptime: TimeInterval?
    private var ended: Date?
    private(set) var started: Date?
    private(set) var isRecording = false
    private(set) var byteCount = 0
    private(set) var elapsed: TimeInterval = 0
    private(set) var stopReason: StopReason?
    var sampleCount: Int { samples.count }

    init(maxDuration: TimeInterval = 3600, maxBytes: Int = 32 * 1_048_576) {
        self.maxDuration = maxDuration; self.maxBytes = maxBytes
    }
    mutating func start(date: Date = Date(), uptime: TimeInterval = ProcessInfo.processInfo.systemUptime) {
        guard started == nil else { return } // Explicit clear required; never discard an unexported recording.
        started = date; startedUptime = uptime; isRecording = true
    }
    mutating func stop(_ reason: StopReason = .user, date: Date = Date(), uptime: TimeInterval = ProcessInfo.processInfo.systemUptime) {
        guard isRecording else { return }
        elapsed = max(0, uptime - startedUptime)
        isRecording = false; ended = date; stopReason = reason
    }
    mutating func append(outgoing: [String: Any], incoming: [String: Any], context: Context,
                         date: Date = Date(), uptime: TimeInterval = ProcessInfo.processInfo.systemUptime) {
        guard isRecording else { return }
        elapsed = max(0, uptime - startedUptime)
        if elapsed >= maxDuration { stop(.duration, date: date, uptime: uptime); return }
        if let lastSampleUptime, uptime - lastSampleUptime < 1 { return }
        var lines = ["", "AMOSTRA \(sampleCount + 1) | UTC=\(date.ISO8601Format()) | elapsedMs=\(Int(elapsed * 1000))",
                     "session=\(context.session) mode=\(context.call ? "call" : "inPerson")",
                     "profile=\(Self.safe(context.profile))", "asrModel=\(Self.safe(context.model))",
                     "asrModelSHA256=\(Self.safe(context.modelHash))",
                     "requestedBackend=\(Self.safe(context.requestedBackend.isEmpty ? "bundle-default" : context.requestedBackend))"]
        func add(_ metrics: [String: Any], path: String) {
            lines.append("[\(path)]")
            for key in Self.numericKeys {
                let number = metrics[key] as? NSNumber
                let value = number.flatMap { $0.doubleValue.isFinite ? $0.stringValue : nil } ?? "NOT MEASURED"
                lines.append("\(key)=\(value)")
            }
            for key in ["loadedVoice", "ttsCachedVoices", "sourceLanguage", "targetLanguage"] {
                lines.append("\(key)=\(Self.safe(metrics[key] as? String ?? "NOT MEASURED"))")
            }
            // Errors may contain arbitrary backend text/paths; preserve presence only.
            for key in ["audioError", "asrError", "conversationError"] {
                lines.append("\(key)Present=\((metrics[key] as? String).map { !$0.isEmpty } == true ? 1 : 0)")
            }
        }
        add(outgoing, path: context.call ? "outgoing \(Self.safe(context.outgoingRoute))" : "in-person \(Self.safe(context.inPersonRoute))")
        if context.call { add(incoming, path: "incoming \(Self.safe(context.incomingRoute))") }
        let sample = lines.joined(separator: "\n") + "\n"
        let size = sample.utf8.count
        guard size <= maxBytes - byteCount else { stop(.size, date: date, uptime: uptime); return }
        samples.append(sample); byteCount += size; lastSampleUptime = uptime
    }
    private static func safe(_ value: String) -> String {
        String(value.prefix(256)).components(separatedBy: .controlCharacters).joined(separator: " ")
    }
    func report() -> String {
        """
        LocalVoiceTranslator — relatório de desempenho v1
        Início UTC: \(started?.ISO8601Format() ?? "NOT MEASURED")
        Fim UTC: \(ended?.ISO8601Format() ?? "em gravação")
        Estado: \(stopReason?.rawValue ?? "Gravando")
        Duração: \(String(format: "%.1f", locale: Locale(identifier: "en_US_POSIX"), elapsed)) s
        Amostras de painel: \(sampleCount)
        App: \(Self.safe(Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "development"))
        macOS: \(Self.safe(ProcessInfo.processInfo.operatingSystemVersionString))
        CPU lógica: \(ProcessInfo.processInfo.processorCount); memória física: \(ProcessInfo.processInfo.physicalMemory) bytes

        Coleta nominal: 1 s; elapsedMs usa relógio monotônico. Intervalos podem aumentar sob carga/suspensão.
        Limites: \(Int(maxDuration)) s ou \(maxBytes) bytes de amostras; encerra sem sobrescrever amostras.
        Sem áudio, transcrições, traduções, embeddings, IDs de dispositivos ou texto de erros.
        CPU/memória/threads/thermalState são do processo/sistema, não somar os dois caminhos.
        cpuPercent: % de um núcleo, pode exceder 100. Bytes: bytes; Ms: milissegundos; Seconds: segundos.
        inputLevel/outputLevel: RMS; asrRTF: tempo de inferência/duração do áudio.
        Depth/Capacity/HighWater: itens; Dropped/Count/Frames/Completed: contadores da sessão, não por segundo.
        running/aec/noiseSuppression/*Loaded/metalActive: 0=não, 1=sim.
        asrBackend: 0=Metal, 1=Core ML CPU/ANE, 2=Core ML automático, 3=Core ML CPU/GPU.
        Backend descreve configuração; não é medição de utilização GPU/ANE.
        revisionConflicts: revisões divergentes após confirmação; recoveredContinuations é um subconjunto.
        Recuperar a continuação NÃO corrige o texto/áudio anterior nem comprova qualidade da tradução.
        asrState/translationState/ttsState: 0=parado, 1=carregando, 2=pronto, 3=erro.
        thermalState: 0=nominal, 1=fair, 2=serious, 3=critical; não mede temperatura em graus.
        memoryPressure: 0=normal, 1=warning, 2=critical. ErrorPresent: apenas presença de erro.
        NOT MEASURED: ausente/inválido; zero preserva o valor bruto do painel, não prova execução.
        Latências são a última medição e podem se repetir entre amostras; NÃO são eventos independentes.
        P50/P95 vêm do engine, somente com >=30 eventos; não são recalculados sobre amostras do painel.
        Primeiro áudio inclui captura de fala, estabilidade e filas; E2E inclui reprodução.
        afterVadEndMs é estimativa baseada no VAD, não medição acústica.
        chunkLagMs: atraso de cada unidade até sua reprodução; inclui espera acumulada na saída.
        audioSkipped: unidades de áudio omitidas por limite de fila; o texto permanece disponível.
        identifySpeakers/speechOutput: 0=desativado, 1=ativado; idioma vazio=automático.
        languageDetections: chamadas da identificação automática de idioma, não da transcrição.
        Origem automática: asrLanguageMs inclui encoder + identificação de idioma; asrDecodeMs mede a decodificação após essa etapa.
        Origem manual: asrLanguageMs fica NOT MEASURED; asrDecodeMs inclui encoder + decodificação. O encoder continua necessário.
        Não compare asrDecodeMs isoladamente entre origem manual e automática; compare também asrLatencyMs e os eventos completos.
        rejectedDigitalSilence: unidades rejeitadas por silêncio digital; separado de rejectedNoSpeech, que usa evidência do reconhecimento.

        \(samples.joined())
        """
    }
    func write(to url: URL) throws {
        try report().write(to: url, atomically: true, encoding: .utf8)
    }
}
