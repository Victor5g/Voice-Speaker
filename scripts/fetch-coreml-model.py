#!/usr/bin/python3
"""Development-only restore of the pinned encoder; never invoked by the app."""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

root = Path(__file__).resolve().parent.parent
base = root / 'models'
entry = next(e for e in json.loads((base / 'manifest.json').read_text())['models'] if e['type'] == 'asr-encoder')
archive = root / 'cache/ggml-small-encoder.mlmodelc.zip'
def digest(path):
    with path.open('rb') as stream:
        value = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1048576), b''): value.update(chunk)
    return value.hexdigest()
if not archive.is_file() or digest(archive) != entry['archiveSHA256']:
    temporary = archive.with_suffix('.zip.part')
    if temporary.is_symlink() or archive.is_symlink(): raise SystemExit('Unsafe archive path')
    subprocess.run(['/usr/bin/curl', '--fail', '--location', '--retry', '3', '--max-time', '600',
                    '--output', str(temporary), entry['source']], check=True)
    if temporary.stat().st_size != entry['archiveSize'] or digest(temporary) != entry['archiveSHA256']:
        raise SystemExit('Core ML archive integrity failed')
    temporary.replace(archive)
expected = {e['filename']: e for e in entry['files']}
seen = set()
with zipfile.ZipFile(archive) as bundle:
    for member in bundle.infolist():
        relative = 'optional/' + member.filename
        target = base / relative
        if base not in target.resolve().parents or target.is_symlink() or (member.external_attr >> 16) & 0o170000 == 0o120000:
            raise SystemExit('Unsafe Core ML archive entry')
        if member.is_dir(): continue
        if relative not in expected or relative in seen: raise SystemExit('Unexpected Core ML asset')
        seen.add(relative)
        data = bundle.read(member)
        asset = expected[relative]
        if len(data) != asset['expectedSize'] or hashlib.sha256(data).hexdigest() != asset['sha256']:
            raise SystemExit('Core ML asset integrity failed')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
if seen != expected.keys(): raise SystemExit('Incomplete encoder archive')
print('PASS pinned Core ML encoder restored inside project')
