#!/usr/bin/python3
"""Development-only, pinned CC-BY-4.0 FLEURS recordings; never bundled in the app."""
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
root = Path(__file__).resolve().parent.parent
revision = '70bb2e84b976b7e960aa89f1c648e09c59f894dd'
pins = {
    'pt_br': (621291324, '968d1b68a90132ca1f044f664866418b0db0351a93601a671d5637016da4f49b'),
    'en_us': (289851356, 'd9c2e37b41aacd41bc283554a0a82b5476b36887049774ecb2819dcaaa55a356')}
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''): h.update(chunk)
    return h.hexdigest()
for pin in json.loads((root / 'tests/data/human-metadata.lock.json').read_text()):
    target = root / pin['filename']
    if root not in target.resolve().parents: raise RuntimeError('Unsafe fixture metadata path')
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.is_file() or digest(target) != pin['sha256']:
        subprocess.run(['/usr/bin/curl', '--silent', '--show-error', '--fail', '--location', '--retry', '3', '--output', str(target), pin['source']], check=True)
    if target.stat().st_size != pin['expectedSize'] or digest(target) != pin['sha256']: raise RuntimeError('Fixture metadata integrity failed')
(root / 'tools/downloads').mkdir(parents=True, exist_ok=True)
def fetch(locale):
    size, sha = pins[locale]
    source = f'https://huggingface.co/datasets/google/fleurs/resolve/{revision}/data/{locale}/audio/test.tar.gz'
    archive = root / 'tools/downloads' / ('fleurs-' + locale + '-test.tar.gz')
    if not archive.is_file() or archive.stat().st_size != size or digest(archive) != sha:
        subprocess.run(['/usr/bin/curl', '--silent', '--show-error', '--fail', '--location', '--retry', '3', '--max-time', '1800', '-o', str(archive), source], check=True)
    if archive.stat().st_size != size or digest(archive) != sha: raise RuntimeError('Human recording archive hash mismatch')
    selected = {}; seen = set()
    with (root / ('cache/fleurs-' + locale + '.tsv')).open() as stream:
        for row in csv.reader(stream, delimiter='\t'):
            if 48000 <= int(row[5]) <= 144000 and row[0] not in seen:
                selected[row[1]] = row; seen.add(row[0])
                if len(selected) == 6: break
    output = root / 'tests/data/human'; output.mkdir(parents=True, exist_ok=True)
    records = []
    with tarfile.open(archive, 'r|gz') as stream:
        for member in stream:
            name = Path(member.name).name
            if name not in selected: continue
            if not member.isfile() or member.size > 16000 * 4 * 30 + 65536: raise RuntimeError('Invalid human fixture')
            row = selected[name]
            original = root / 'cache' / (locale + '-' + name)
            with stream.extractfile(member) as sourceFile, original.open('wb') as target: shutil.copyfileobj(sourceFile, target)
            filename = output / (locale + '-' + name)
            subprocess.run(['/usr/bin/afconvert', str(original), str(filename), '-f', 'WAVE', '-d', 'LEI16@16000', '-c', '1'], check=True)
            records.append(dict(filename=str(filename.relative_to(root)), reference=row[2], normalizedReference=row[3],
                language=locale, datasetItem=row[0], sourceRecording=name, archive=source, archiveSHA256=sha,
                originalSHA256=digest(original), sha256=digest(filename), expectedSize=filename.stat().st_size,
                revision=revision, license='CC-BY-4.0', type='human recording; speaker identity labels not provided'))
            if len(records) == len(selected): break
    if len(records) != 6: raise RuntimeError('Human fixture set incomplete')
    print('Verified six human recordings: ' + locale, flush=True)
    return records
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: groups = list(pool.map(fetch, pins))
manifest = root / 'tests/data/human/manifest.json'
previous = json.loads(manifest.read_text()) if manifest.is_file() else {}
manifest.write_text(json.dumps(dict(previous, dataset='Google FLEURS', source='https://huggingface.co/datasets/google/fleurs',
    license='CC-BY-4.0', recordings=[r for group in groups for r in group]), indent=2) + '\n')
shutil.copyfile(root / 'cache/fleurs-card.md', root / 'tests/data/human/DATASET_CARD.md')
