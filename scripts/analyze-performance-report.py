#!/usr/bin/env python3
"""Summarize panel samples without treating repeated latency values as events."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re


COUNTERS = set('''acceptedBlocks audioDropped captureCallbacks configurationRestarts
eventDrops inputFrames outputFrames overlapWindows partialPublished partialReplaced
playbackCallbacks playbackDropped playbackUnitsSuppressed referenceDropped segmentsDropped
selfPlaybackBlocks separationCalls synthesizedCount transcriptDropped translatedCount
translationDropped ttsCacheHits ttsCacheMisses ttsDropped vadErrors vadFrames vadSegments
vadVoicedFrames vadDropped asrCompleted asrEmptyUnits asrObsoleteJobs firstAudioCount
e2eCount afterVadEndCount languageDetections rejectedNoSpeech rejectedDigitalSilence audioSkipped chunkLagCount
revisionConflicts recoveredContinuations memoryAdmissionDenied'''.split())
IDENTIFIERS = {'loadedVoice', 'ttsCachedVoices', 'sourceLanguage', 'targetLanguage'}
CONTENT_KEYS = {'text', 'original', 'translation', 'transcript', 'error', 'message',
                'inputDeviceUID', 'outputDeviceUID', 'audioError', 'asrError', 'conversationError'}
SAMPLE = re.compile(r'^AMOSTRA (\d+) \| UTC=([^|]+) \| elapsedMs=(\d+)$')
SESSION = re.compile(r'^session=(\d+) mode=(\w+)$')


def evidence(path):
    data = path.read_bytes()
    return {'file': path.name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def summarize_values(values, counter=False):
    valid = [value for value in values if value is not None]
    result = {'panelSamples': len(values), 'measuredSamples': len(valid),
              'missingOrInvalidSamples': len(values) - len(valid),
              'min': min(valid) if valid else None, 'max': max(valid) if valid else None,
              'firstMeasured': valid[0] if valid else None,
              'lastMeasured': valid[-1] if valid else None,
              'zeroSamples': sum(value == 0 for value in valid)}
    if counter:
        resets = sum(b < a for a, b in zip(valid, valid[1:]))
        result['counterResetObservations'] = resets
        result['counterDelta'] = valid[-1] - valid[0] if valid and not resets else None
        result['counterDeltaScope'] = 'Last minus first observed value within this session/path; excludes activity before recording; null after observed reset.'
    return result


def analyze(path):
    text = path.read_text(encoding='utf-8')
    samples, current, section = [], None, None
    metadata = {}
    configurations = {key: set() for key in ('asrModel', 'asrModelSHA256', 'requestedBackend', 'profile')}
    for line in text.splitlines():
        if match := SAMPLE.fullmatch(line):
            current = {'sample': int(match[1]), 'utc': match[2].strip(),
                       'elapsedMs': int(match[3]), 'session': None, 'mode': None, 'paths': {}}
            samples.append(current); section = None
            continue
        if current is None:
            for label, key in [('Duração: ', 'declaredDurationSeconds'), ('Amostras de painel: ', 'declaredPanelSamples')]:
                if line.startswith(label):
                    value = line[len(label):].removesuffix(' s')
                    metadata[key] = float(value) if key.endswith('Seconds') else int(value)
            continue
        if match := SESSION.fullmatch(line):
            current['session'] = int(match[1]); current['mode'] = match[2]
            continue
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1]
            if section in current['paths']:
                raise ValueError('Duplicate path within panel sample')
            current['paths'][section] = {'numeric': {}, 'identifiers': {}}
            continue
        if section is None and '=' in line:
            key, value = line.split('=', 1)
            if key in configurations and re.fullmatch(r'[A-Za-z0-9_ .+\-]*', value) and len(value) <= 256:
                configurations[key].add(value)
        if section is None or '=' not in line:
            continue
        key, value = line.split('=', 1)
        if key in CONTENT_KEYS:
            continue
        target = current['paths'][section]
        if key in IDENTIFIERS:
            # Only fixed backend identifiers; never ingest text/errors/device IDs.
            if re.fullmatch(r'[A-Za-z0-9_,; .\-]*', value) and len(value) <= 256:
                target['identifiers'][key] = value
            continue
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9]*', key):
            continue
        try:
            numeric = float(value)
            if not math.isfinite(numeric): numeric = None
        except ValueError:
            if value != 'NOT MEASURED': continue
            numeric = None
        if key in target['numeric']:
            raise ValueError('Duplicate metric within panel sample')
        target['numeric'][key] = numeric
    if not samples:
        raise ValueError('No panel samples found')
    groups = {}
    for sample in samples:
        if sample['session'] is None:
            raise ValueError('Panel sample has no session')
        for route, values in sample['paths'].items():
            group = groups.setdefault(f"session {sample['session']} / {route}",
                                      {'session': sample['session'], 'mode': sample['mode'], 'route': route, 'samples': []})
            group['samples'].append(values)
    for group in groups.values():
        values = group.pop('samples')
        group['panelSamples'] = len(values)
        keys = sorted({key for sample in values for key in sample['numeric']})
        group['metrics'] = {key: summarize_values([sample['numeric'].get(key) for sample in values], key in COUNTERS) for key in keys}
        group['observedIdentifiers'] = {key: sorted({sample['identifiers'][key] for sample in values
                                                   if key in sample['identifiers']}) for key in sorted(IDENTIFIERS)}
    warnings = []
    if metadata.get('declaredPanelSamples', len(samples)) != len(samples):
        warnings.append('Declared sample count does not match parsed samples')
    if any(b['sample'] <= a['sample'] or b['elapsedMs'] < a['elapsedMs'] for a, b in zip(samples, samples[1:])):
        warnings.append('Sample sequence or monotonic elapsed time regressed')
    return {'input': evidence(path), 'metadata': metadata, 'parsedPanelSamples': len(samples),
            'firstElapsedMs': samples[0]['elapsedMs'], 'lastElapsedMs': samples[-1]['elapsedMs'],
            'observedSpanMs': samples[-1]['elapsedMs'] - samples[0]['elapsedMs'],
            'statisticScope': 'Panel snapshots of latest values; repeated values are not independent inference events. No percentiles recomputed. Existing P50/P95 fields are engine-reported snapshots only.',
            'privacyScope': 'Numeric metrics and allowlisted backend identifiers only; conversation text and arbitrary strings ignored.',
            'observedConfigurations': {key: sorted(values) for key, values in configurations.items()},
            'warnings': warnings, 'paths': groups}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--attachment', type=Path, action='append', default=[])
    args = parser.parse_args()
    result = analyze(args.report)
    result['attachments'] = [evidence(path) for path in args.attachment]
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
