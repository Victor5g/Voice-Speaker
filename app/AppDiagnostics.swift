#if DEBUG
import AppKit
import AVFoundation
import Foundation

/// Explicit development diagnostics. Never enabled in Release; no conversation data.
@MainActor
enum AppDiagnostics {
    private static var started = false

    static func runIfRequested(state: AppState) async {
        let audio = CommandLine.arguments.contains("--audio-diagnostic")
        let ui = CommandLine.arguments.contains("--ui-diagnostic")
        guard (audio || ui) && !started else { return }
        started = true
        Task { @MainActor in
            try? await Task.sleep(for: .seconds(150))
            emit(["event": "timeout", "reason": "diagnostic exceeded 150 seconds; microphone permission may be pending"])
            state.closeNativeCallAudio()
            exit(2)
        }
        if CommandLine.arguments.contains("--show-audio") {
            var route = state.audioRouting; route.mode = .call; state.selectAudioRouting(route)
        }
        if CommandLine.arguments.contains("--explicit-audio-diagnostic") {
            var route = state.audioRouting
            if let mic = state.audioDevices.first(where: { $0.name == "GENERAL WEBCAM" && $0.inputChannels > 0 }),
               let phones = state.audioDevices.first(where: { $0.name == "External Headphones" && $0.outputChannels > 0 }) {
                route.microphone = mic.id; route.listeningOutput = phones.id; route.headphonesConfirmed = true
                state.selectAudioRouting(route)
            } else { emit(["event": "audio_failed", "reason": "diagnostic microphone/headphones not available"]); exit(2) }
        }
        if ui && CommandLine.arguments.contains("--native-route-ui-diagnostic") {
            // Configuration/UI fixture only: no microphone or playback starts.
            // A stereo output is sufficient for creating the native objects here;
            // this is not evidence that a person is physically wearing headphones.
            var route = state.audioRouting; route.mode = .call
            guard let mic = state.audioDevices.first(where: { !$0.isVirtual && $0.inputChannels > 0 }),
                  let output = state.audioDevices.first(where: { !$0.isVirtual && $0.inputChannels == 0 && $0.outputChannels == 2 }) else { exit(2) }
            route.microphone = mic.id; route.listeningOutput = output.id; route.headphonesConfirmed = true
            state.selectAudioRouting(route)
            await state.prepareNativeCallAudio()
            emit(["event": "native_routes", "prepared": state.nativeCallAudioReady,
                  "callInput": state.audioRouting.callInput, "callOutput": state.audioRouting.callOutput,
                  "running": state.running, "error": state.error ?? ""])
            guard state.nativeCallAudioReady else { state.closeNativeCallAudio(); exit(2) }
        }
        NSApplication.shared.activate(ignoringOtherApps: true)
        // Give the real window its first layout/display pass, not simulated inference.
        try? await Task.sleep(for: .milliseconds(500))
        let visibleWindows = NSApplication.shared.windows.filter { $0.isVisible && $0.contentView != nil }
        let window = CommandLine.arguments.contains("--show-settings")
            ? (visibleWindows.first { $0.title.localizedCaseInsensitiveContains("settings") || $0.title.localizedCaseInsensitiveContains("ajustes") } ?? NSApplication.shared.keyWindow)
            : visibleWindows.first
        emit(["event": "visible_windows", "windows": visibleWindows.map { ["title": $0.title, "windowNumber": $0.windowNumber] }])
        emit(["event": "window", "visible": window != nil, "windowNumber": window?.windowNumber ?? 0])
        if ui && CommandLine.arguments.contains("--call-setup-ui-diagnostic"), let window {
            await verifyCallSetup(state: state, window: window)
            return
        }
        if audio {
            emit(["event": "microphone_permission_request", "authorizationStatus": AVCaptureDevice.authorizationStatus(for: .audio).rawValue])
            await state.start()
            guard state.running else {
                emit(["event": "audio_failed", "reason": state.error ?? "capture did not start"])
                state.stop()
                exit(2)
            }
            // Hardware initialization is asynchronous. Wait for measured callbacks,
            // with a real timeout, rather than assuming VPIO starts within one second.
            for _ in 0..<100 {
                try? await Task.sleep(for: .milliseconds(100))
                state.refresh(forceMetrics: true)
                if (state.number("inputFrames") ?? 0) > 0 && (state.number("outputFrames") ?? 0) > 0 { break }
            }
            let beforeInput = state.number("inputFrames") ?? 0
            let beforeOutput = state.number("outputFrames") ?? 0
            state.testPlayback()
            var peakOutput = 0.0
            // Observe actual hardware callbacks during and after the 350 ms tone.
            for _ in 0..<20 {
                try? await Task.sleep(for: .milliseconds(25))
                state.refresh(forceMetrics: true)
                peakOutput = max(peakOutput, state.number("outputLevel") ?? 0)
            }
            let captureAdvanced = (state.number("inputFrames") ?? 0) > beforeInput
            let outputAdvanced = (state.number("outputFrames") ?? 0) > beforeOutput
            emit(["event": "audio", "inputFrames": state.number("inputFrames") ?? 0,
                  "outputFrames": state.number("outputFrames") ?? 0,
                  "captureContinuedDuringPlayback": captureAdvanced,
                  "outputCallbacksAdvanced": outputAdvanced, "peakRenderedRMS": peakOutput,
                  "aec": state.number("aec") ?? 0,
                  "configurationRestarts": state.number("configurationRestarts") ?? 0,
                  "ramBytes": state.number("ramBytes") ?? 0,
                  "peakRSSBytes": state.number("peakRSSBytes") ?? 0,
                  "cpuPercent": state.number("cpuPercent") ?? 0,
                  "thermalState": state.number("thermalState") ?? 0])
            if state.asrModel != nil {
                for _ in 0..<300 {
                    state.refresh(forceMetrics: true)
                    if state.number("asrState") != 1 { break }
                    try? await Task.sleep(for: .milliseconds(100))
                }
                emit(["event": "asr_model", "state": state.number("asrState") ?? 0,
                      "running": state.running, "windowVisible": window?.isVisible ?? false,
                      "audioError": state.snapshot["audioError"] as? String ?? "",
                      "sessionError": state.error ?? "",
                      "metalActive": state.number("metalActive") ?? 0,
                      "error": state.snapshot["asrError"] as? String ?? "",
                      "ramBytes": state.number("ramBytes") ?? 0])
            }
            let asrReady = state.asrModel == nil || state.number("asrState") == 2
            let auxiliary = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 120, height: 80), styleMask: [.titled], backing: .buffered, defer: false)
            auxiliary.isReleasedWhenClosed = false
            auxiliary.close()
            try? await Task.sleep(for: .milliseconds(50))
            state.refresh(forceMetrics: true)
            let auxiliaryKeptSession = state.running && state.number("asrState") == 2
            emit(["event": "auxiliary_window_close", "keptSession": auxiliaryKeptSession])
            state.stop()
            emit(["event": "stop", "cleared": !state.running && state.snapshot.isEmpty && state.transcripts.isEmpty])
            exit(captureAdvanced && outputAdvanced && peakOutput > 0 && asrReady && auxiliaryKeptSession ? 0 : 1)
        }
        // Leave the actual composited window available to the external test screenshot.
        try? await Task.sleep(for: .seconds(2))
        state.stop()
        if CommandLine.arguments.contains("--native-route-ui-diagnostic") { window?.close() }
        NSApplication.shared.terminate(nil)
    }

    private static func press(_ identifier: String, window: NSWindow) -> Bool {
        guard let view = CallSetupControlMarker.controls[identifier], view.window === window else { return false }
        let frame = view.convert(view.bounds, to: nil)
        guard frame.width > 0, frame.height > 0,
              window.frame.contains(window.convertToScreen(frame)) else { return false }
        let point = NSPoint(x: identifier == "call.headphones" ? frame.maxX - 16 : frame.midX, y: frame.midY)
        guard let down = NSEvent.mouseEvent(with: .leftMouseDown, location: point, modifierFlags: [],
            timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
            context: nil, eventNumber: 0, clickCount: 1, pressure: 1),
              let up = NSEvent.mouseEvent(with: .leftMouseUp, location: point, modifierFlags: [],
            timestamp: ProcessInfo.processInfo.systemUptime + 0.01, windowNumber: window.windowNumber,
            context: nil, eventNumber: 1, clickCount: 1, pressure: 0) else { return false }
        // AppKit switches track synchronously inside mouseDown; queue the release
        // first so their tracking loop can consume it without waiting for real input.
        NSApplication.shared.postEvent(up, atStart: false)
        NSApplication.shared.sendEvent(down)
        return true
    }

    /// Send mouse events to the actual laid-out SwiftUI controls in this window.
    /// No global event injection, Accessibility permission, or audio capture.
    private static func verifyCallSetup(state: AppState, window: NSWindow) async {
        window.setContentSize(NSSize(width: 840, height: 620))
        try? await Task.sleep(for: .seconds(1))
        func fail(_ reason: String) {
            emit(["event": "setup_failed", "reason": reason]); state.closeNativeCallAudio(); exit(2)
        }
        func capture(_ stage: String) async {
            try? await Task.sleep(for: .milliseconds(600))
            window.contentView?.layoutSubtreeIfNeeded()
            window.displayIfNeeded()
            emit(["event": "setup_stage", "stage": stage, "windowNumber": window.windowNumber,
                  "prepared": state.nativeCallAudioReady, "error": state.error ?? "",
                  "callInput": state.audioRouting.callInput, "callOutput": state.audioRouting.callOutput,
                  "windowFrame": NSStringFromRect(window.frame),
                  "contentFrame": NSStringFromRect(window.contentView?.frame ?? .zero),
                  "errorFrame": CallSetupControlMarker.controls["call.error"].map { NSStringFromRect(window.convertToScreen($0.convert($0.bounds, to: nil))) } ?? "absent"])
            try? await Task.sleep(for: .seconds(1))
        }
        guard state.audioRouting.callInput.isEmpty && state.audioRouting.callOutput.isEmpty else { fail("Expected unprepared first-use state"); return }
        await capture("initial")
        guard press("call.prepare", window: window) else { fail("Prepare button not visible at minimum size"); return }
        try? await Task.sleep(for: .milliseconds(300))
        guard state.error?.contains("Seu microfone") == true && !state.nativeCallAudioReady else { fail("Missing choice error was not published"); return }
        await capture("missing-microphone")
        guard let errorView = CallSetupControlMarker.controls["call.error"],
              window.frame.contains(window.convertToScreen(errorView.convert(errorView.bounds, to: nil))) else {
            fail("Error text was outside the window"); return
        }
        // Physical selections are a configuration fixture, not a headphones test.
        guard let mic = state.audioDevices.first(where: { !$0.isVirtual && $0.inputChannels > 0 }),
              let output = state.audioDevices.first(where: { !$0.isVirtual && $0.inputChannels == 0 && $0.outputChannels == 2 }) else { fail("No supported physical setup fixture"); return }
        var route = state.audioRouting; route.microphone = mic.id; route.listeningOutput = output.id
        state.selectAudioRouting(route)
        try? await Task.sleep(for: .milliseconds(200))
        guard press("call.headphones", window: window) else { fail("Headphones toggle not accessible"); return }
        try? await Task.sleep(for: .milliseconds(200))
        guard state.audioRouting.headphonesConfirmed else { fail("Toggle did not change route"); return }
        await capture("selected")
        guard press("call.prepare", window: window) else { fail("Second prepare press not handled"); return }
        for _ in 0..<100 {
            try? await Task.sleep(for: .milliseconds(100))
            if state.nativeCallAudioReady || state.error != nil { break }
        }
        guard state.nativeCallAudioReady && state.error == nil && !state.running &&
            AudioRouting.nativeDevicesAvailable(in: state.audioDevices) else { fail(state.error ?? "Preparation did not publish both devices"); return }
        await capture("prepared")
        window.close()
        state.closeNativeCallAudio()
        guard !AudioDevice.available().contains(where: { $0.id == NativeCallAudioSession.microphoneUID() || $0.id == NativeCallAudioSession.speakersUID() }) else { fail("Native device remained after cleanup"); return }
        emit(["event": "setup_result", "result": "PASS", "scope": "Actual first-use button/toggle presses; physical choices are fixtures, no capture or meeting; normal cleanup verified"])
        NSApplication.shared.terminate(nil)
    }

    private static func emit(_ value: [String: Any]) {
        guard let data = try? JSONSerialization.data(withJSONObject: value, options: [.sortedKeys]),
              let line = String(data: data, encoding: .utf8) else { return }
        print("LVT_DIAGNOSTIC " + line)
        fflush(stdout)
    }
}
#endif
