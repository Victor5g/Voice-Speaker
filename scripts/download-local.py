#!/usr/bin/python3
"""Explicit development downloads only. Not bundled or called by the application."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

root = Path(__file__).resolve().parent.parent

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()

def download(url, target, expected):
    target = target.resolve()
    if root not in target.parents:
        raise SystemExit('ABORT: download outside project')
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and sha(target) == expected:
        print('Verified cached ' + target.name, flush=True)
        return
    temporary = target.with_suffix(target.suffix + '.part')
    if temporary.is_symlink(): raise SystemExit('ABORT: download symlink')
    subprocess.run(['/usr/bin/curl', '--fail', '--location', '--retry', '3', '--connect-timeout', '30', '--max-time', '1800', '--output', str(temporary), url], check=True)
    if sha(temporary) != expected: raise SystemExit('SHA-256 mismatch: ' + target.name)
    temporary.replace(target)
    print('Verified downloaded ' + target.name, flush=True)

cmake = root / 'tools/downloads/cmake-3.31.8-macos-universal.tar.gz'
download('https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-macos-universal.tar.gz', cmake,
         'd1449f969c54d5c00886d5b643340d493dfb3c81cb39ee29b35453395c11ebf7')
if not (cmake.parent / 'cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake').is_file():
    with tarfile.open(cmake) as archive:
        # Validate the exact pinned archive; including symlink destinations before extraction.
        for entry in archive.getmembers():
            destination = (cmake.parent / entry.name).resolve()
            if cmake.parent.resolve() not in destination.parents:
                raise SystemExit('ABORT: archive path escapes local tools')
            if entry.issym() or entry.islnk():
                link = (destination.parent / entry.linkname).resolve() if entry.issym() else (cmake.parent / entry.linkname).resolve()
                if cmake.parent.resolve() not in link.parents:
                    raise SystemExit('ABORT: archive link escapes local tools')
            elif not entry.isdir() and not entry.isfile():
                raise SystemExit('ABORT: archive special entry')
        archive.extractall(cmake.parent)
manifest = json.loads((root / 'models/manifest.json').read_text())
for model in manifest['models']:
    if model['type'] != 'asr': continue
    path = root / 'models' / model['filename']
    if (root / 'models').resolve() not in path.resolve().parents: raise SystemExit('ABORT: model path')
    download(model['source'], path, model['sha256'])
    if path.stat().st_size != model['expectedSize']: raise SystemExit('Model size mismatch')
print('Optional development tool and models ready; all files remain inside the project.')
