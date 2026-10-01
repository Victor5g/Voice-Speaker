import SwiftUI

@main
struct LocalVoiceTranslatorApp: App {
    @StateObject private var state = AppState()
    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(state)
                .frame(minWidth: 840, minHeight: 620)
                .task {
                    #if DEBUG
                    await AppDiagnostics.runIfRequested(state: state)
                    #endif
                }
                .background(SessionWindowObserver { state.stop() })
        }
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandGroup(after: .appInfo) { Button("Encerrar sessão") { state.stop() }.keyboardShortcut(".") }
        }
        Settings { SettingsView().environmentObject(state).frame(width: 600, height: 620) }
    }
}

private struct SessionWindowObserver: NSViewRepresentable {
    var onClose: () -> Void
    func makeNSView(context: Context) -> WindowView { WindowView(onClose: onClose) }
    func updateNSView(_ view: WindowView, context: Context) { view.onClose = onClose }
    final class WindowView: NSView {
        var onClose: () -> Void
        private var observer: NSObjectProtocol?
        init(onClose: @escaping () -> Void) { self.onClose = onClose; super.init(frame: .zero) }
        required init?(coder: NSCoder) { fatalError("Window observer is constructed in code") }
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let observer { NotificationCenter.default.removeObserver(observer) }
            observer = nil
            guard let window else { return }
            // Auxiliary windows, including permission panels, must not stop audio.
            observer = NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: window, queue: .main) { [weak self] _ in
                self?.onClose()
            }
        }
        deinit { if let observer { NotificationCenter.default.removeObserver(observer) } }
    }
}

struct RootView: View {
    @Environment(\.openSettings) private var openSettings
    // Explicit DynamicProperty storage avoids the SDK 27 State macro's nested
    // plugin sandbox; equivalent to the legacy @State backing storage.
    private var _selected = SwiftUI.State<String>(initialValue: "Conversation")
    private var selected: String { _selected.wrappedValue }
    init() {
        #if DEBUG
        // Render the real Engine view for local screenshot diagnostics without
        // requiring global Accessibility permission or changing the Release UI.
        if CommandLine.arguments.contains("--show-engine") { _selected = State(initialValue: "Engine") }
        if CommandLine.arguments.contains("--show-audio") { _selected = State(initialValue: "Áudio") }
        if CommandLine.arguments.contains("--show-languages") { _selected = State(initialValue: "Idiomas e vozes") }
        #endif
    }
    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 26) {
                Label("LocalVoice", systemImage: "waveform").font(.title2.weight(.semibold)).padding(.top, 24)
                List(selection: _selected.projectedValue) {
                    Label("Conversation", systemImage: "bubble.left.and.bubble.right").tag("Conversation")
                    Label("Engine", systemImage: "waveform.path.ecg").tag("Engine")
                    Label("Áudio", systemImage: "arrow.triangle.branch").tag("Áudio")
                    Label("Idiomas e vozes", systemImage: "character.bubble").tag("Idiomas e vozes")
                    Label("Sobre", systemImage: "info.circle").tag("Sobre")
                }.listStyle(.sidebar)
                VStack(alignment: .leading, spacing: 6) {
                    Label("Processamento local", systemImage: "lock.shield")
                    Text("PT-BR ⇄ EN").foregroundStyle(.secondary)
                }.font(.caption).padding(.bottom, 20)
            }.padding(.horizontal, 14).navigationSplitViewColumnWidth(200)
        } detail: {
            switch selected {
            case "Engine": EngineView(onConfigureLanguages: { _selected.wrappedValue = "Idiomas e vozes" })
            case "Sobre": AboutView()
            case "Áudio": AudioRoutingView(onConfigureLanguages: { _selected.wrappedValue = "Idiomas e vozes" })
            case "Idiomas e vozes": SettingsView()
            default: ConversationView(onConfigureLanguages: { _selected.wrappedValue = "Idiomas e vozes" })
            }
        }.tint(.teal)
        .task {
            #if DEBUG
            if CommandLine.arguments.contains("--show-settings") { openSettings() }
            #endif
        }
    }
}
