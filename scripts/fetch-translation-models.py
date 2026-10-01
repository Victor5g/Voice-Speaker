#!/usr/bin/python3
"""Fetch exact development source snapshots, verifying upstream LFS/blob identities."""
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

root = Path(__file__).resolve().parent.parent
models = [
    ('pt-en', 'Helsinki-NLP/opus-mt-ROMANCE-en', 'e9ca9975e3972afd80732f08ce01d3a1339f47f8'),
    ('en-pt', 'Helsinki-NLP/opus-mt-tc-big-en-pt', '9f2863d807ecf91a374bdbecb8d01e402e90622e'),
]
allowed = {'README.md', 'config.json', 'generation_config.json', 'source.spm', 'target.spm',
           'vocab.json', 'tokenizer_config.json', 'special_tokens_map.json'}
for direction, repo, revision in models:
    url = f'https://huggingface.co/api/models/{repo}/revision/{revision}?blobs=true'
    with urllib.request.urlopen(url, timeout=30) as response:
        metadata = json.load(response)
    if metadata['sha'] != revision: raise SystemExit('Unexpected model revision')
    siblings = metadata['siblings']
    weights = 'model.safetensors' if any(f['rfilename'] == 'model.safetensors' for f in siblings) else 'pytorch_model.bin'
    directory = root / 'cache/model-sources' / direction
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for info in siblings:
        name = info['rfilename']
        if name not in allowed and name != weights: continue
        target = directory / name
        source = f'https://huggingface.co/{repo}/resolve/{revision}/{name}'
        def verified(path):
            if not path.is_file() or path.stat().st_size != info['size']: return False
            sha = hashlib.sha256() if info.get('lfs') else hashlib.sha1()
            if not info.get('lfs'): sha.update(b'blob ' + str(info['size']).encode() + b'\0')
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1048576), b''): sha.update(block)
            return sha.hexdigest() == (info['lfs']['sha256'] if info.get('lfs') else info['blobId'])
        if not verified(target):
            part = target.with_suffix(target.suffix + '.part')
            if part.is_symlink() or target.is_symlink(): raise SystemExit('Unsafe model destination')
            subprocess.run(['/usr/bin/curl', '--fail', '--location', '--retry', '3', '--max-time', '1200', '--output', str(part), source], check=True)
            if not verified(part): raise SystemExit('Source model identity mismatch: ' + name)
            part.replace(target)
        digest = hashlib.sha256()
        with target.open('rb') as stream:
            for block in iter(lambda: stream.read(1048576), b''): digest.update(block)
        records.append({'filename': name, 'sha256': digest.hexdigest(), 'expectedSize': info['size'], 'source': source})
        print('Verified source: ' + direction + '/' + name, flush=True)
    (directory / 'source-manifest.json').write_text(json.dumps({'repository': repo, 'revision': revision,
        'license': metadata.get('cardData', {}).get('license'), 'files': records}, indent=2) + '\n')
