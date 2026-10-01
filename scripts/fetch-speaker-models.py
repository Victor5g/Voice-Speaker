#!/usr/bin/python3
"""Restore pinned native speaker/overlap/separation resources during development."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

root = Path(__file__).resolve().parent.parent
base = root / 'models'
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''): h.update(block)
    return h.hexdigest()
def download(pin, destination):
    if root not in destination.resolve().parents: raise RuntimeError('Unsafe model path')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and destination.stat().st_size == pin['expectedSize'] and digest(destination) == pin['sha256']: return
    part = destination.with_suffix(destination.suffix + '.part')
    if part.is_symlink(): raise RuntimeError('Unsafe temporary path')
    subprocess.run(['/usr/bin/curl', '--fail', '--location', '--retry', '3', '--max-time', '1200', '--output', str(part), pin['source']], check=True)
    if part.stat().st_size != pin['expectedSize'] or digest(part) != pin['sha256']: raise RuntimeError('Model download integrity failed')
    part.replace(destination)
for pin in json.loads((base / 'speaker-downloads.lock.json').read_text()):
    download(pin, base / 'optional/speakers' / pin['filename'])
overlap = json.loads((base / 'overlap-download.lock.json').read_text())
archive = root / 'tools/downloads' / overlap['filename']
download(overlap, archive)
entries = json.loads((base / 'speaker-assets.json').read_text())
for model in entries:
    if model['type'] == 'overlap': continue
    for asset in model.get('files', []):
        if asset['filename'] != model['filename']: download(asset, base / asset['filename'])
entry = next(m for m in entries if m['type'] == 'overlap')
with tarfile.open(archive) as stream:
    for asset in entry['files']:
        name = asset['filename'].removeprefix('optional/speakers/')
        member = stream.getmember(name)
        if not member.isfile() or member.size != asset['expectedSize']: raise RuntimeError('Unsafe archive member')
        destination = base / asset['filename']
        if base.resolve() not in destination.resolve().parents: raise RuntimeError('Unsafe overlap destination')
        destination.parent.mkdir(parents=True, exist_ok=True)
        data = stream.extractfile(member).read()
        if hashlib.sha256(data).hexdigest() != asset['sha256']: raise RuntimeError('Overlap asset hash mismatch')
        destination.write_bytes(data)
catalog = json.loads((base / 'manifest.json').read_text())
catalog['models'] = [m for m in catalog['models'] if m['type'] not in ('embedding', 'embedding-reference', 'overlap', 'separation')] + entries
(base / 'manifest.json').write_text(json.dumps(catalog, indent=2) + '\n')
print('PASS restored verified speaker, overlap and separation models')
