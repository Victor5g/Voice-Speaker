#!/usr/bin/python3
"""Build 60 paired human fixtures per language from already pinned local archives.

No download, network request, TTS, speaker inference, or acoustic annotation occurs.
Half of the paired sentences form a reserved set; do not tune against that set.
"""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import wave

root = Path(__file__).resolve().parents[2]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def local(path):
    if not path.resolve().is_relative_to(root):
        raise SystemExit('Corpus destination escapes the project')
    return path


baseline = json.loads((root/'tests/data/human/manifest.json').read_text())
excluded = {r['datasetItem'] for r in baseline['recordings']}
metadata_locks = json.loads((root/'tests/data/human-metadata.lock.json').read_text())
groups, pins = {}, {}
for language in ('pt_br', 'en_us'):
    metadata = root/f'cache/fleurs-{language}.tsv'
    lock = next(p for p in metadata_locks if p['filename'] == str(metadata.relative_to(root)))
    if metadata.stat().st_size != lock['expectedSize'] or digest(metadata) != lock['sha256']:
        raise SystemExit('Unverified local metadata: '+language)
    items = {}
    with metadata.open() as stream:
        for row in csv.reader(stream, delimiter='\t', quoting=csv.QUOTE_NONE):
            if len(row) != 7 or not row[5].isdigit():
                raise SystemExit('Malformed pinned TSV row')
            if row[0] not in excluded and 48000 <= int(row[5]) <= 144000:
                items.setdefault(row[0], row)
    groups[language] = items
    pins[language] = next(r for r in baseline['recordings'] if r['language'] == language)

paired = set(groups['pt_br']) & set(groups['en_us'])
selected = sorted(paired, key=lambda item: hashlib.sha256(('lvt-latency-v1:'+item).encode()).hexdigest())[:60]
if len(selected) != 60:
    raise SystemExit('Insufficient paired local recordings; no synthetic replacement is allowed')
splits = {item: 'development' if i < 30 else 'reserved' for i, item in enumerate(selected)}
output = local(root/'tests/data/latency-human')
output.mkdir(parents=True, exist_ok=True)
scratch = local(root/'cache/latency-human')
scratch.mkdir(parents=True, exist_ok=True)
records = []
for language in ('pt_br', 'en_us'):
    pin = pins[language]
    archive = root/f'tools/downloads/fleurs-{language}-test.tar.gz'
    if not archive.is_file() or digest(archive) != pin['archiveSHA256']:
        raise SystemExit('Pinned local archive is unavailable or corrupt: '+language)
    wanted = {groups[language][item][1]: item for item in selected}
    remaining = set(wanted)
    with tarfile.open(archive, 'r|gz') as stream:
        for member in stream:
            name = Path(member.name).name
            if name not in remaining:
                continue
            if Path(name).name != name or not member.isfile() or member.size > 16000*4*30+65536:
                raise SystemExit('Unsafe selected archive member')
            item = wanted[name]; row = groups[language][item]
            original = local(scratch/(language+'-'+name))
            if original.is_symlink(): raise SystemExit('Unsafe scratch destination')
            with stream.extractfile(member) as source, original.open('wb') as destination:
                shutil.copyfileobj(source, destination)
            directory = local(output/splits[item]); directory.mkdir(exist_ok=True)
            target = local(directory/(language+'-'+name))
            if target.is_symlink(): raise SystemExit('Unsafe fixture destination')
            if target.exists(): target.unlink()  # Dedicated generated corpus only.
            subprocess.run(['/usr/bin/afconvert', str(original), str(target), '-f', 'WAVE', '-d', 'LEI16@16000', '-c', '1'], check=True)
            with wave.open(str(target)) as audio:
                count = audio.getnframes()
                if audio.getframerate()!=16000 or audio.getnchannels()!=1 or audio.getsampwidth()!=2:
                    raise SystemExit('Unexpected converted audio contract')
            other = 'en_us' if language == 'pt_br' else 'pt_br'
            records.append(dict(filename=str(target.relative_to(root)), split=splits[item], language=language,
                                datasetItem=item, reference=row[2], normalizedReference=row[3],
                                parallelTranslationReference=groups[other][item][2],
                                samples=count, expectedSize=target.stat().st_size, sha256=digest(target),
                                originalSHA256=digest(original), archive=pin['archive'], archiveSHA256=pin['archiveSHA256'],
                                revision=pin['revision'], license='CC-BY-4.0', speakerReference=None,
                                acousticAnnotation='PENDING', meaningReview='PENDING'))
            remaining.remove(name)
            if not remaining: break
    if remaining: raise SystemExit('Archive did not contain all selected recordings')
    print('Prepared 60 paired human recordings: '+language, flush=True)

manifest = dict(schemaVersion=1, dataset='Google FLEURS', license='CC-BY-4.0',
                selection='60 paired sentence IDs, SHA-256 order lvt-latency-v1, excluding the previous 12 recordings',
                split='30 development and 30 reserved sentences per language; same sentence always in same split',
                limitation='Speaker identity and acoustic boundaries are unavailable; this does not establish six speakers or acoustic latency.',
                recordings=sorted(records, key=lambda r: (r['split'], r['datasetItem'], r['language'])))
local(output/'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+'\n')
print('Prepared 120 human fixtures. Acoustic annotation, identity coverage and meaning review remain pending.', flush=True)
