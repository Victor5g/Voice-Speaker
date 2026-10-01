# Audio acceptance fixtures

No recorded conversation was supplied, generated, downloaded or stored.
`conversation_ptbr_en.wav` is absent. Golden inference is NOT IMPLEMENTED until the
preceding phase gates, licensed models and runtime are available.

The future fixture suite must cover all 15 scenarios in `docs/REQUIREMENTS.md`, including
EN-US, EN-GB and other English accents, one speaker switching language, 2/3 speakers,
returning speakers, overlap, noise, silence, music/TV, live playback leakage and false
language oscillation. Every label and expected output must be derived from real audio.
Integration artifacts may only be written to `artifacts/test-output/` with explicit
test invocation. The app itself must not save microphone captures.
