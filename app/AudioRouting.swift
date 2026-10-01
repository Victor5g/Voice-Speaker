import Foundation

struct AudioDevice: Identifiable, Equatable {
    var id: String
    var name: String
    var inputChannels: Int
    var outputChannels: Int
    var isVirtual: Bool

    static func available() -> [AudioDevice] {
        TranslationEngineBridge.availableAudioDevices().compactMap {
            guard let uid = $0["uid"] as? String, let name = $0["name"] as? String else { return nil }
            let native = uid == NativeCallAudioSession.microphoneUID() || uid == NativeCallAudioSession.speakersUID()
            return AudioDevice(id: uid, name: name,
                inputChannels: ($0["inputChannels"] as? NSNumber)?.intValue ?? 0,
                outputChannels: ($0["outputChannels"] as? NSNumber)?.intValue ?? 0,
                isVirtual: native || (($0["virtual"] as? NSNumber)?.boolValue ?? false))
        }.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }
}

struct AudioRouting: Equatable {
    static let translatorMicrophoneUID = "LocalVoiceTranslatorMicrophone_UID"
    static let translatorSpeakersUID = "LocalVoiceTranslatorSpeakers_UID"
    enum Mode: String, CaseIterable { case inPerson = "Presencial", call = "Chamada" }
    var mode: Mode = .inPerson
    var microphone = ""
    var listeningOutput = ""
    var callInput = ""
    var callOutput = ""
    var headphonesConfirmed = false
    var usesNativeDevices: Bool {
        callOutput == NativeCallAudioSession.microphoneUID() || callOutput == NativeCallAudioSession.speakersUID() ||
        callInput == NativeCallAudioSession.microphoneUID() || callInput == NativeCallAudioSession.speakersUID()
    }

    static func callDevices(in devices: [AudioDevice]) -> [AudioDevice] {
        devices.filter { $0.isVirtual && $0.inputChannels > 0 && $0.outputChannels > 0 }
    }
    static func nativeDevicesAvailable(in devices: [AudioDevice]) -> Bool {
        [NativeCallAudioSession.microphoneUID(), NativeCallAudioSession.speakersUID()].allSatisfy { uid in
            devices.contains { $0.id == uid && $0.isVirtual && $0.inputChannels == 2 && $0.outputChannels == 2 }
        }
    }
    mutating func discoverTranslatorDevices(_ devices: [AudioDevice]) {
        guard mode == .call else { return }
        let available = Self.callDevices(in: devices)
        // Only fill empty choices. Never replace a disconnected/user-selected route.
        if callOutput.isEmpty {
            callOutput = [NativeCallAudioSession.microphoneUID(), Self.translatorMicrophoneUID].first { uid in available.contains { $0.id == uid } } ?? ""
        }
        if callInput.isEmpty {
            callInput = [NativeCallAudioSession.speakersUID(), Self.translatorSpeakersUID].first { uid in available.contains { $0.id == uid } } ?? ""
        }
    }

    func nativePreparationError(devices: [AudioDevice]) -> String? {
        guard devices.contains(where: { $0.id == microphone && !$0.isVirtual && $0.inputChannels > 0 }) else {
            return "Selecione seu microfone físico em ‘Seu microfone’."
        }
        guard let output = devices.first(where: { $0.id == listeningOutput && !$0.isVirtual && $0.outputChannels > 0 }) else {
            return "Selecione seus fones em ‘Ouvir traduções nos fones’."
        }
        guard output.outputChannels == 2 && output.inputChannels == 0 else {
            return "Esta saída compartilha entrada de microfone ou não é estéreo. O modo nativo precisa de fones com saída estéreo separada; Bluetooth com entrada e saída no mesmo dispositivo ainda não é suportado."
        }
        guard headphonesConfirmed else { return "Marque ‘Estou usando fones de ouvido’ para preparar a chamada." }
        return nil
    }

    func validationError(devices: [AudioDevice]) -> String? {
        func device(_ uid: String, input: Bool) -> AudioDevice? {
            devices.first { $0.id == uid && (input ? $0.inputChannels : $0.outputChannels) > 0 }
        }
        if mode == .inPerson {
            if !microphone.isEmpty && device(microphone, input: true) == nil { return "O microfone selecionado está indisponível." }
            if !listeningOutput.isEmpty && device(listeningOutput, input: false) == nil { return "A saída selecionada está indisponível." }
            if !microphone.isEmpty && microphone == listeningOutput && device(microphone, input: true)?.isVirtual == true {
                return "Entrada e saída não podem usar o mesmo dispositivo virtual."
            }
            if (!microphone.isEmpty || !listeningOutput.isEmpty) && !headphonesConfirmed {
                return "Use fones e confirme abaixo para utilizar dispositivos personalizados."
            }
            return nil
        }
        if Self.callDevices(in: devices).count < 2 {
            return "Áudio de chamada não preparado. Escolha seu microfone e seus fones e use ‘Preparar áudio da chamada’."
        }
        if usesNativeDevices && (callOutput != NativeCallAudioSession.microphoneUID() || callInput != NativeCallAudioSession.speakersUID()) {
            return "No áudio nativo, envie para LocalVoiceTranslator Microphone e receba de LocalVoiceTranslator Speakers."
        }
        guard let mic = device(microphone, input: true), let listening = device(listeningOutput, input: false),
              let incoming = device(callInput, input: true), let outgoing = device(callOutput, input: false) else {
            return "Selecione os quatro dispositivos. A chamada precisa de dois dispositivos virtuais independentes."
        }
        if mic.isVirtual || listening.isVirtual { return "Escolha seu microfone físico e seus fones para os caminhos pessoais." }
        if !incoming.isVirtual || !outgoing.isVirtual || incoming.outputChannels == 0 || outgoing.inputChannels == 0 {
            return "Entrada e saída da chamada precisam ser dispositivos virtuais com entrada e saída."
        }
        if incoming.id == outgoing.id { return "Use dispositivos virtuais diferentes para receber e enviar; o mesmo dispositivo cria retorno." }
        if !headphonesConfirmed { return "Use fones na chamada e confirme abaixo para evitar que o microfone capte a tradução." }
        return nil
    }
}
