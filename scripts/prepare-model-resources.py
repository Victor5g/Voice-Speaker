#!/usr/bin/python3
"""Package selected native models and all verified auxiliary resources."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parent.parent
catalog = json.loads((root / 'models/manifest.json').read_text())
selection = json.loads((root / 'models/selection.json').read_text())
translation = json.loads((root / 'models/translation-selection.json').read_text())
asr_profiles = selection.get('profiles', [selection])
entries = [m for m in catalog['models'] if
    (m['type'] == 'asr' and any(m['filename'] == p['filename'] and m['sha256'] == p['sha256'] for p in asr_profiles)) or
    (m['type'] == 'asr-encoder' and any(m['asrSHA256'] == p['sha256'] for p in asr_profiles)) or
    (m['type'] == 'translation' and m['quantization'] == translation[m['direction']]) or m['type'] in ('tts', 'embedding', 'overlap', 'separation')]
if len([m for m in entries if m['type']=='asr']) != len(asr_profiles) or len([m for m in entries if m['type']=='translation']) != 2 or len([m for m in entries if m['type']=='tts']) != 6:
    raise SystemExit('ABORT: incomplete conversation model set')
destination_root = (root / 'build/ModelResources').resolve()
if root / 'build' not in destination_root.parents: raise SystemExit('Unsafe resource folder')
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''): h.update(block)
    return h.hexdigest()
expected = set()
for entry in entries:
    for asset in entry.get('files', [entry]):
        source = (root / 'models' / asset['filename']).resolve()
        destination = (destination_root / asset['filename']).resolve()
        if root / 'models' not in source.parents or destination_root not in destination.parents:
            raise SystemExit('ABORT: model asset path escapes project')
        if source.stat().st_size != asset['expectedSize'] or digest(source) != asset['sha256']:
            raise SystemExit('ABORT: model asset integrity failed: ' + asset['filename'])
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or digest(destination) != asset['sha256']: shutil.copyfile(source, destination)
        expected.add(destination)
    print('Prepared verified bundle: ' + entry['name'])
for old in destination_root.rglob('*'):
    if old.is_file() and old.name not in ('manifest.json', 'selection.json', 'translation-selection.json') and old.resolve() not in expected:
        if destination_root not in old.resolve().parents: raise SystemExit('Unsafe stale resource')
        old.unlink()
(destination_root / 'manifest.json').write_text(json.dumps({'schemaVersion': 1, 'models': entries}, indent=2) + '\n')
(destination_root / 'selection.json').write_text(json.dumps(selection, indent=2) + '\n')
(destination_root / 'translation-selection.json').write_text(json.dumps(translation, indent=2) + '\n')
