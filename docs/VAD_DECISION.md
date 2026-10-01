# Phase 3 VAD selection

WebRTC VAD is integrated as its standalone C classifier, compiled by Xcode with
`WEBRTC_POSIX`. Source comes from py-webrtcvad 2.0.10 at the exact commit in
`third_party/dependencies.lock.json`. No Python wrapper is compiled, installed or
executed by the app. The bundled license must accompany distribution.

WebRTC offers small fixed state and 10/20/30 ms PCM frames without a neural runtime.
Silero was considered: its current ONNX interface uses 512 samples at 16 kHz (or
256 at 8 kHz), and needs both an inference runtime and model. This phase chooses
WebRTC to establish streaming segmentation with the existing native toolchain.
This is an engineering dependency decision, not an unmeasured accuracy or latency win.
WebRTC's music/noise false positives remain an acceptance risk to evaluate with
the full fixture suite. Speech detection does not establish speaker identity.

Production workers use mode 2, 20 ms frames, 150 ms preroll, at least 100 ms classified
speech, 300 ms trailing silence and an 8 second maximum segment. Sample rates
8/16/32/48 kHz are supported directly; 44.1 kHz is explicitly rejected and counted,
pending a dedicated recognition resampler. Input discontinuities discard incomplete
segments rather than joining unrelated audio. Segment output has capacity 8 and
drops newest on congestion. There is no ASR consumer yet in phase 3.

The upstream 8 kHz PCM fixture is preserved at `tests/data/webrtc-test-audio.raw`,
SHA-256 `3dbb730b90e0266d78d4fe03f00aba37a83780a6a5240d9084b5c01266194c1e`.
Tests compare all four classifier modes with the exact golden decision strings in
the pinned upstream `test_webrtcvad.py`; no Python dependency is needed to run them.
Segmentation state tests inject decisions only in unit tests. The integration test
passes actual fixture PCM through the real C classifier and worker queue.

References:
- https://github.com/wiseman/py-webrtcvad/tree/ba13ceea9dd3f3a068e869b2e94413205bd87f7d
- https://github.com/snakers4/silero-vad/releases
