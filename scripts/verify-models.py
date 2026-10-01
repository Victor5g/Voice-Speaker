#!/usr/bin/python3
"""Verify every model and auxiliary asset, including the selected personal release set."""
import argparse
import hashlib
import json
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent / 'models')
parser.add_argument('--release', action='store_true')
args = parser.parse_args()
base = args.root.resolve()
models = json.loads((base / 'manifest.json').read_text())['models']
required = {'name', 'type', 'version', 'filename', 'sha256', 'license', 'source', 'expectedSize', 'backend'}
verified = {}
for model in models:
    missing = required - model.keys()
    if missing: raise SystemExit('FAIL missing fields: ' + ', '.join(sorted(missing)))
    assets = model.get('files', [model])
    if not any(f['filename'] == model['filename'] and f['sha256'] == model['sha256'] and f['expectedSize'] == model['expectedSize'] for f in assets):
        raise SystemExit('FAIL primary model is not verified by its asset list')
    seen = set()
    for asset in assets:
        relative = asset['filename']
        path = (base / relative).resolve()
        identity = (asset['sha256'], asset['expectedSize'])
        if base not in path.parents or relative in seen: raise SystemExit('FAIL unsafe or duplicate asset: ' + relative)
        seen.add(relative)
        if relative in verified:
            if verified[relative] != identity: raise SystemExit('FAIL conflicting asset identities')
            continue
        if not path.is_file() or path.stat().st_size != asset['expectedSize']: raise SystemExit('FAIL missing or invalid asset: ' + relative)
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1048576), b''): digest.update(block)
        if digest.hexdigest() != asset['sha256']: raise SystemExit('FAIL SHA-256: ' + relative)
        verified[relative] = identity
    print('PASS model and auxiliaries: ' + model['name'])
if args.release:
    selection = json.loads((base / 'selection.json').read_text())
    translation = json.loads((base / 'translation-selection.json').read_text())
    if not any(m['type'] == 'asr' and m['filename'] == selection['filename'] and m['sha256'] == selection['sha256'] for m in models): raise SystemExit('FAIL selected ASR model missing')
    for profile in selection.get('profiles', []):
        if not any(m['type'] == 'asr' and m['filename'] == profile['filename'] and m['sha256'] == profile['sha256'] for m in models): raise SystemExit('FAIL approved ASR profile missing')
    for direction in ('pt-en', 'en-pt'):
        if not any(m['type'] == 'translation' and m['direction'] == direction and m['quantization'] == translation[direction] for m in models): raise SystemExit('FAIL selected translation model missing: ' + direction)
    voices = [m for m in models if m['type'] == 'tts']
    for voice in ('pt_BR-faber', 'pt_BR-jeff', 'pt_BR-edresson', 'en_US-lessac', 'en_US-ryan', 'en_US-amy'):
        if not any(voice in m['filename'] for m in voices): raise SystemExit('FAIL missing voice: ' + voice)
    for kind in ('embedding', 'overlap', 'separation'):
        if len([m for m in models if m['type'] == kind]) != 1: raise SystemExit('FAIL missing or ambiguous model: ' + kind)
print('PASS verified %d models / %d unique files; functional acceptance is checked separately.' % (len(models), len(verified)))
