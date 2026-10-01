#!/usr/bin/python3
"""Exercise the real capture-to-PCM pipeline for each ASR backend using WAVs."""
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
output = root / 'artifacts/coreml-comparison/pipeline'
output.mkdir(parents=True, exist_ok=True)
configs = [(mode, {'asrBackend': mode}) for mode in ('metal', 'coreml-ane', 'coreml-all', 'coreml-gpu')]
configs.append(('noise-metal', {'asrBackend': 'metal', 'fixture': 'tests/data/scenarios/speech-noise.wav'}))
results = []
for name, config in configs:
    config['output'] = str(output / (name + '.wav'))
    config['fixtureType'] = 'Synthetic WAV; native production pipeline; not physical conversation acceptance'
    path = root / 'build' / ('coreml-pipeline-' + name + '.json')
    path.write_text(json.dumps(config, indent=2) + '\n')
    print('Pipeline ' + name, flush=True)
    with (output / (name + '.jsonl')).open('w') as out, (output / (name + '.stderr.log')).open('w') as err:
        status = subprocess.run([str(root / 'scripts/project-local.sh'), str(root / 'build/coreml-conversation-golden'),
                                 str(root / 'models'), str(path)], stdout=out, stderr=err, timeout=240)
    rows = [json.loads(line) for line in (output / (name + '.jsonl')).read_text().splitlines() if line.startswith('{')]
    final = next((r for r in reversed(rows) if r.get('result') == 'PASS'), None)
    result = {'scenario': name, 'exitCode': status.returncode, 'summary': final}
    results.append(result)
    (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    print(name + ': ' + str(status.returncode), flush=True)
if any(r['exitCode'] for r in results): raise SystemExit(1)
