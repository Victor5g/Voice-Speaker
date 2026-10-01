import SwiftUI

struct AudioRoutingView: View {
    @EnvironmentObject var state: AppState
    var onConfigureLanguages: () -> Void = {}
    private var locked: Bool { state.running || state.requestingPermission }
    private var hasCallDevices: Bool { AudioRouting.callDevices(in: state.audioDevices).count >= 2 }
    private func binding<T>(_ key: WritableKeyPath<AudioRouting, T>) -> Binding<T> {
        Binding(get: { state.audioRouting[keyPath: key] }, set: { value in
            var route = state.audioRouting; route[keyPath: key] = value; state.selectAudioRouting(route)
        })
    }
    private func picker(_ title: String, key: WritableKeyPath<AudioRouting, String>, input: Bool, virtual: Bool? = nil) -> some View {
        let devices = state.audioDevices.filter { (input ? $0.inputChannels : $0.outputChannels) > 0 && (virtual == nil || $0.isVirtual == virtual) }
        let selected = state.audioRouting[keyPath: key]
        return Picker(title, selection: binding(key)) {
            Text(devices.isEmpty ? "Nenhum dispositivo disponível" : state.audioRouting.mode == .call ? "Selecione…" : "Padrão do sistema").tag("")
            if !selected.isEmpty && !devices.contains(where: { $0.id == selected }) {
                Text("Dispositivo indisponível").tag(selected)
            }
            ForEach(devices) { Text($0.name).tag($0.id) }
        }.accessibilityIdentifier(title)
    }
    var body: some View {
        GeometryReader { viewport in
        VStack(alignment: .leading, spacing: 12) {
            Text("Caminhos de áudio").font(.title2.weight(.semibold))
            Picker("Uso", selection: binding(\.mode)) {
                ForEach(AudioRouting.Mode.allCases, id: \.self) { Text($0.rawValue).tag($0) }
            }.pickerStyle(.segmented).disabled(locked)
            Button("Configurar idiomas e vozes", action: onConfigureLanguages)
            // Form owns its scrolling. Setup actions remain outside it, visible even
            // at the minimum window size; no second ScrollView hides the button.
            GeometryReader { space in
            Form {
                if state.audioRouting.mode == .call {
                    Section("1. Escolha seu microfone e seus fones") {
                        picker("Seu microfone", key: \.microphone, input: true, virtual: false)
                        picker("Ouvir traduções nos fones", key: \.listeningOutput, input: false, virtual: false)
                        Toggle("Estou usando fones de ouvido", isOn: binding(\.headphonesConfirmed))
                            .accessibilityIdentifier("call.headphones")
                            .background {
                                #if DEBUG
                                CallSetupControlMarker(identifier: "call.headphones")
                                #endif
                            }
                    }
                    if hasCallDevices {
                        Section("2. Dispositivos da chamada") {
                            picker("Enviar tradução à chamada", key: \.callOutput, input: false, virtual: true)
                            picker("Receber áudio da chamada", key: \.callInput, input: true, virtual: true)
                            Text("Na chamada, selecione o dispositivo de envio como microfone e o de recepção como alto-falante. Depois, inicie a escuta no tradutor.").foregroundStyle(.secondary)
                        }
                    } else {
                        Section("2. Crie os dispositivos da chamada") {
                            Text("Depois de escolher os fones, clique em ‘Preparar áudio da chamada’ no rodapé. O app disponibilizará os dois dispositivos e preencherá os caminhos de envio e recepção.")
                            Text("Não é preciso selecionar nada de envio ou recepção antes de preparar.").foregroundStyle(.secondary)
                        }
                    }
                    DisclosureGroup("Como conectar e remover os dispositivos") {
                        Text("Envio: \(state.directionPreferences.outgoing.routeLabel). Recepção: \(state.directionPreferences.incoming.routeLabel). Altere idiomas, vozes e saída em Ajustes. A tradução tem atraso enquanto a fala é reconhecida e a voz é preparada.")
                        Text("No Meet, Zoom, Teams, Discord ou Slack, selecione microfone ‘LocalVoiceTranslator Microphone’ e saída ‘LocalVoiceTranslator Speakers’ ao usar o modo nativo. No navegador, a seleção da saída depende do site e do navegador.")
                        Text("O modo nativo exige macOS 14.2+ e saída estéreo separada do microfone. Bluetooth com entrada e saída no mesmo dispositivo ainda não é suportado. O macOS pode pedir permissão para capturar áudio do sistema.")
                        Text("STOP silencia a tradução e mantém os dispositivos. Antes de desativar o áudio nativo ou sair do app, restaure o microfone e os fones na chamada. Os dispositivos são removidos ao sair normalmente, sem driver instalado. Após encerramento forçado, podem restar agregados na Configuração de Áudio MIDI.")
                        Text("Desative o monitoramento da própria voz na chamada. Mantenha o tradutor aberto e a escuta iniciada.")
                    }.foregroundStyle(.secondary)
                } else {
                    picker("Microfone", key: \.microphone, input: true)
                    picker("Reproduzir traduções em", key: \.listeningOutput, input: false)
                    Text("Com ambos em ‘Padrão do sistema’, a proteção acústica usa o processamento de voz do macOS. Dispositivos personalizados exigem fones.").foregroundStyle(.secondary)
                    Toggle("Estou usando fones de ouvido", isOn: binding(\.headphonesConfirmed))
                }
            }.disabled(locked).formStyle(.grouped)
                .frame(width: space.size.width, height: space.size.height)
            }
            Divider()
            if state.audioRouting.mode == .call {
                if state.nativeCallAudioReady {
                    Label("Dispositivos nativos preparados", systemImage: "checkmark.circle")
                    Button("Desativar áudio nativo da chamada") { state.closeNativeCallAudio() }.disabled(locked)
                } else {
                    HStack {
                        Button("Preparar áudio da chamada") { Task { await state.prepareNativeCallAudio() } }
                            .buttonStyle(.borderedProminent)
                            .accessibilityIdentifier("call.prepare")
                            .background {
                                #if DEBUG
                                CallSetupControlMarker(identifier: "call.prepare")
                                #endif
                            }
                            .disabled(locked)
                        if state.nativeSetupInFlight { ProgressView().controlSize(.small); Text("Preparando…") }
                    }
                    Text("Sem instalar driver. Primeiro escolha seu microfone e seus fones acima.").font(.caption).foregroundStyle(.secondary)
                }
            }
            if let error = state.error {
                Text(error).foregroundStyle(.orange).fixedSize(horizontal: false, vertical: true)
                    .background {
                        #if DEBUG
                        CallSetupControlMarker(identifier: "call.error")
                        #endif
                    }
            }
            else if state.audioRouting.mode != .call || hasCallDevices,
                    let error = state.audioRouting.validationError(devices: state.audioDevices) {
                Text(error).foregroundStyle(.orange).fixedSize(horizontal: false, vertical: true)
            }
            HStack {
                Button("Atualizar dispositivos") { state.refreshAudioDevices() }.disabled(locked)
                Text("Pare a sessão antes de mudar os caminhos.").font(.caption).foregroundStyle(.secondary)
            }
        }.padding(20)
            .frame(width: viewport.size.width, height: viewport.size.height, alignment: .topLeading)
        }
    }
}

#if DEBUG
/// Geometry only: does not intercept clicks or replace the production button action.
struct CallSetupControlMarker: NSViewRepresentable {
    static var controls: [String: NSView] = [:]
    var identifier: String
    final class MarkerView: NSView {
        override func hitTest(_ point: NSPoint) -> NSView? { nil }
    }
    func makeNSView(context: Context) -> NSView {
        let view = MarkerView(); Self.controls[identifier] = view; return view
    }
    func updateNSView(_ view: NSView, context: Context) { Self.controls[identifier] = view }
    static func dismantleNSView(_ view: NSView, coordinator: ()) {
        controls = controls.filter { $0.value !== view }
    }
}
#endif
