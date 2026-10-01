#!/usr/bin/python3
"""Development-only, serial matched PCM comparison. Does not promote a policy."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--threshold', type=float, default=.5)
parser.add_argument('--repeats', type=int, default=3)
parser.add_argument('--current-boundary', action='store_true')
parser.add_argument('--allow-comma-to-period', action='store_true')
args = parser.parse_args()
if not 0 < args.threshold <= 1 or not 1 <= args.repeats <= 10:
    raise SystemExit('Invalid threshold/repeats')
if args.allow_comma_to_period and not args.current_boundary:
    raise SystemExit('Comma-to-period requires current-boundary evidence')
out = args.output.resolve()
if root not in out.parents or out.exists(): raise SystemExit('Use a new output directory inside the project')
out.mkdir(parents=True)
binary = root / 'build/boundary-conversation-golden'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
policy = dict(partialIntervalMs=750, agreements=2, stableAudioMs=600, adaptive=True, trimSpeakerEdges=True)
summary = {'measuredAt': datetime.now(timezone.utc).isoformat(), 'threshold': args.threshold, 'consecutiveBoundaryEvidence': not args.current_boundary,
           'allowCommaToPeriodBoundary': args.allow_comma_to_period,
           'binarySHA256': sha(binary), 'sourceSHA256': {str(p.relative_to(root)): sha(p) for folder in ['engine', 'tests/cpp']
                for p in sorted((root / folder).rglob('*')) if p.is_file()},
           'scope': 'Synthetic development fixtures. Same WAV and callbacks; separate cold loads. Not human acceptance or acoustic end-of-speech latency.',
           'runs': []}
for repeat in range(args.repeats):
    for scenario, fixture in [('noise','tests/data/scenarios/speech-noise.wav'), ('clean','tests/data/conversation_ptbr_en.wav')]:
        for candidate in ([False, True] if repeat % 2 == 0 else [True, False]):
            name = f'{scenario}-{"candidate" if candidate else "control"}-{repeat+1}'
            config = {'fixture': fixture, 'output': str(out / (name+'.wav')),
                      'requiredOriginalTerms': {'pt':['reunião','documentos','conversar','projeto'], 'en':['meeting','documents','project']},
                      'streaming': {**policy, 'minimumBoundaryProbability': args.threshold if candidate else 0,
                                    'consecutiveBoundaryEvidence': not args.current_boundary,
                                    'allowCommaToPeriodBoundary': candidate and args.allow_comma_to_period}}
            configPath = out / (name+'.json'); configPath.write_text(json.dumps(config,indent=2)+'\n')
            with (out/(name+'.jsonl')).open('w') as log, (out/(name+'.err')).open('w') as error:
                completed = subprocess.run([str(root/'scripts/project-local.sh'), str(binary), str(root/'models'), str(configPath)],
                                           cwd=root,stdout=log,stderr=error,timeout=240)
            rows = [json.loads(line) for line in (out/(name+'.jsonl')).read_text().splitlines() if line.startswith('{')]
            first = {}
            for row in rows:
                if row.get('state') == 'audio-onset':
                    first.setdefault(str(row['sourceSegment']), {'captureToFirstSoundMs':row['captureToPlaybackMs'],
                        'afterVadEndEstimatedMs': row['timing'].get('afterVadEndEstimatedMs')})
            result = {'name': name, 'candidate': candidate, 'fixtureSHA256': sha(root/fixture), 'exitCode': completed.returncode,
                      'firstAudioBySource': first, 'confirmedTexts': [r['original'] for r in rows if r.get('state') == 'recognized'],
                      'stability': next((r for r in rows if r.get('stabilityValidation')),None),
                      'summary': next((r for r in reversed(rows) if r.get('result')),None),
                      'error': (out/(name+'.err')).read_text()}
            summary['runs'].append(result)
            (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
            print(name,completed.returncode,result['stability'],flush=True)
if any(r['candidate'] and r['exitCode'] for r in summary['runs']): raise SystemExit(1)
