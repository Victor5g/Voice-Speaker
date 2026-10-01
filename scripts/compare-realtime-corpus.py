#!/usr/bin/env python3
"""Compare matched native human replays without counting panel states as events."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = [value for value in values if value is not None and math.isfinite(value)]
    return {'n': len(values), 'meanMs': statistics.mean(values) if values else None,
            'minMs': min(values) if values else None, 'maxMs': max(values) if values else None,
            'p50Ms': statistics.median(values) if len(values) >= 30 else None,
            'p95Ms': statistics.quantiles(values, n=100, method='inclusive')[94] if len(values) >= 30 else None}


def file_evidence(path):
    raw = path.read_bytes()
    return {'file': str(path), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def elapsed(event, origin):
    timing = event.get('timing', {})
    ticks, rate = timing.get('playbackEvent'), timing.get('ticksPerSecond')
    if not origin or not isinstance(ticks, (int, float)) or not isinstance(rate, (int, float)):
        return None
    if not math.isfinite(ticks) or not math.isfinite(rate) or rate <= 0 or ticks < origin:
        return None
    return (ticks - origin) * 1000 / rate


def identity(event):
    if not isinstance(event.get('session'), int) or not isinstance(event.get('segment'), int):
        raise ValueError('Event without session/segment identity')
    return event['session'], event['segment']


def recording_result(row, events):
    by_state = {'audio-onset': {}, 'played': {}, 'translated': {}, 'audio-skipped': {}, 'text-only': {}}
    issues = list(row.get('errors', []))
    duplicates = 0
    first_frame = row.get('captureFirstFrame', 0)
    for event in events:
        timing = event.get('timing', {})
        if first_frame and timing.get('windowEndFrame', 0) and timing['windowEndFrame'] <= first_frame:
            issues.append('event-from-previous-recording')
        state = event['state']
        if state in ('error', 'dropped', 'unavailable', 'revision-conflict', 'revision-preserved'):
            issues.append(state)
        if state not in by_state:
            continue
        key = identity(event)
        previous = by_state[state].get(key)
        if previous is not None:
            duplicates += 1
            if previous.get('timing', {}).get('playbackEvent') != timing.get('playbackEvent'):
                issues.append('conflicting-duplicate-' + state)
        else:
            by_state[state][key] = event
    onset_ids, played_ids, translated_ids = (set(by_state[name]) for name in ('audio-onset', 'played', 'translated'))
    for field, observed in [('translatedUnits', len(translated_ids)), ('playedUnits', len(played_ids))]:
        if row.get(field) != observed:
            issues.append('summary-event-count-mismatch:' + field)
    omitted = set(by_state['audio-skipped']) | set(by_state['text-only'])
    if omitted:
        issues.append('spoken-output-omitted-or-disabled')
    if not translated_ids or onset_ids != translated_ids or played_ids != translated_ids:
        issues.append('incomplete-spoken-coverage')
    origin = row.get('captureOrigin', 0)
    onsets = [elapsed(event, origin) for event in by_state['audio-onset'].values()]
    endings = [elapsed(event, origin) for event in by_state['played'].values()]
    if any(value is None for value in onsets + endings):
        issues.append('missing-or-invalid-playback-clock')
    onsets = [value for value in onsets if value is not None]
    endings = [value for value in endings if value is not None]
    capture_ms = row.get('samples', 0) / 16 if row.get('samples', 0) > 0 else None
    if capture_ms is None:
        issues.append('missing-capture-duration')
    vad = []
    for event in by_state['audio-onset'].values():
        timing = event.get('timing', {})
        value = timing.get('afterVadEndEstimatedMs')
        if timing.get('endpointReason') == 1 and isinstance(value, (int, float)) and math.isfinite(value) and value >= 0:
            vad.append(value)
    if any(row.get(field, 0) for field in ('pendingUnits', 'pendingEvents')):
        issues.append('pending-work-at-recording-end')
    issues = sorted(set(issues))
    return {'recording': row['recording'], 'sha256': row['sha256'], 'language': row['language'],
            'reference': row['reference'], 'original': row['original'], 'translation': row['translation'],
            'translationReference': row.get('translationReference'), 'split': row.get('split'),
            'captureDurationMs': capture_ms, 'rawQualityErrors': row.get('errors', []),
            'comparisonIssues': issues, 'meaningAcceptance': 'PENDING',
            'translatedUnits': len(translated_ids), 'playedUnits': len(played_ids),
            'onsetUnits': len(onset_ids), 'omittedUnits': len(omitted), 'repeatedStatesIgnored': duplicates,
            'firstAudioFromCaptureStartMs': min(onsets) if onsets else None,
            'lastPlaybackFromCaptureStartMs': max(endings) if endings else None,
            'lastPlaybackFromCaptureEndMs': max(endings) - capture_ms if endings and capture_ms is not None else None,
            'afterVadEndEstimatedMsByUnit': vad,
            'timingEligible': not issues}


def read_run(path, expected):
    if not path.exists():
        return {'status': 'MISSING', 'records': {}, 'summary': None, 'evidence': None}
    raw = path.read_bytes()
    boundary = raw.rfind(b'\n') + 1
    rows = [json.loads(line) for line in raw[:boundary].decode('utf-8').splitlines() if line.startswith('{')]
    records, events, summary = {}, {}, None
    for row in rows:
        if row.get('recordingResult'):
            if row['recording'] in records:
                raise ValueError('Duplicate recording result')
            records[row['recording']] = row
        elif 'recording' in row and 'state' in row:
            events.setdefault(row['recording'], []).append(row)
        elif 'result' in row:
            if summary is not None:
                raise ValueError('Duplicate native summary')
            summary = row
    results = {name: recording_result(row, events.get(name, [])) for name, row in records.items()}
    complete = summary is not None and len(results) == expected and summary.get('recordings') == expected and boundary == len(raw)
    status = 'COMPLETE' if complete else 'INCOMPLETE'
    if complete and (summary.get('result') != 'PASS' or any(r['comparisonIssues'] for r in results.values())):
        status = 'COMPLETE_WITH_ISSUES'
    if any('event-from-previous-recording' in r['comparisonIssues'] for r in results.values()):
        status = 'INVALID_ATTRIBUTION'
        # The source record can also appear empty; do not compare another subset.
        for row in results.values():
            row['timingEligible'] = False
    return {'status': status, 'records': results, 'summary': summary, 'evidence': file_evidence(path)}


TIMINGS = ('firstAudioFromCaptureStartMs', 'lastPlaybackFromCaptureStartMs', 'lastPlaybackFromCaptureEndMs')


def compare(directory, expected=5):
    report = {'scope': 'Native replay of matched licensed development recordings, not physical/live or acoustic acceptance.',
              'sampleScope': 'One first onset and last playback per recording; each onset deduplicated by session/segment. VAD estimates remain separate per-unit samples.',
              'percentileScope': 'P50/P95 null for fewer than 30 observations. Negative lastPlaybackFromCaptureEndMs can occur with streaming and trailing WAV silence.',
              'meaningAcceptance': 'PENDING', 'expectedPerLanguage': expected, 'runs': {}, 'pairs': []}
    for language in ('en', 'pt'):
        runs = {phase: read_run(directory / f'corpus-{phase}-{language}.jsonl', expected) for phase in ('before', 'after')}
        for phase, run in runs.items():
            valid = [row for row in run['records'].values() if row['timingEligible']]
            report['runs'][f'{phase}-{language}'] = {
                'status': run['status'], 'completedRecordings': len(run['records']),
                'timingEligibleRecordings': len(valid), 'evidence': run['evidence'],
                'nativeSummary': run['summary'], 'peakFootprintBytes': (run['summary'] or {}).get('peakFootprintBytes'),
                'recordings': list(run['records'].values()),
                'timing': {key: stats([row[key] for row in valid]) for key in TIMINGS},
                'afterVadEndEstimatedByUnit': stats([value for row in valid for value in row['afterVadEndEstimatedMsByUnit']])}
        before, after = runs['before']['records'], runs['after']['records']
        for name in sorted(before.keys() & after.keys()):
            a, b = before[name], after[name]
            for key in ('sha256', 'reference', 'translationReference', 'language', 'split', 'captureDurationMs'):
                if a[key] != b[key]:
                    raise ValueError(f'Unmatched {key}: {name}')
            issues = list(set(a['comparisonIssues'] + b['comparisonIssues']))
            if a['playedUnits'] != b['playedUnits']:
                issues.append('spoken-unit-count-changed: review segmentation/omission before latency claim')
            eligible = (a['timingEligible'] and b['timingEligible'] and not issues and
                        all(run['status'] in ('COMPLETE', 'COMPLETE_WITH_ISSUES') for run in runs.values()))
            report['pairs'].append({'recording': name, 'sha256': a['sha256'], 'language': language,
                                    'beforePlayedUnits': a['playedUnits'], 'afterPlayedUnits': b['playedUnits'],
                                    'transcriptChanged': a['original'] != b['original'],
                                    'translationChanged': a['translation'] != b['translation'],
                                    'meaningAcceptance': 'PENDING', 'timingComparable': eligible,
                                    'comparisonIssues': sorted(issues),
                                    'afterMinusBeforeMs': {key: b[key] - a[key] if eligible and a[key] is not None and b[key] is not None else None for key in TIMINGS}})
    report['allRunsTerminal'] = all(run['nativeSummary'] is not None for run in report['runs'].values())
    report['comparisonComplete'] = (len(report['pairs']) == expected * 2 and
                                    all(run['status'] in ('COMPLETE', 'COMPLETE_WITH_ISSUES') for run in report['runs'].values()))
    report['comparablePairs'] = sum(pair['timingComparable'] for pair in report['pairs'])
    report['pairedTimingDeltas'] = {key: stats([pair['afterMinusBeforeMs'][key] for pair in report['pairs']]) for key in TIMINGS}
    report['coverageWarning'] = 'Faster output with missing/omitted units is not a latency improvement. Changed text still requires meaning review even when timing is comparable.'
    report['runProvenance'] = {}
    for phase in ('before', 'after'):
        path = directory / f'corpus-{phase}-runs.json'
        if path.exists():
            report['runProvenance'][phase] = {'evidence': file_evidence(path), 'runs': json.loads(path.read_text())}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--expected', type=int, default=5)
    parser.add_argument('--allow-incomplete', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.expected <= 30:
        parser.error('Expected 1..30 recordings per language')
    report = compare(args.directory, args.expected)
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if not report['comparisonComplete'] and not args.allow_incomplete:
        raise SystemExit(2)


if __name__ == '__main__':
    main()
