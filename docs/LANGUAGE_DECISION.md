# PT-BR / English routing and stabilization

LanguageRouter maps Whisper `pt` or logical `pt-BR` only to Portuguese, `en` to English,
and every other code to Unsupported. A stable Portuguese segment routes to English;
a stable English segment routes to Brazilian Portuguese. Uncertain/unsupported input
has no target language and cannot reach translation. This stage does not translate.

LanguageStabilizer stores bounded history for at most 64 actual speaker IDs, and clears
it at session end. Unknown speakers use nullopt and get independent segment decisions;
they never share a fabricated Speaker 1. Once diarization exists, the same API can use
its actual IDs. Therefore live per-speaker stabilization is not yet validated.

Current conservative policy requires finite confidence >=0.65 and at least 700 ms.
An established speaker changes language after two consecutive candidates totaling
at least 1600 ms, or one >=3000 ms candidate with confidence >=0.9. Unsupported or weak
utterances preserve established history but do not guess an output from that history.
They also reset a pending change. These are explicit engineering thresholds, not
calibrated probabilities or measured universal accuracy claims; human code-switching
validation and tuning remain necessary.

Unit tests cover each direction, unsupported rejection, short false Spanish evidence,
independent speakers, oscillation prevention, genuine switches, finite input, history
capacity, unknown-speaker isolation and session reset. WAV integration checks the
actual detected language and resulting target direction for PT-BR, EN-US and EN-GB.
Translation worker/models are still NOT IMPLEMENTED.
