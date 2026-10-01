import SwiftUI
struct AboutView: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Image(systemName: "waveform").font(.system(size: 44)).foregroundStyle(.teal)
            Text("LocalVoiceTranslator").font(.largeTitle.weight(.semibold))
            Text("0.2.1 · Uso pessoal")
            Text("Tradutor conversacional local para português brasileiro e inglês.").foregroundStyle(.secondary)
            Text("Reconhecimento, tradução, vozes e participantes processados neste Mac. Falas simultâneas podem exigir repetição. Validação acústica e offline em andamento.")
            Label("Sem conta, servidor ou telemetria", systemImage: "lock.shield")
            Spacer()
        }.padding(36).frame(maxWidth: .infinity, alignment: .leading)
    }
}
