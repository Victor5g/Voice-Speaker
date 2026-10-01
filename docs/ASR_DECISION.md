# ASR implementation and measured choice

The official pinned whisper.cpp v1.8.3 is statically linked into the app and tests.
Metal and Accelerate are enabled; network/server/SDL/FFmpeg functionality is excluded.
Multilingual F16 tiny/base/small model hashes were verified against pinned upstream
artifact metadata, recorded in models/manifest.json, and revalidated locally.

The development benchmark runs the production C++ wrapper, including actual language
probabilities and transcription. It excludes loading and has one excluded warmup plus
10 measured runs on each of three synthetic fixtures. M2 Pro results and raw text are
in BENCHMARKS.md / artifacts/benchmark. No phrase or language result is hardcoded in
the engine. Fixture references exist only in tests/benchmark scripts.

Greedy decoding repeated “o que é” at an eight-second VAD cut in the Portuguese fixture.
Beam search 5 with temperature 0 eliminated that observed repetition while preserving
the bounded segment size. The benchmark was rerun for all three candidate models with
the final decoder, and small again had the lowest literal word error. A separate
integration test now measures complete VAD-segmented transcripts and rejects fixture
WER above 0.25. This gate prevents the observed regression; it is not a broad speech
quality claim. No speculative text cleanup hides recognition errors.

Language detection inspects all Whisper probabilities. UI maps pt→PT-BR and en→EN,
otherwise Unsupported. The subsequent LanguageStabilizer/LanguageRouter stage now rejects weak or unsupported
evidence and determines the opposite output language. Diarization is absent; the UI
says speaker unidentified and unknown speakers do not share language history.

The selected model alone is copied into build/ModelResources and then into the app.
C++ verifies SHA-256 before opening model structures. Sinc conversion runs on the
ASR worker; tests cover DC, duration, anti-alias rejection, and invalid rates. Bounded
queues, real worker failures, cancellation cleanup and synthetic fixture inference are
tested. Model loading/LID cannot currently be interrupted immediately, so STOP may
wait for them. Human speech, varied accents/noise and GUI microphone inference after
the latest rebuild still require runtime validation.
