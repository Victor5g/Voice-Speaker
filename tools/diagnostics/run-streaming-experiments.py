#!/usr/bin/python3
"""Sequential candidate comparisons using the native production pipeline."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
policies = {
    'control': dict(partialIntervalMs=1500, agreements=3, stableAudioMs=0, adaptive=False),
    'cadence': dict(partialIntervalMs=1000, agreements=3, stableAudioMs=0, adaptive=True),
    'temporal': dict(partialIntervalMs=1500, agreements=2, stableAudioMs=900, adaptive=False),
    'combined': dict(partialIntervalMs=1000, agreements=2, stableAudioMs=900, adaptive=True),
    'fast': dict(partialIntervalMs=750, agreements=2, stableAudioMs=600, adaptive=True),
    'fast-trim': dict(partialIntervalMs=750, agreements=2, stableAudioMs=600, adaptive=True, trimSpeakerEdges=True),
}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', default='build/latency-policy/conversation')
parser.add_argument('--policies', nargs='+', choices=policies, default=list(policies))
parser.add_argument('--scenarios', nargs='+', default=['conversation', 'speech-noise'])
parser.add_argument('--tag', default='')
args = parser.parse_args()
if args.tag and (Path(args.tag).name != args.tag or args.tag in ('.','..')):
    raise SystemExit('Experiment tag must be one directory name')
artifact = root/'artifacts/latency-policy'/args.tag
build = root/'build/latency-policy'
artifact.mkdir(parents=True, exist_ok=True); build.mkdir(exist_ok=True)
engine_hash = hashlib.sha256()
for p in sorted(p for folder in ('engine', 'bridge') for p in (root/folder).rglob('*') if p.is_file()):
    engine_hash.update(str(p.relative_to(root)).encode()); engine_hash.update(p.read_bytes())
provenance = dict(engineFingerprint=engine_hash.hexdigest(), binary=args.binary,
                  binarySHA256=hashlib.sha256((root/args.binary).read_bytes()).hexdigest(),
                  scope='Sequential WAV experiments; not human or physical latency acceptance')
results = []
for scenario in args.scenarios:
    if scenario == 'conversation':
        config = {}
    elif scenario == 'human':
        config = json.loads((root/'build/human-golden-config.json').read_text())
    else:
        config = json.loads((root/f'build/scenario-{scenario}.json').read_text())
    for name in args.policies:
        outdir = artifact/name; outdir.mkdir(exist_ok=True)
        config['streaming'] = policies[name]
        config['output'] = str((outdir/f'{scenario}.wav').relative_to(root))
        run_config = build/f'{name}-{scenario}.json'
        run_config.write_text(json.dumps(config, indent=2)+'\n')
        trace, error = outdir/f'{scenario}.jsonl', outdir/f'{scenario}.err'
        with trace.open('w') as out, error.open('w') as err:
            completed = subprocess.run(['scripts/project-local.sh', args.binary, 'models', str(run_config.relative_to(root))],
                                       cwd=root, stdout=out, stderr=err)
        rows = [json.loads(line) for line in trace.read_text().splitlines() if line.startswith('{')]
        content = {}
        for row in rows:
            if row.get('state') == 'translated':
                language = content.setdefault(row['language'], dict(original=[], translated=[]))
                language['original'].append(row['original']); language['translated'].append(row['translation'])
        seen = set(); first_onsets = []
        for row in rows:
            if row.get('state') == 'audio-onset' and row['sourceSegment'] not in seen:
                first_onsets.append(row.get('captureToPlaybackMs')); seen.add(row['sourceSegment'])
        result = dict(scenario=scenario, policy=name, settings=policies[name], exitCode=completed.returncode,
                      summary=next((r for r in reversed(rows) if 'result' in r), None), content=content,
                      firstOnsetsMs=first_onsets, errors=[line for line in error.read_text().splitlines() if line.startswith('FAIL ')])
        results.append(result)
        # Individual records persist even if a later experiment is interrupted.
        (outdir/f'{scenario}-result.json').write_text(json.dumps(dict(provenance=provenance, **result), indent=2, ensure_ascii=False)+'\n')
        print(json.dumps({k: result[k] for k in ('scenario', 'policy', 'exitCode', 'firstOnsetsMs', 'errors')}), flush=True)
(artifact/'experiment-results.json').write_text(json.dumps(dict(provenance=provenance, runs=results), indent=2, ensure_ascii=False)+'\n')
