import AVFoundation
import SwiftUI
import UniformTypeIdentifiers

@MainActor
final class AppState: ObservableObject {
    struct Transcript: Identifiable {
        var id = UUID().uuidString
        var text: String
        var language: String
        var targetLanguage: String = ""
        var languageStatus: String = ""
        var translation: String = ""
        var processingState: String = "recognized"
        var speaker: Int = 0
        var error: String = ""
        var voice: String = ""
        var audioPath: String = ""
        var languageLabel: String { language == "pt" ? "PT-BR" : language == "en" ? "EN" : "Não suportado" }
    }
    struct Participant: Identifiable {
        let id: Int
        var language = ""
        var state = ""
        var voice = ""
    }
    @Published var running = false
    @Published var requestingPermission = false
    @Published var error: String?
    @Published var snapshot: [String: Any] = [:]
    @Published var cpuHistory: [Double] = []
    @Published var ramHistory: [Double] = []
    @Published var firstAudioHistory: [Double] = []
    @Published var e2eHistory: [Double] = []
    @Published var asrHistory: [Double] = []
    @Published var asrQueueHistory: [Double] = []
    @Published var incomingHistories: [String: [Double]] = [:]
    @Published var transcripts: [Transcript] = []
    @Published var participants: [Int: Participant] = [:]
    @Published private(set) var performanceRecording = PerformanceRecording()
    @Published private(set) var exportingPerformance = false
    @Published private(set) var performanceExportMessage: String?
    @Published private(set) var directionPreferences = DirectionPreferences()
    @Published private(set) var settingsResetMessage: String?
    private let preferencesDefaults: UserDefaults
    var processingSettingsLocked: Bool { running || requestingPermission || nativeSetupInFlight }
    func directionSettings(incoming: Bool) -> DirectionSettings {
        directionPreferences.loadRoute(incoming: incoming)
    }
    func selectSourceLanguage(_ language: TranslationLanguage, incoming: Bool) {
        var settings = directionSettings(incoming: incoming)
        settings.selectSourceLanguage(language)
        selectDirectionSettings(settings, incoming: incoming)
    }
    func selectTargetLanguage(_ language: TranslationLanguage, incoming: Bool) {
        var settings = directionSettings(incoming: incoming)
        settings.selectTargetLanguage(language)
        selectDirectionSettings(settings, incoming: incoming)
    }
    func selectDirectionSettings(_ settings: DirectionSettings, incoming: Bool) {
        guard !processingSettingsLocked else { return }
        if let issue = settings.validationError { error = issue; return }
        var next = directionPreferences
        guard next.select(settings, incoming: incoming, sessionActive: processingSettingsLocked,
                          defaults: preferencesDefaults) else { return }
        directionPreferences = next
        error = nil
    }

    @discardableResult
    func restoreDefaultSettings(glossary: GlossaryStore? = nil) -> Bool {
        guard !processingSettingsLocked else { return false }
        settingsResetMessage = nil
        do {
            // A missing factory glossary must not leave the rest partially reset.
            try (glossary ?? GlossaryStore.shared).restoreDefaults()
        } catch {
            self.error = "Não foi possível restaurar os padrões: \(error.localizedDescription)"
            return false
        }
        closeNativeCallAudio()
        preferencesDefaults.removeObject(forKey: DirectionPreferences.storageKey)
        directionPreferences = DirectionPreferences()
        audioRouting = AudioRouting()
        selectedProfile = "Balanced"
        selectedASRBackend = ""
        asrModel = availableProfiles.first(where: { $0.id == "Balanced" })?.model
        settingsResetMessage = "Configurações padrão restauradas. Para usar chamadas, prepare novamente os caminhos na aba Áudio."
        return true
    }

    func startPerformanceRecording() {
        guard performanceRecording.started == nil && !exportingPerformance else { return }
        performanceExportMessage = nil
        performanceRecording.start()
        refresh(forceMetrics: true)
    }
    func stopPerformanceRecording() { performanceRecording.stop() }
    func clearPerformanceRecording() {
        guard !performanceRecording.isRecording && !exportingPerformance else { return }
        performanceRecording = PerformanceRecording(); performanceExportMessage = nil
    }
    func exportPerformanceRecording() {
        guard !performanceRecording.isRecording && performanceRecording.sampleCount > 0 && !exportingPerformance else { return }
        exportingPerformance = true; performanceExportMessage = nil
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "LocalVoiceTranslator-desempenho.txt"
        panel.canCreateDirectories = true
        panel.title = "Exportar relatório de desempenho"
        let recording = performanceRecording
        // Asynchronous panel and background formatting/write keep capture/UI responsive.
        panel.begin { [weak self] response in
            guard let self else { return }
            guard response == .OK, let url = panel.url else {
                self.exportingPerformance = false; return
            }
            Task { @MainActor in
                let failure = await Task.detached(priority: .utility) { () -> String? in
                    let access = url.startAccessingSecurityScopedResource()
                    defer { if access { url.stopAccessingSecurityScopedResource() } }
                    do { try recording.write(to: url); return nil }
                    catch { return error.localizedDescription }
                }.value
                self.performanceExportMessage = failure.map { "Não foi possível exportar: " + $0 } ?? "Relatório TXT exportado."
                self.exportingPerformance = false
            }
        }
    }
    let availableProfiles = ModelCatalog.bundledProfiles()
    @Published private(set) var selectedProfile = "Balanced"
    @Published private(set) var selectedASRBackend = ""
    func selectASRBackend(_ backend: String) {
        guard !running && !requestingPermission,
              ["", "metal", "coreml-ane", "coreml-all", "coreml-gpu"].contains(backend) else { return }
        selectedASRBackend = backend
    }
    @Published private(set) var asrModel = ModelCatalog.bundledASR()
    @Published private(set) var audioRouting = AudioRouting()
    @Published private(set) var audioDevices: [AudioDevice] = []
    @Published private(set) var incomingSnapshot: [String: Any] = [:]
    @Published private(set) var nativeCallAudioReady = false
    private let nativeCallAudio = NativeCallAudioSession()
    private let nativeAudioQueue = DispatchQueue(label: "localvoice.native-audio-setup")
    @Published private(set) var nativeSetupInFlight = false
    private let engine = TranslationEngineBridge()
    private let incomingEngine = TranslationEngineBridge()
    func refreshAudioDevices() {
        audioDevices = AudioDevice.available()
        if !running && !requestingPermission { audioRouting.discoverTranslatorDevices(audioDevices) }
    }
    func selectAudioRouting(_ routing: AudioRouting) {
        guard !running && !requestingPermission else { return }
        if nativeCallAudioReady && (routing.mode != audioRouting.mode || routing.listeningOutput != audioRouting.listeningOutput) {
            error = "Desative o áudio nativo da chamada antes de mudar o modo ou os fones."
            return
        }
        audioRouting = routing
        audioRouting.discoverTranslatorDevices(audioDevices)
        error = nil
    }
    func prepareNativeCallAudio() async {
        guard !running && !requestingPermission && !nativeSetupInFlight && audioRouting.mode == .call && !nativeCallAudioReady else { return }
        refreshAudioDevices()
        if let issue = audioRouting.nativePreparationError(devices: audioDevices) {
            error = issue
            return
        }
        requestingPermission = true; nativeSetupInFlight = true; error = nil
        let current = generation, output = audioRouting.listeningOutput
        let session = nativeCallAudio, queue = nativeAudioQueue
        let failure: String? = await withCheckedContinuation { continuation in
            queue.async {
                do {
                    try session.prepare(withOutputDeviceUID: output)
                    // Verify the same enumeration consumed by the pickers, not only
                    // the native object's IDs. HAL publishes topology asynchronously.
                    for _ in 0..<40 {
                        if AudioRouting.nativeDevicesAvailable(in: AudioDevice.available()) {
                            continuation.resume(returning: nil)
                            return
                        }
                        Thread.sleep(forTimeInterval: 0.025)
                    }
                    session.close()
                    continuation.resume(returning: "O macOS não disponibilizou os dois dispositivos da chamada. Confira a permissão de captura de áudio do sistema e tente preparar novamente.")
                }
                catch { session.close(); continuation.resume(returning: error.localizedDescription) }
            }
        }
        nativeSetupInFlight = false
        guard generation == current else { queue.async { session.close() }; return }
        requestingPermission = false
        if let failure { error = failure; return }
        nativeCallAudioReady = true
        audioRouting.callOutput = NativeCallAudioSession.microphoneUID()
        audioRouting.callInput = NativeCallAudioSession.speakersUID()
        refreshAudioDevices()
    }
    func closeNativeCallAudio() {
        stop()
        nativeAudioQueue.sync { nativeCallAudio.close() }
        nativeCallAudioReady = false
        if audioRouting.callOutput == NativeCallAudioSession.microphoneUID() { audioRouting.callOutput = "" }
        if audioRouting.callInput == NativeCallAudioSession.speakersUID() { audioRouting.callInput = "" }
        refreshAudioDevices()
    }
    private var timer: Timer?
    private var terminationObserver: NSObjectProtocol?
    private var generation = 0
    private var speakerIdentities: [String: Int] = [:]
    private var lastMetricsRefresh: TimeInterval = 0

    init(preferencesDefaults: UserDefaults = .standard) {
        self.preferencesDefaults = preferencesDefaults
        directionPreferences = DirectionPreferences.load(from: preferencesDefaults)
        terminationObserver = NotificationCenter.default.addObserver(forName: NSApplication.willTerminateNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.closeNativeCallAudio() }
        }
        refreshAudioDevices()
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in self?.refresh() }
        }
    }

    deinit {
        timer?.invalidate()
        if let terminationObserver { NotificationCenter.default.removeObserver(terminationObserver) }
    }

    func start() async {
        guard !running && !requestingPermission else { return }
        guard let model = asrModel, let resources = ModelCatalog.resourcesPath else {
            error = "Modelos locais ausentes ou incompletos. Verifique os recursos do aplicativo antes de iniciar."
            return
        }
        refreshAudioDevices()
        if audioRouting.mode == .call && audioRouting.usesNativeDevices && !nativeCallAudioReady {
            error = "Prepare os dispositivos nativos nesta instância do tradutor antes de iniciar a chamada."
            return
        }
        if let routeError = audioRouting.validationError(devices: audioDevices) { error = routeError; return }
        let outgoingOptions = directionPreferences.outgoing
        let incomingOptions = directionPreferences.incoming
        if let issue = outgoingOptions.validationError ?? (audioRouting.mode == .call ? incomingOptions.validationError : nil) {
            error = issue; return
        }
        let glossaryJSON = GlossaryStore.shared.engineJSON()
        generation += 1
        let requestGeneration = generation
        requestingPermission = true
        error = nil
        let authorized: Bool
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized: authorized = true
        case .notDetermined: authorized = await AVCaptureDevice.requestAccess(for: .audio)
        default: authorized = false
        }
        guard requestGeneration == generation else { return }
        requestingPermission = false
        guard authorized else {
            error = "Permita o microfone em Ajustes do Sistema → Privacidade e Segurança → Microfone."
            return
        }
        do {
            engine.configureASRModel(model.path, sha256: model.sha256)
            engine.configureASRBackend(selectedASRBackend)
            engine.configureConversationResources(resources)
            let call = audioRouting.mode == .call
            engine.configureInputDeviceUID(audioRouting.microphone,
                outputDeviceUID: call ? audioRouting.callOutput : audioRouting.listeningOutput,
                sourceLanguage: outgoingOptions.sourceLanguage.rawValue)
            try engine.configureProcessingOptions(outgoingOptions.engineJSON(glossaryJSON: glossaryJSON))
            try engine.start()
            if call {
                incomingEngine.configureASRModel(model.path, sha256: model.sha256)
                incomingEngine.configureASRBackend(selectedASRBackend)
                incomingEngine.configureConversationResources(resources)
                incomingEngine.configureInputDeviceUID(audioRouting.callInput, outputDeviceUID: audioRouting.listeningOutput,
                    sourceLanguage: incomingOptions.sourceLanguage.rawValue)
                try incomingEngine.configureProcessingOptions(incomingOptions.engineJSON(glossaryJSON: glossaryJSON))
                try incomingEngine.start()
            }
            running = engine.running && (!call || incomingEngine.running)
        } catch { self.error = error.localizedDescription; stopAudioPaths() }
        refresh()
    }

    private func stopAudioPaths() {
        // Invalidate both PCM paths before either hardware graph teardown can wait.
        engine.requestStop()
        incomingEngine.requestStop()
        engine.stop()
        incomingEngine.stop()
    }
    func stop() {
        performanceRecording.stop(.session)
        generation += 1
        requestingPermission = false
        stopAudioPaths()
        incomingSnapshot.removeAll()
        running = false
        error = nil
        cpuHistory.removeAll(keepingCapacity: false)
        ramHistory.removeAll(keepingCapacity: false)
        firstAudioHistory.removeAll(); e2eHistory.removeAll(); asrHistory.removeAll(); asrQueueHistory.removeAll()
        snapshot.removeAll(keepingCapacity: false)
        incomingHistories.removeAll(keepingCapacity: false)
        transcripts.removeAll(keepingCapacity: false)
        participants.removeAll(keepingCapacity: false)
        speakerIdentities.removeAll(keepingCapacity: false)
    }

    func testPlayback() {
        // Never inject a diagnostic tone into the remote participants' microphone.
        if audioRouting.mode == .call { incomingEngine.playTestTone() } else { engine.playTestTone() }
    }
    func selectProfile(_ profile: String) {
        guard !running && !requestingPermission, profile != selectedProfile,
              let choice = availableProfiles.first(where: { $0.id == profile }) else { return }
        stop()
        selectedProfile = profile
        asrModel = choice.model
    }
    func number(_ key: String) -> Double? { (snapshot[key] as? NSNumber)?.doubleValue }
    func refresh(forceMetrics: Bool = false) {
        let now = ProcessInfo.processInfo.systemUptime
        let measure = forceMetrics || lastMetricsRefresh == 0 || now - lastMetricsRefresh >= 1
        let call = audioRouting.mode == .call
        if measure {
            snapshot = engine.snapshot()
            incomingSnapshot = call ? incomingEngine.snapshot() : [:]
            if performanceRecording.isRecording {
                performanceRecording.append(outgoing: snapshot, incoming: incomingSnapshot,
                    context: .init(call: call, profile: selectedProfile, model: asrModel?.name ?? "NOT MEASURED",
                                   modelHash: asrModel?.sha256 ?? "NOT MEASURED",
                                   requestedBackend: selectedASRBackend, session: generation,
                                   outgoingRoute: directionPreferences.outgoing.routeLabel,
                                   incomingRoute: directionPreferences.incoming.routeLabel,
                                   inPersonRoute: directionPreferences.outgoing.routeLabel), uptime: now)
            }
            lastMetricsRefresh = now
            refreshAudioDevices()
        }
        let events = engine.drainConversationEvents().map { ($0, call ? "Você → chamada" : "Presencial", 0) }
            + incomingEngine.drainConversationEvents().map { ($0, "Chamada → você", 64) }
        for (event, path, speakerOffset) in events {
            guard running, let session = event["session"] as? NSNumber, let segment = event["segment"] as? NSNumber else { continue }
            let id = "\(path)-\(session)-\(segment)"
            if event["state"] as? String == "removed" { transcripts.removeAll(where: { $0.id == id }); continue }
            var item = transcripts.first(where: { $0.id == id }) ?? Transcript(id: id, text: "", language: "")
            if let text = event["text"] as? String, !text.isEmpty { item.text = text }
            if let text = event["translation"] as? String, !text.isEmpty { item.translation = text }
            if let language = event["language"] as? String, !language.isEmpty { item.language = language }
            if let language = event["targetLanguage"] as? String, !language.isEmpty { item.targetLanguage = language }
            item.processingState = event["state"] as? String ?? item.processingState
            item.audioPath = path
            if let speaker = (event["speaker"] as? NSNumber)?.intValue, speaker > 0 {
                let identity = "\(speakerOffset)-\(speaker)"
                if speakerIdentities[identity] == nil && speakerIdentities.count < 64 {
                    speakerIdentities[identity] = speakerIdentities.count + 1
                }
                item.speaker = speakerIdentities[identity] ?? 0
            }
            item.error = event["error"] as? String ?? ""
            if let voice = event["voice"] as? String, !voice.isEmpty { item.voice = voice }
            if item.speaker > 0 && item.speaker <= 64 {
                var participant = participants[item.speaker] ?? Participant(id: item.speaker)
                if !item.language.isEmpty { participant.language = item.language }
                participant.state = item.processingState
                if !item.voice.isEmpty { participant.voice = item.voice }
                participants[item.speaker] = participant
            }
            if let index = transcripts.firstIndex(where: { $0.id == id }) { transcripts[index] = item }
            else { transcripts.append(item) }
        }
        for item in ModelCatalog.resourcesPath == nil ? engine.drainTranscripts() : [] {
            guard let text = item["text"] as? String, let language = item["language"] as? String else { continue }
            transcripts.append(Transcript(text: text, language: language,
                                          targetLanguage: item["targetLanguage"] as? String ?? "",
                                          languageStatus: item["languageStatus"] as? String ?? ""))
        }
        transcripts = Array(transcripts.suffix(80))
        if running, let pipelineError = snapshot["conversationError"] as? String, !pipelineError.isEmpty { error = pipelineError }
        if running, let asrError = snapshot["asrError"] as? String, !asrError.isEmpty { error = "Reconhecimento indisponível: " + asrError }
        if running && ["audioDropped", "vadDropped", "segmentsDropped", "transcriptDropped", "eventDrops"].contains(where: { (number($0) ?? 0) > 0 }) {
            error = "Houve perda de trechos por congestionamento. As filas e os descartes estão na aba Engine."
        }
        if running && (!engine.running || (call && !incomingEngine.running)) {
            let detail = snapshot["audioError"] as? String ?? incomingSnapshot["audioError"] as? String
            stop()
            error = detail ?? "Um caminho de áudio foi interrompido. Confira os dispositivos e inicie novamente."
        }
        if running && call {
            for key in ["conversationError", "asrError"] {
                if let detail = incomingSnapshot[key] as? String, !detail.isEmpty { error = "Chamada → você: " + detail }
            }
            if ["audioDropped", "vadDropped", "segmentsDropped", "transcriptDropped", "eventDrops"].contains(where: { (incomingSnapshot[$0] as? NSNumber)?.intValue ?? 0 > 0 }) {
                error = "Chamada → você: houve perda de trechos por congestionamento."
            }
        }
        if running, measure, let cpu = number("cpuPercent") { cpuHistory.append(cpu) }
        if running, measure, let ram = number("ramBytes") { ramHistory.append(ram / 1_048_576) }
        if running, measure {
            if call {
                for key in ["firstAudioMs", "e2eMs", "asrLatencyMs", "segmentsDepth"] {
                    if let value = (incomingSnapshot[key] as? NSNumber)?.doubleValue {
                        incomingHistories[key] = Array(((incomingHistories[key] ?? []) + [value]).suffix(60))
                    }
                }
            }
            if let value = number("firstAudioMs") { firstAudioHistory = Array((firstAudioHistory + [value]).suffix(60)) }
            if let value = number("e2eMs") { e2eHistory = Array((e2eHistory + [value]).suffix(60)) }
            if let value = number("asrLatencyMs") { asrHistory = Array((asrHistory + [value]).suffix(60)) }
            if let value = number("segmentsDepth") { asrQueueHistory = Array((asrQueueHistory + [value]).suffix(60)) }
        }
        cpuHistory = Array(cpuHistory.suffix(60))
        ramHistory = Array(ramHistory.suffix(60))
    }
}
