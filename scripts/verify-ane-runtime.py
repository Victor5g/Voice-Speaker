#!/usr/bin/python3
"""Development-only Instruments evidence. Never equate a compute plan with execution."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=root / 'artifacts/ane-runtime')
parser.add_argument('--analyze-only', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
if root not in out.parents:
    raise SystemExit('Output must stay inside the project')
out.mkdir(parents=True, exist_ok=True)
selection = json.loads((root / 'models/selection.json').read_text())
fixture = json.loads((root / 'tests/data/human/manifest.json').read_text())['recordings'][0]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
if sha(root / fixture['filename']) != fixture['sha256']:
    raise SystemExit('Fixture hash mismatch')
def run(command, **kwargs):
    subprocess.run([str(root / 'scripts/project-local.sh'), *map(str, command)],
                   cwd=root, check=True, timeout=240, **kwargs)

if not args.analyze_only:
    subprocess.run(['bash', 'scripts/build-coreml-compute-plan.sh'], cwd=root, check=True)
    for stem, backend in [('ane', 'coreml-ane'), ('gpu', 'coreml-gpu')]:
        trace = out / (stem + '-runtime.trace')
        if trace.exists():
            raise SystemExit('Trace already exists; use a new output directory')
        command = [root / 'build/asr-benchmark', '--backend', backend, '--resources', root / 'models',
                   root / 'models' / selection['filename'], selection['sha256'], '20', root / fixture['filename']]
        run(['/usr/bin/xcrun', 'xctrace', 'record', '--template', 'Core AI',
             '--instrument', 'Core ML', '--instrument', 'Neural Engine', '--output', trace,
             '--time-limit', '30s', '--no-prompt', '--target-stdout', out / (stem + '-runtime.stdout.log'),
             '--launch', '--', *command])
        run(['/usr/bin/xcrun', 'xctrace', 'export', '--input', trace, '--toc',
             '--output', out / (stem + '-runtime-toc.xml')])
        run(['/usr/bin/xcrun', 'xctrace', 'export', '--input', trace, '--xpath',
             '/trace-toc/run[@number="1"]/data/table[@schema="ane-hw-intervals" or @schema="coreml-os-signpost"]',
             '--output', out / (stem + '-runtime-events.xml')])
        with (out / (stem + '-compute-plan.json')).open('w') as output:
            run([root / 'build/coreml-compute-plan', root / 'models', root / 'models' / selection['filename'],
                 selection['sha256'], backend], stdout=output)

summary = {'analyzedAt': datetime.now(timezone.utc).isoformat(),
           'scope': 'Production WhisperASR in native benchmark; one human WAV, warmup + 20 predictions. Not live GUI/call acceptance.',
           'attribution': 'ANE hardware table has no PID column. Attribution uses encoder-specific event label, scoped launched target, 21 predictions and CPU/GPU negative control.',
           'timingCaveat': 'Traced intervals include instrumentation; not comparable to untraced latency benchmarks. Fewer than 30 samples: no P50/P95.',
           'modelSHA256': selection['sha256'], 'fixtureSHA256': fixture['sha256'],
           'binarySHA256AtAnalysis': sha(root / 'build/asr-benchmark'), 'results': []}
for stem, backend in [('ane', 'coreml-ane'), ('gpu', 'coreml-gpu')]:
    toc = ET.parse(out / (stem + '-runtime-toc.xml')).getroot()
    target = toc.find('./run/info/target/process')
    assert target is not None and target.get('return-exit-status') == '0', 'Benchmark failed'
    assert '--backend ' + backend in target.get('arguments', ''), 'Wrong trace backend'
    logs = [json.loads(line) for line in (out / (stem + '-runtime.stdout.log')).read_text().splitlines()]
    assert len(logs) == 20 and all(row['backend'] == (1 if stem == 'ane' else 3) for row in logs), 'Incomplete predictions'
    assert all(Path(row['fixture']).resolve() == root / fixture['filename'] and
               Path(row['model']).resolve() == root / 'models' / selection['filename'] for row in logs), 'Wrong input'
    tree = ET.parse(out / (stem + '-runtime-events.xml')).getroot()
    refs = {element.get('id'): element for element in tree.iter() if element.get('id')}
    def resolve(element):
        while element.get('ref'):
            element = refs[element.get('ref')]
        return element
    events = []
    for node in tree.findall('node'):
        schema = node.find('schema')
        if schema is None or schema.get('name') != 'ane-hw-intervals':
            continue
        columns = [col.findtext('mnemonic') for col in schema.findall('col')]
        for row in node.findall('row'):
            cells = {key: resolve(cell) for key, cell in zip(columns, row)}
            label = cells['event-label'].get('fmt', '')
            if 'ggml-small-encoder_main__Op0_AneInference' not in label:
                continue
            events.append({'label': label, 'startNs': int(cells['start'].text),
                           'durationNs': int(cells['duration'].text), 'state': cells['state'].get('fmt')})
    predictions = [event for event in events if event['label'].endswith(' Prediction') and event['state'] == 'Active']
    assert len(predictions) == (21 if stem == 'ane' else 0), 'Hardware evidence differs from expected control'
    plan = json.loads((out / (stem + '-compute-plan.json')).read_text())
    assert plan['backend'] == backend and plan['modelSHA256'] == selection['sha256']
    summary['results'].append({'backend': backend, 'targetPID': target.get('pid'),
        'recordedAt': toc.findtext('./run/info/summary/start-date'), 'measuredTranscriptions': len(logs),
        'maxThermalState': max(row['thermalState'] for row in logs),
        'anePredictionEvents': len(predictions), 'aneLoadEvents': len(events) - len(predictions),
        'anePredictionTotalMs': sum(event['durationNs'] for event in predictions) / 1e6,
        'anticipatedPreferredDeviceCounts': plan['preferredDeviceCounts'],
        'anticipatedANEOperators': dict(Counter(op['operator'] for op in plan['operations'] if op['preferredDevice'] == 'ane')),
        'events': events,
        'artifactSHA256': {suffix: sha(out / (stem + suffix)) for suffix in
            ['-runtime-events.xml', '-runtime-toc.xml', '-runtime.stdout.log', '-compute-plan.json']}})
summary['verdict'] = 'CONFIRMED: encoder executed on ANE in CPU/ANE run; zero matching ANE events in CPU/GPU control. Per-operation mapping remains anticipated, not separately traced.'
(out / 'ane-verification.json').write_text(json.dumps(summary, indent=2) + '\n')
print(summary['verdict'])
