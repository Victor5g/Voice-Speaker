#!/usr/bin/python3
"""Matched native ASR comparison. Does not silently change the release selection."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--smoke', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
output = root / 'artifacts/coreml-comparison'
output.mkdir(parents=True, exist_ok=True)
selection = json.loads((root / 'models/selection.json').read_text())
recordings = json.loads((root / 'tests/data/human/manifest.json').read_text())['recordings']
if args.smoke:
    recordings = [next(r for r in recordings if r['language'] == lang) for lang in ('pt_br', 'en_us')]
repeats = 1 if args.smoke else 5
def percentile(values, fraction):
    if len(values) < 30: return None
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    low = int(index)
    return ordered[low] + (ordered[min(low + 1, len(ordered) - 1)] - ordered[low]) * (index - low)
def wer(reference, text):
    a, b = re.findall(r'\w+', reference.lower()), re.findall(r'\w+', text.lower())
    row = list(range(len(b) + 1))
    for i, word in enumerate(a, 1):
        current = [i]
        for j, other in enumerate(b, 1):
            current.append(min(current[-1] + 1, row[j] + 1, row[j-1] + (word != other)))
        row = current
    return row[-1] / len(a)
truth = {str(root / r['filename']): r for r in recordings}
for path, recording in truth.items():
    if hashlib.sha256(Path(path).read_bytes()).hexdigest() != recording['sha256']:
        raise SystemExit('Human fixture hash mismatch')
summary = {'measuredAt': datetime.now(timezone.utc).isoformat(), 'smoke': args.smoke,
           'benchmarkBinarySHA256': hashlib.sha256((root / 'build/asr-benchmark').read_bytes()).hexdigest(),
           'hardware': subprocess.check_output(['/usr/sbin/sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
           'modelSHA256': selection['sha256'], 'repeatsPerRecording': repeats,
           'scope': 'Warm isolated production ASR, repeated 12 human recordings; not live E2E or proof of ANE placement.',
           'results': []}
for backend in ('metal', 'coreml-ane', 'coreml-all', 'coreml-gpu'):
    stem = ('smoke-' if args.smoke else '') + backend
    command = [str(root / 'scripts/project-local.sh'), str(root / 'build/asr-benchmark'),
               '--backend', backend, '--resources', str(root / 'models'),
               str(root / 'models' / selection['filename']), selection['sha256'], str(repeats), *truth]
    print('Measuring ' + backend, flush=True)
    with (output / (stem + '.jsonl')).open('w') as out, (output / (stem + '.stderr.log')).open('w') as err:
        completed = subprocess.run(command, stdout=out, stderr=err, timeout=1200)
    result = {'backend': backend, 'exitCode': completed.returncode}
    if completed.returncode == 0:
        rows = [json.loads(line) for line in (output / (stem + '.jsonl')).read_text().splitlines()]
        if len(rows) != len(truth) * repeats: raise SystemExit('Incomplete sample set')
        result.update({'samples': len(rows), 'maxFootprintBytes': max(r['physicalFootprint'] for r in rows),
                       'loadMs': rows[0]['modelLoadMs'], 'firstWarmupMs': rows[0]['warmupMs'],
                       'metalActive': all(r['metal'] for r in rows), 'maxThermalState': max(r['thermalState'] for r in rows),
                       'languageCorrect': all(r['language'] == truth[r['fixture']]['language'][:2] for r in rows),
                       'languages': {}})
        for language in ('pt_br', 'en_us'):
            subset = [r for r in rows if truth[r['fixture']]['language'] == language]
            result['languages'][language] = {'samples': len(subset),
                'p50Ms': percentile([r['asrLatencyMs'] for r in subset], .5),
                'p95Ms': percentile([r['asrLatencyMs'] for r in subset], .95),
                'meanMs': statistics.mean(r['asrLatencyMs'] for r in subset),
                'meanWER': statistics.mean(wer(truth[r['fixture']]['reference'], r['sourceText']) for r in subset)}
    summary['results'].append(result)
    (output / ('smoke-summary.json' if args.smoke else 'summary.json')).write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(result), flush=True)
if any(r['exitCode'] for r in summary['results']): raise SystemExit(1)
