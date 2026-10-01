"""Summarize recorded native tests; does not run inference or change the app."""
import datetime
import hashlib
import json
import pathlib
import statistics

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/asr-private-state"


def records(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.startswith("{")]


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def distribution(values):
    result = {"n": len(values), "mean": statistics.mean(values)}
    if len(values) >= 30:
        result.update(p50=statistics.median(values),
                      p95=statistics.quantiles(values, n=100, method="inclusive")[94])
    return result


report = {
    "checkedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "scope": "Isolated warm ASR, not complete pipeline, semantic or acoustic acceptance",
    "decision": "Keep production arbitration serial; concurrency worsens PT median despite improving pair throughput",
    "productionConcurrentStatesEnabled": False,
    "percentiles": "Only n >= 30; inclusive linear interpolation; repeated recordings are not independent speakers",
    "memory": "Sampled process footprint, not an instrumented allocation peak or whole-app budget",
    "backends": {},
}
for backend in ("metal", "coreml-ane", "coreml-gpu", "coreml-all"):
    rows = records(OUT / (backend + ".jsonl"))
    assert rows[-1]["result"] == "PASS", backend
    summary = {"nativeResult": rows[-1], "modes": {}}
    for mode in ("serial-private", "concurrent-private"):
        samples = [r for r in rows if r.get("mode") == mode and "route" in r]
        summary["modes"][mode] = {
            "routes": {str(route): {
                key: distribution([r[key] for r in samples if r["route"] == route])
                for key in ("requestToResultMs", "asrMs", "modelWaitMs")
            } for route in (0, 1)},
            "pairMs": distribution([r["pairMs"] for r in samples if r["route"] == 0]),
            "thermalStates": sorted({r["thermalState"] for r in samples}),
            "resources": next(r for r in rows if r.get("mode") == mode and r.get("modeSummary")),
        }
    report["backends"][backend] = summary

baseline = records(ROOT / "artifacts/asr-state-recovery/control-quality.jsonl")
current = records(OUT / "single-state.jsonl")
# Compare inference rows only: configuration/setup rows are not measurements.
baseline = [r for r in baseline if "sourceText" in r]
current = [r for r in current if "sourceText" in r]
assert len(baseline) == len(current) == 60
for before, after in zip(baseline, current):
    for key in ("fixture", "iteration", "sourceText", "language"):
        assert before[key] == after[key], (key, before, after)
report["singleStateUnchangedComparisons"] = len(current)
paths = [
    "engine/asr/WhisperASR.hpp", "engine/asr/WhisperASR.cpp",
    "engine/asr/ASRWorker.cpp", "tests/cpp/ASRPrivateStateTests.mm",
    "scripts/build-asr-private-state-test.sh", "scripts/summarize-asr-private-state.py",
    "build/asr-private-state-tests", "build/asr-benchmark", "models/selection.json",
    "tests/data/human/manifest.json",
]
paths += [str(p.relative_to(ROOT)) for p in (ROOT / "build/whisper/lib").glob("*.a")]
paths += [str(p.relative_to(ROOT)) for p in OUT.glob("*.jsonl")]
report["sha256"] = {p: sha(ROOT / p) for p in paths}
(OUT / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
print("PASS: 60 unchanged single-state outputs; 96 private-state comparisons; summary written")
