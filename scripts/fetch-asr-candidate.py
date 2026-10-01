#!/usr/bin/python3
"""Explicit, isolated development acquisition. Never called by the application."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def local_path(root, relative):
    """Reject links, absolute paths and traversal before reading or writing."""
    relative = str(relative)
    parts = PurePosixPath(relative).parts
    if (not parts or relative != '/'.join(parts) or '\\' in relative
            or any(part in ('.', '..') for part in parts)
            or PurePosixPath(relative).is_absolute()):
        raise ValueError('Unsafe project path: ' + relative)
    path = root
    for part in parts:
        path = path / part
        if path.is_symlink():
            raise ValueError('Symlink is not allowed: ' + str(path))
    if root not in path.resolve().parents:
        raise ValueError('Path escapes project')
    return path


def verified(path, model):
    if path.is_symlink():
        raise ValueError('Symlink is not allowed: ' + str(path))
    if not path.exists():
        return False
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('Expected a regular file: ' + str(path))
    if path.stat().st_size != model['expectedSize']:
        return False
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''):
            digest.update(chunk)
    return digest.hexdigest() == model['sha256']


def download(url, target):
    subprocess.run([
        '/usr/bin/curl', '--fail', '--location', '--proto', '=https',
        '--proto-redir', '=https', '--retry', '3', '--connect-timeout', '30',
        '--max-time', '1800', '--output', str(target), url,
    ], check=True)


def acquire(root, candidate_id, downloader=download):
    root = Path(root)
    # The public CLI has no output/root override. Tests use isolated build folders.
    if root.is_symlink() or root.absolute() != root.resolve():
        raise ValueError('Root must be canonical and cannot contain symlinks')
    if root != ROOT and ROOT / 'build' not in root.parents:
        raise ValueError('Acquisition must remain in this project')
    lock_path = local_path(root, 'models/asr-candidates.lock.json')
    manifest_path = local_path(root, 'models/manifest.json')
    lock = json.loads(lock_path.read_text())
    if lock.get('schemaVersion') != 1:
        raise ValueError('Unsupported candidate lock schema')
    candidates = [c for c in lock['candidates'] if c['id'] == candidate_id]
    if len(candidates) != 1:
        raise ValueError('Unknown or duplicate candidate: ' + candidate_id)
    model = candidates[0]['model']
    version = model['version']
    if (model['type'] != 'asr' or not re.fullmatch('[a-f0-9]{40}', version)
            or not re.fullmatch('[a-f0-9]{64}', model['sha256'])
            or type(model['expectedSize']) is not int or model['expectedSize'] <= 0):
        raise ValueError('Invalid candidate identity')
    target = local_path(root, 'models/' + model['filename'])
    if not model['filename'].startswith('optional/'):
        raise ValueError('Candidate must be optional')
    expected_url = ('https://huggingface.co/ggerganov/whisper.cpp/resolve/'
                    + version + '/' + target.name)
    if model['source'] != expected_url:
        raise ValueError('Candidate source must be pinned HTTPS upstream')
    build = local_path(root, 'build')
    build.mkdir(exist_ok=True)
    process_lock = local_path(root, 'build/asr-candidate-acquisition.lock')
    with process_lock.open('a+b') as held_lock:
        fcntl.flock(held_lock, fcntl.LOCK_EX)
        original_manifest = manifest_path.read_bytes()
        manifest = json.loads(original_manifest)
        if manifest.get('schemaVersion') != 1 or not isinstance(manifest.get('models'), list):
            raise ValueError('Invalid model manifest')
        registered = [m for m in manifest['models'] if m['filename'] == model['filename']]
        if len(registered) > 1 or (registered and registered[0] != model):
            raise ValueError('Conflicting manifest entry; refusing to replace metadata')
        cached = verified(target, model)
        if cached and registered:
            return 'Verified cached candidate; manifest unchanged'
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.asr-candidate-', dir=target.parent) as folder:
            staging = Path(folder)
            candidate = staging / 'model.bin'
            if not cached:
                downloader(model['source'], candidate)
                if not verified(candidate, model):
                    raise ValueError('Candidate size/SHA-256 mismatch; existing files preserved')
            if not registered:
                manifest['models'].append(model)
            staged_manifest = staging / 'manifest.json'
            staged_manifest.write_text(json.dumps(manifest, indent=2) + '\n')
            local_path(root, 'models/' + model['filename'])
            local_path(root, 'models/manifest.json')
            if manifest_path.read_bytes() != original_manifest:
                raise ValueError('Manifest changed during acquisition; retry explicitly')
            # Stage a rollback link before replacing an existing corrupt model.
            backup = staging / 'previous-model.bin'
            had_target = target.exists()
            if not cached and had_target:
                os.link(target, backup)
            replaced = False
            try:
                if not cached:
                    os.replace(candidate, target)
                    replaced = True
                os.replace(staged_manifest, manifest_path)
            except BaseException:
                if replaced:
                    if had_target:
                        os.replace(backup, target)
                    else:
                        target.unlink()
                raise
    return 'Verified candidate and registered inventory; selection/profiles unchanged'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', choices=['large-v3-turbo-q5_0'])
    args = parser.parse_args()
    try:
        print(acquire(ROOT, args.candidate))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit('ABORT: ' + str(error)) from error


if __name__ == '__main__':
    main()
