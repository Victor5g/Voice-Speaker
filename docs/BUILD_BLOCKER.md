# Historical Xcode blocker — resolved 2026-09-11

**No further authorization is needed for the Xcode temporary/cache exception.**
The user explicitly answered “Autorizo sim!!”. See [authorization](BUILD_AUTHORIZATION.md).
Actual `xcodebuild` Debug, Release and XCTest now pass. The incomplete translator
is no longer blocked by Xcode; its remaining AI/runtime acceptance is separate.

## Original reproduction

On macOS 26.6.2 / Xcode 26.5, the additional project write sandbox initially caused
Debug and Release exit 65 with “Unable to write to info file ...DerivedData/info.plist”,
NSCocoaErrorDomain 513 / NSPOSIXErrorDomain 1. The visible destination was local,
but Foundation attempted atomic-replacement scratch writes in the current user's
`/private/var/folders/.../T/TemporaryItems/NSIRD_xcodebuild_*`. Developer plugin caches
under that user's C directory were also needed. TMPDIR did not redirect these writes.
Historical sandbox evidence is retained in `artifacts/xcode-sandbox-denials.log`.
Current build-debug.log and build-release.log now contain successful builds.

## Applied resolution

`project-local.sh` asks getconf for DARWIN_USER_TEMP_DIR and DARWIN_USER_CACHE_DIR,
canonicalizes each, validates the /private/var/folders per-user T/C shape, and permits
those narrowly scoped temporary/cache writes alongside the project. It also permits
terminal data writes including XCTest pseudo-terminals. No global package installation,
HOME/PATH setting, shell profile, privileged command or system executable modification.
Dependencies, models, tooling, DerivedData, controlled caches and artifacts remain local.

Hosted XCTest under the extra tool sandbox launched the app without its injection
environment and timed out. A hostless XCTest target now compiles the actual production
AppState, ModelCatalog, bridge and engine; it has an app build dependency. This runs
real XCTest with Xcode while app runtime validation remains a separate gate.
`-collect-test-diagnostics never` avoids invoking OS sysdiagnose outside the project.

The app must be opened normally through LaunchServices so macOS initializes its own
App Sandbox. Nesting the tool sandbox over the executable caused a secinit failure,
“Could not set sandbox profile data: Operation not permitted”. The app's App Sandbox
and microphone-only entitlement remain enabled. The known OS-created crash report is
documented in PRIVACY.md; no external logs or TCC records were deleted.

## Current runtime boundary

Xcode is no longer blocked. Production ASR/translation/TTS, diarization and PCM
are implemented, with automated WAV and cancellation gates. Explicit GENERAL
WEBCAM input and External Headphones output passed capture during a 350 ms tone,
Whisper/Metal ready, auxiliary-window close and STOP in the current call-routing
work. This does not establish acoustic echo cancellation or meeting compatibility.
See `artifacts/custom-audio-verification.json` and `docs/FINAL_REPORT.md`.

The Audio tab supports four explicit routes and now prepares native Core Audio
aggregate/tap devices from inside the app (macOS 14.2+, stereo output with no input
channels on the same UID). No third-party driver installation is required for this
native path. A sandboxed production-controller test passed bidirectional PCM between
two processes and removal with an active client. Each meeting application, physical
speech and user-disconnected network remain manual acceptance. See CALL_AUDIO_SETUP.md.
The long-test source fingerprint must match the engine that is packaged.
