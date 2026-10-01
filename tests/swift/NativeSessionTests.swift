import Foundation

// Native diagnostic runner for the same phase-1 state while the xcodebuild gate is blocked.
// Not included in the app or in its XCTest target.
@main
struct NativeSessionTests {
    @MainActor
    static func main() {
        var passed = 0
        func check(_ value: Bool, _ name: String) {
            guard value else { print("FAIL \(name)"); exit(1) }
            passed += 1
            print("PASS \(name)")
        }
        let state = AppState()
        state.refresh()
        check((state.number("ramBytes") ?? 0) > 0 && (state.number("threads") ?? 0) > 0,
              "native bridge returns actual process memory and thread count")
        check(state.number("inputFrames") == 0 && !state.running,
              "initialization does not access microphone or invent captured frames")
        for _ in 0..<80 { state.refresh() }
        check(state.cpuHistory.count <= 60 && state.ramHistory.count <= 60,
              "Swift metrics history is bounded")
        state.error = "test-only previous failure"
        state.stop()
        check(!state.running && !state.requestingPermission && state.error == nil && state.snapshot.isEmpty && state.cpuHistory.isEmpty && state.ramHistory.isEmpty,
              "Stop clears Swift state and native session synchronously")
        state.stop()
        check(!state.running && state.snapshot.isEmpty, "repeated Stop is safe")
        print("RESULT \(passed) passed, 0 failed")
    }
}
