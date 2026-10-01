#!/usr/bin/python3
"""Measures the production C++ ASR backend. Development only; no runtime Python."""
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import statistics
import subprocess

root = Path(__file__).resolve().parent.parent
artifacts = root / 'artifacts/benchmark'
artifacts.mkdir(parents=True, exist_ok=True)
truth = {
    'ptbr.wav': ('pt', 'Vamos começar a reunião. Eu preciso de cinco minutos para organizar os documentos. Depois podemos conversar sobre o projeto.'),
    'enus.wav': ('en', 'Let us begin the meeting. I need five minutes to organize the documents. Then we can talk about the project.'),
    'engb.wav': ('en', 'I think we should wait. Please give me five minutes to check the documents before we start the meeting.')
}

def wer(expected, actual):
    a, b = re.findall(r'\w+', expected.lower()), re.findall(r'\w+', actual.lower())
    row = list(range(len(b) + 1))
    for i, word in enumerate(a, 1):
        current = [i]
        for j, other in enumerate(b, 1):
            current.append(min(current[-1] + 1, row[j] + 1, row[j - 1] + (word != other)))
        row = current
    return row[-1] / len(a)

def percentile(values, p):
    values = sorted(values)
    position = (len(values) - 1) * p
    low = int(position)
    return values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (position - low)

manifest = json.loads((root / 'models/manifest.json').read_text())
results = []
for model in manifest['models']:
    if model['type'] != 'asr': continue
    short = Path(model['filename']).stem
    output = artifacts / (short + '.jsonl')
    command = ['scripts/project-local.sh', str(root / 'build/asr-benchmark'), str(root / 'models' / model['filename']), model['sha256'], '10']
    command += [str(root / 'tests/data/generated' / name) for name in truth]
    with output.open('w') as stream, (artifacts / (short + '.stderr.log')).open('w') as errors:
        subprocess.run(command, cwd=root, stdout=stream, stderr=errors, check=True, timeout=600)
    rows = [json.loads(line) for line in output.read_text().splitlines()]
    if len(rows) != 30: raise SystemExit('Incomplete measurement set: ' + short)
    result = {
        'name': model['name'], 'filename': model['filename'], 'sha256': model['sha256'], 'samples': len(rows),
        'latencyP50Ms': percentile([r['asrLatencyMs'] for r in rows], 0.5),
        'latencyP95Ms': percentile([r['asrLatencyMs'] for r in rows], 0.95),
        'rtfP50': percentile([r['rtf'] for r in rows], 0.5),
        'maxPhysicalFootprint': max(r['physicalFootprint'] for r in rows),
        'peakRSS': max(r['peakRSS'] for r in rows), 'metalAllocatedBytes': max(r['metalAllocatedBytes'] for r in rows),
        'metal': all(r['metal'] for r in rows), 'maxThermalState': max(r['thermalState'] for r in rows),
        'syntheticWER': statistics.mean(wer(truth[Path(r['fixture']).name][1], r['sourceText']) for r in rows),
        'languageCorrect': all(r['language'] == truth[Path(r['fixture']).name][0] for r in rows)
    }
    results.append(result)
    print(json.dumps(result), flush=True)
eligible = [r for r in results if r['languageCorrect'] and r['rtfP50'] < 1 and max(r['peakRSS'], r['maxPhysicalFootprint']) + r['metalAllocatedBytes'] < 8 * 1024**3]
if not eligible: raise SystemExit('No candidate met measured real-time, language and conservative memory gates')
selected = min(eligible, key=lambda r: (r['syntheticWER'], r['latencyP50Ms']))
hardware = subprocess.check_output(['/usr/sbin/sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip()
timestamp = datetime.now(timezone.utc).isoformat()
summary = {'measuredAt': timestamp, 'hardware': hardware, 'decoder': 'beam search 5, temperature 0', 'results': results, 'selection': selected['filename']}
(artifacts / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(root / 'models/selection.json').write_text(json.dumps({'schemaVersion': 1, 'profile': 'Balanced', 'filename': selected['filename'], 'sha256': selected['sha256'], 'measuredAt': timestamp, 'hardware': hardware, 'basis': 'Lowest literal WER on three synthetic speech fixtures; measured latency breaks ties; RTF < 1 and conservative memory < 8 GiB required.'}, indent=2) + '\n')
lines = ['# Benchmarks', '', f'Measured {timestamp} on {hardware}.', '',
         'Production C++ WhisperASR backend: F16 models, four CPU threads, Metal, flash attention, beam search 5, temperature 0.',
         'Three locally synthesized fixtures: PT-BR Luciana, EN-US Samantha and EN-GB Daniel.',
         'One excluded warmup per fixture; 10 measured runs per fixture (30 samples per model).',
         'Latency includes mel extraction, language probabilities and transcription; excludes model loading.',
         'Repeated synthetic fixtures are not a representative conversation study.', '',
         '| Model | P50 ms | P95 ms | RTF P50 | Peak RSS MiB | Metal MiB | Literal WER | PT/EN correct |',
         '|---|---:|---:|---:|---:|---:|---:|---|']
for r in results:
    lines.append(f'| {r["name"]} | {r["latencyP50Ms"]:.2f} | {r["latencyP95Ms"]:.2f} | {r["rtfP50"]:.4f} | {r["peakRSS"]/1024**2:.1f} | {r["metalAllocatedBytes"]/1024**2:.1f} | {r["syntheticWER"]:.3f} | {r["languageCorrect"]} |')
lines += ['', f'Provisional Balanced selection: **{selected["name"]}**.',
          'Choose the lowest literal WER among language-correct models with measured RTF < 1 and',
          'conservative CPU footprint/RSS plus Metal allocations < 8 GiB. Measured P50 latency breaks ties.',
          'Digits versus spelled numbers count as literal word errors. No normalization hides differences.',
          'Re-evaluate on diverse real speakers and noise before claiming general accuracy.', '',
          'Maximum thermal states (0 nominal, 1 fair, 2 serious, 3 critical): ' + ', '.join(f'{r["name"]}={r["maxThermalState"]}' for r in results) + '.',
          'Raw measurements: `artifacts/benchmark/summary.json` and adjacent JSONL files.', '',
          'TTFT, E2E, translation/TTS latency, end-to-end app RAM and acoustic quality: **NOT MEASURED**.',
          'These measurements cover ASR only; no translated-audio pipeline exists yet.']
(root / 'BENCHMARKS.md').write_text('\n'.join(lines) + '\n')
print('Selected from measured candidates: ' + selected['name'], flush=True)
