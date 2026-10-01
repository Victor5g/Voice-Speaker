#!/usr/bin/python3
"""Fetch hash-pinned Sherpa/Piper archives and record every runtime data file."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
root = Path(__file__).resolve().parent.parent
pins = json.loads((root / 'models/tts-downloads.lock.json').read_text())
cards = {item['filename']: item for item in json.loads((root / 'models/tts-cards.lock.json').read_text())}
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''): h.update(chunk)
    return h.hexdigest()
def fetch(pin):
    archive = root / 'tools/downloads' / pin['filename']
    if not archive.is_file() or archive.stat().st_size != pin['expectedSize'] or digest(archive) != pin['sha256']:
        if archive.is_symlink(): raise RuntimeError('Unsafe archive destination')
        subprocess.run(['/usr/bin/curl', '--silent', '--show-error', '--fail', '--location', '--retry', '3',
            '--max-time', '1200', '--output', str(archive), pin['source']], check=True)
        if archive.stat().st_size != pin['expectedSize'] or digest(archive) != pin['sha256']:
            raise RuntimeError('TTS archive integrity failure')
    destination = root / 'models/optional/tts'
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as stream:
        for member in stream.getmembers():
            path = (destination / member.name).resolve()
            if destination.resolve() not in path.parents or not (member.isfile() or member.isdir()):
                raise RuntimeError('Unsafe TTS archive member')
        stream.extractall(destination)
    folder = destination / ('vits-piper-' + pin['voice'])
    # Preserve the model-specific upstream card with its dataset/model terms.
    locale, speaker, quality = pin['voice'].split('-')
    language = locale.split('_')[0]
    revision = '1162a9173d0ce503555aed757976b7a9912eae4c'
    source = f'https://huggingface.co/rhasspy/piper-voices/resolve/{revision}/{language}/{locale}/{speaker}/{quality}/MODEL_CARD'
    subprocess.run(['/usr/bin/curl', '--silent', '--show-error', '--fail', '--location', '--retry', '3',
                    '--output', str(folder / 'UPSTREAM_MODEL_CARD.txt'), source], check=True)
    card = folder / 'UPSTREAM_MODEL_CARD.txt'
    expected = cards[str(card.relative_to(root / 'models'))]
    if card.stat().st_size != expected['expectedSize'] or digest(card) != expected['sha256']: raise RuntimeError('Voice model card integrity failed')
    assets = [{'filename': str(p.relative_to(root / 'models')), 'sha256': digest(p), 'expectedSize': p.stat().st_size}
              for p in sorted(folder.rglob('*')) if p.is_file()]
    primary = next(p for p in assets if p['filename'].endswith('/' + pin['voice'] + '.onnx'))
    print('Verified voice and pronunciation data: ' + pin['voice'], flush=True)
    return dict(primary, name='Piper ' + pin['voice'], type='tts', voice=pin['voice'],
                language='pt-BR' if locale=='pt_BR' else 'en', version=str(pin['assetId']),
                source=pin['source'], archive=pin, upstreamRevision=revision,
                license='See bundled UPSTREAM_MODEL_CARD.txt and espeak-ng COPYING (GPL-3.0-or-later)',
                backend='Sherpa-ONNX 1.13.6 / Piper VITS / CPU', quantization='float32', files=assets)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    entries = list(pool.map(fetch, pins))
(root / 'models/tts-assets.json').write_text(json.dumps(entries, indent=2) + '\n')
manifest = root / 'models/manifest.json'
catalog = json.loads(manifest.read_text())
catalog['models'] = [m for m in catalog['models'] if m['type'] != 'tts'] + entries
manifest.write_text(json.dumps(catalog, indent=2) + '\n')
