#!/usr/bin/env python3
"""Real production pipeline, synthetic acoustic fixtures; no physical AEC claim."""
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
out = root / 'artifacts/realtime-improvements'
out.mkdir(parents=True, exist_ok=True)
results = []
for name in ('silence', 'noise-only', 'instrumental-tone', 'digital-echo'):
    source = 'echo-config.json' if name == 'digital-echo' else 'scenario-' + name + '.json'
    for manual in (False, True):
        config = json.loads((root / 'build' / source).read_text())
        label = name + ('-manual' if manual else '-auto')
        config['output'] = str(out / (label + '.wav'))
        if manual:
            config['processing'] = {'sourceLanguage': 'pt' if name == 'digital-echo' else 'en',
                                    'targetLanguage': 'en' if name == 'digital-echo' else 'pt',
                                    'identifySpeakers': False}
        path = root / 'build' / ('realtime-' + label + '.json')
        path.write_text(json.dumps(config, indent=2) + '\n')
        with (out / (label + '.jsonl')).open('w') as stdout, (out / (label + '.err')).open('w') as stderr:
            run = subprocess.run(['scripts/project-local.sh', 'build/realtime-conversation-golden', 'models', str(path)],
                                 cwd=root, stdout=stdout, stderr=stderr)
        result = {'case': label, 'exitCode': run.returncode}
        results.append(result)
        print(json.dumps(result), flush=True)
(out / 'audio-protection-results.json').write_text(json.dumps(results, indent=2) + '\n')
raise SystemExit(int(any(result['exitCode'] for result in results)))
