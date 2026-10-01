#!/usr/bin/env python3
"""Describe the isolated native duplex arbitration check without inventing percentiles."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/asr-arbitration'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_run(name):
    path = OUT / f'{name}.jsonl'
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.startswith('{')]
    assert rows[-1]['result'] == 'PASS', f'{name} did not finish successfully'
    waits = [{}, {}]
    for row in rows:
        timing = row.get('timing', {})
        started = timing.get('inferenceStarted', 0)
        waited = timing.get('asrModelWaitMs')
        if started and waited is not None:
            waits[row['route']][started] = waited
    return {
        'policy': name,
        'binarySHA256': sha(ROOT / f'build/asr-arbitration/{name}'),
        'traceSHA256': sha(path),
        'summary': rows[-1],
        'modelWaitsOfPublishedResultsMs': [list(route.values()) for route in waits],
        'scope': 'Published results only; cancelled requests are absent from this event trace. '
                 'Engine aggregate wait metrics include cancelled admissions in the candidate. '
                 'No acoustic or physical latency acceptance; insufficient samples for percentiles.',
    }


if __name__ == '__main__':
    report = {'runs': [read_run('control'), read_run('candidate')],
              'baselineCheckpoint': json.loads((ROOT / 'artifacts/latency-policy/checkpoint-acceptance.json').read_text()),
              'conclusion': 'Functional duplex regression passed; no latency target or complete translation acceptance established.'}
    (OUT / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    for run in report['runs']:
        print(run['policy'], 'PASS', 'waits of published results', run['modelWaitsOfPublishedResultsMs'])
