import SwiftUI

struct PerformanceRecordingView: View {
    @EnvironmentObject var state: AppState
    var body: some View {
        GroupBox("Gravar desempenho") {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    if state.performanceRecording.isRecording {
                        Label("Gravando", systemImage: "record.circle.fill").foregroundStyle(.red)
                        Button("Parar gravação") { state.stopPerformanceRecording() }
                    } else {
                        Button("Gravar desempenho") { state.startPerformanceRecording() }
                            .disabled(state.performanceRecording.started != nil || state.exportingPerformance)
                    }
                    Spacer()
                    Button("Exportar TXT…") { state.exportPerformanceRecording() }
                        .disabled(state.performanceRecording.isRecording || state.performanceRecording.sampleCount == 0 || state.exportingPerformance)
                    Button("Limpar") { state.clearPerformanceRecording() }
                        .disabled(state.performanceRecording.isRecording || state.performanceRecording.started == nil || state.exportingPerformance)
                }
                Text("\(state.performanceRecording.sampleCount) amostras · \(Int(state.performanceRecording.elapsed)) s · \(state.performanceRecording.byteCount / 1024) KiB")
                    .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                Text("Coleta a cada segundo, incluindo os dois caminhos da chamada. Sem áudio ou texto das conversas. Limite de 1 hora ou 32 MiB. Pare a gravação para exportar; use Limpar para iniciar outra.")
                    .font(.caption).foregroundStyle(.secondary)
                if let reason = state.performanceRecording.stopReason { Text(reason.rawValue).font(.caption) }
                if let message = state.performanceExportMessage { Text(message).font(.caption).textSelection(.enabled) }
                Text("O relatório fica em memória até você exportar. STOP encerra a gravação e preserva o relatório; fechar o aplicativo descarta os dados não exportados.")
                    .font(.caption).foregroundStyle(.secondary)
            }.frame(maxWidth: .infinity, alignment: .leading).padding(6)
        }
    }
}
