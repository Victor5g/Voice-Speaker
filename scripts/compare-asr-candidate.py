#!/usr/bin/python3
"""Development-only isolated ASR screen. Requires --run; never changes selection."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import unicodedata

ROOT = Path(__file__).resolve().parent.parent
REPEATS = 3
CHOICES = [('pt_br', 0, 'ferver'), ('pt_br', 19, 'Great Yarmouth'),
           ('pt_br', 29, 'cálcio'), ('en_us', 8, '11:35'),
           ('en_us', 19, 'Great Yarmouth'), ('en_us', 24, 'Haiti')]
MODELS = [('small', 'optional/ggml-small.bin'),
          ('turbo', 'optional/ggml-large-v3-turbo-q5_0.bin')]
SCOPE = ('Exploratory isolated warm ASR on six explicitly chosen development recordings; '
         '18 outputs/model, three repeated outputs per recording. '
         'Not streaming, E2E, independent 18-recording quality, ANE placement, or promotion evidence. '
         'Fixed model order small then turbo; order/thermal effects are not counterbalanced.')


def validate_preflight(snapshot, now=None):
    """Read-only process inventory supplied outside the project's nested sandbox."""
    now = now or datetime.now(timezone.utc)
    captured = datetime.fromisoformat(snapshot['capturedAt'])
    if captured.tzinfo is None or not 0 <= (now - captured).total_seconds() <= 60:
        raise ValueError('System preflight must have a timezone and be captured within 60 seconds')
    processes = snapshot['processes']
    if not isinstance(processes, list) or not processes:
        raise ValueError('System preflight needs a nonempty process inventory')
    heavy = []
    for process in processes:
        if type(process['pid']) is not int or not isinstance(process['command'], str):
            raise ValueError('Invalid preflight process')
        name = Path(process['command']).name
        if (name in ('xcodebuild', 'clang', 'clang++', 'swift-frontend', 'swiftc', 'cc1', 'llama-cli')
                or name.startswith(('asr-', 'human-corpus', 'conversation-', 'whisper-'))):
            heavy.append(process)
    if heavy:
        raise ValueError('Concurrent build/inference prevents screening: ' + json.dumps(heavy))
    return snapshot


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def local(root, relative):
    relative = str(relative)
    if Path(relative).is_absolute() or '..' in Path(relative).parts:
        raise ValueError('Unsafe path: ' + relative)
    path = root
    for part in Path(relative).parts:
        path /= part
        if path.is_symlink():
            raise ValueError('Symlink is not allowed: ' + relative)
    if root not in path.resolve().parents:
        raise ValueError('Path escapes project')
    return path


def chosen_recordings(manifest):
    result = []
    for language, index, marker in CHOICES:
        options = [r for r in manifest['recordings']
                   if r['split'] == 'development' and r['language'] == language]
        recording = dict(options[index])
        if marker not in recording['reference']:
            raise ValueError('Fixture index no longer matches explicit screen selection')
        recording.update({'developmentLanguageIndex': index, 'selectionMarker': marker})
        result.append(recording)
    return result


def errors(reference, hypothesis):
    words = lambda text: re.findall(r'\w+', unicodedata.normalize('NFC', text).casefold())
    left, right = words(reference), words(hypothesis)
    if not left:
        raise ValueError('Empty reference')
    row = list(range(len(right) + 1))
    for i, word in enumerate(left, 1):
        current = [i]
        for j, other in enumerate(right, 1):
            current.append(min(current[-1] + 1, row[j] + 1, row[j - 1] + (word != other)))
        row = current
    return row[-1], len(left)


def measurements(values):
    return {'samples': len(values), 'mean': statistics.mean(values),
            'min': min(values), 'max': max(values)}


def evaluate(rows, recordings, model_path):
    truth = {str(ROOT / r['filename']): r for r in recordings}
    expected = Counter((path, iteration) for path in truth for iteration in range(REPEATS))
    observed = Counter((r['fixture'], r['iteration']) for r in rows)
    if observed != expected:
        raise ValueError('Incomplete, duplicated, or unexpected fixture/iteration set')
    enriched = []
    for row in rows:
        if row['model'] != str(model_path) or row['backend'] != 0 or row['metal'] is not True:
            raise ValueError('Model/backend does not match Metal configuration')
        if row['encoderReuse'] is not True:
            raise ValueError('Unexpected encoder reuse configuration')
        for name in ('asrLatencyMs', 'modelLoadMs', 'warmupMs', 'physicalFootprint',
                     'melMs', 'languageMs', 'decodeMs', 'audioDurationMs', 'rtf',
                     'peakRSS', 'metalAllocatedBytes'):
            if not isinstance(row[name], (int, float)) or not math.isfinite(row[name]) or row[name] < 0:
                raise ValueError('Invalid benchmark measurement: ' + name)
        if row['physicalFootprint'] == 0 or row['audioDurationMs'] == 0:
            raise ValueError('Missing memory/audio duration measurement')
        if row['thermalState'] not in (0, 1, 2, 3) or not isinstance(row['sourceText'], str):
            raise ValueError('Invalid thermal state or transcript')
        recording = truth[row['fixture']]
        distance, count = errors(recording['reference'], row['sourceText'])
        enriched.append(dict(row, reference=recording['reference'],
                             expectedLanguage=recording['language'][:2],
                             languageCorrect=row['language'] == recording['language'][:2],
                             wordErrors=distance, referenceWords=count, wer=distance / count,
                             meaningReview='PENDING'))
    def aggregate(subset):
        return {'samples': len(subset), 'latencyMs': measurements([r['asrLatencyMs'] for r in subset]),
                'melMs': measurements([r['melMs'] for r in subset]),
                'languageMs': measurements([r['languageMs'] for r in subset]),
                'decodeMs': measurements([r['decodeMs'] for r in subset]),
                'meanWER': statistics.mean(r['wer'] for r in subset),
                'weightedWER': sum(r['wordErrors'] for r in subset) / sum(r['referenceWords'] for r in subset),
                'allLanguagesCorrect': all(r['languageCorrect'] for r in subset),
                'footprintBytes': measurements([r['physicalFootprint'] for r in subset]),
                'maxPeakRSSBytes': max(r['peakRSS'] for r in subset),
                'maxMetalAllocatedBytes': max(r['metalAllocatedBytes'] for r in subset),
                'thermalStates': sorted(set(r['thermalState'] for r in subset))}
    report = aggregate(enriched)
    report['languages'] = {lang: aggregate([r for r in enriched if r['expectedLanguage'] == lang])
                           for lang in ('pt', 'en')}
    report['fixtures'] = {path: aggregate([r for r in enriched if r['fixture'] == path]) for path in truth}
    report['loadMs'] = rows[0]['modelLoadMs']
    report['warmupExcluded'] = True
    report['percentiles'] = 'Not reported: fewer than 30 outputs/model and only six unique recordings'
    return enriched, report


def run(output, timeout, preflight_file):
    output = local(ROOT, output)
    if ROOT / 'artifacts' not in output.parents:
        raise ValueError('Output must be a new directory inside artifacts')
    if output.exists():
        raise ValueError('Output exists; choose a new path to preserve raw evidence')
    if not preflight_file:
        raise ValueError('--system-preflight is required; collect process inventory outside nested sandbox')
    preflight_path = local(ROOT, preflight_file)
    preflight = validate_preflight(json.loads(preflight_path.read_text()))
    catalog = json.loads(local(ROOT, 'models/manifest.json').read_text())
    fixture_manifest = local(ROOT, 'tests/data/latency-human/manifest.json')
    recordings = chosen_recordings(json.loads(fixture_manifest.read_text()))
    binary = local(ROOT, 'build/asr-benchmark')
    selection = local(ROOT, 'models/selection.json')
    original_selection = selection.read_bytes()
    models = []
    for name, filename in MODELS:
        matches = [m for m in catalog['models'] if m['type'] == 'asr' and m['filename'] == filename]
        if len(matches) != 1:
            raise ValueError('Missing or duplicate candidate model')
        model = matches[0]
        path = local(ROOT, 'models/' + filename)
        if path.stat().st_size != model['expectedSize'] or sha(path) != model['sha256']:
            raise ValueError('Model integrity mismatch: ' + filename)
        models.append((name, model, path))
    for recording in recordings:
        path = local(ROOT, recording['filename'])
        if path.stat().st_size != recording['expectedSize'] or sha(path) != recording['sha256']:
            raise ValueError('Fixture integrity mismatch: ' + recording['filename'])
    provenance_paths = ['scripts/compare-asr-candidate.py', 'tests/cpp/AsrBenchmark.mm',
                        'engine/asr/WhisperASR.cpp', 'engine/asr/WhisperASR.hpp',
                        'models/manifest.json', 'models/asr-candidates.lock.json',
                        'models/selection.json', 'build/asr-benchmark',
                        'tests/data/latency-human/manifest.json']
    provenance = {p: sha(local(ROOT, p)) for p in provenance_paths}
    for path in sorted((ROOT / 'build/whisper/lib').glob('*.a')):
        provenance[str(path.relative_to(ROOT))] = sha(local(ROOT, path.relative_to(ROOT)))
    output.mkdir(parents=True)
    (output / 'system-preflight.json').write_text(json.dumps(preflight, indent=2) + '\n')
    result = {'status': 'RUNNING', 'scope': SCOPE, 'startedAt': datetime.now(timezone.utc).isoformat(),
              'hardware': subprocess.check_output(['/usr/sbin/sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
              'macOS': subprocess.check_output(['/usr/bin/sw_vers', '-productVersion'], text=True).strip(),
              'sourceHashes': provenance, 'fixtures': recordings, 'repeatsPerFixture': REPEATS,
              'models': [m for _, m, _ in models], 'results': [], 'selectionChanged': False,
              'systemPreflightSHA256': sha(preflight_path), 'loadAverageAtStart': os.getloadavg(),
              'processObservationLimit': 'Snapshot before run only; caller must inspect processes after run. No claim of OS exclusivity.',
              'thermalObservation': 'Per-inference NSProcessInfo thermalState in raw rows; no temperature in Celsius inferred.'}
    def save():
        (output / 'summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    save()
    try:
        for name, model, path in models:
            if sha(binary) != provenance['build/asr-benchmark']:
                raise ValueError('Benchmark binary changed during screen')
            command = [str(ROOT / 'scripts/project-local.sh'), str(binary), '--backend', 'metal',
                       '--resources', str(ROOT / 'models'), str(path), model['sha256'], str(REPEATS),
                       *[str(ROOT / r['filename']) for r in recordings]]
            entry = {'name': name, 'command': command, 'timeoutSeconds': timeout,
                     'startedAt': datetime.now(timezone.utc).isoformat(), 'loadAverageAtStart': os.getloadavg()}
            result['results'].append(entry)
            save()
            print('Measuring ' + name, flush=True)
            with (output / (name + '.jsonl')).open('w') as out, (output / (name + '.stderr.log')).open('w') as err:
                try:
                    completed = subprocess.run(command, cwd=ROOT, stdout=out, stderr=err, timeout=timeout)
                    entry['exitCode'] = completed.returncode
                except subprocess.TimeoutExpired:
                    entry.update({'exitCode': None, 'timedOut': True})
                    raise
            entry['finishedAt'] = datetime.now(timezone.utc).isoformat()
            entry['loadAverageAtEnd'] = os.getloadavg()
            if entry['exitCode'] != 0:
                raise RuntimeError(name + ' benchmark failed; raw output preserved')
            raw = (output / (name + '.jsonl')).read_text().splitlines()
            enriched, report = evaluate([json.loads(line) for line in raw], recordings, path)
            entry.update(report)
            (output / (name + '-evaluated.jsonl')).write_text(''.join(json.dumps(r, ensure_ascii=False) + '\n' for r in enriched))
            save()
        if selection.read_bytes() != original_selection:
            result['selectionChanged'] = True
            raise ValueError('Selection changed during screen; results require review')
        result['status'] = 'PASS_SCREEN_COMPLETE_NOT_MODEL_APPROVAL'
    except BaseException as error:
        result.update({'status': 'FAIL', 'error': type(error).__name__ + ': ' + str(error)})
        raise
    finally:
        result['finishedAt'] = datetime.now(timezone.utc).isoformat()
        result['loadAverageAtEnd'] = os.getloadavg()
        result['selectionChanged'] = selection.read_bytes() != original_selection
        save()
    print('Screen complete; no selection changes. ' + str(output / 'summary.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='Execute both models serially')
    parser.add_argument('--output', default='artifacts/asr-turbo-screen')
    parser.add_argument('--timeout', type=int, default=1200, help='Per-model seconds')
    parser.add_argument('--system-preflight', help='Project-relative JSON process inventory captured within 60 seconds')
    args = parser.parse_args()
    if not args.run:
        print(SCOPE + '\nPrepared only. Add --run to execute; no inference performed.')
        return
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    run(args.output, args.timeout, args.system_preflight)


if __name__ == '__main__':
    main()
