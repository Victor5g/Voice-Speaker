#!/usr/bin/python3
"""Read-only comparison of development corpus traces, including live snapshots.

Highlights differences for human review. It never approves meaning or latency.
"""
import argparse
import json
from pathlib import Path
import re
import math
import statistics

NON_SPOKEN_STATES = {'direction-filtered', 'uncertain', 'unsupported', 'overlap-unresolved'}


def distribution(values):
    values = [v for v in values if v is not None and math.isfinite(v) and v >= 0]
    result = {'n': len(values), 'meanMs': statistics.mean(values) if values else None}
    if len(values) >= 30:
        result.update(p50Ms=statistics.median(values),
                      p95Ms=statistics.quantiles(values, n=100, method='inclusive')[94])
    return result


def record_timing(record, events):
    partial = [e for e in events if e['state'] == 'recognized' and e['timing'].get('endpointReason') == 0]
    first = None
    origin = record.get('captureOrigin', 0)
    for event in events:
        if event['state'] != 'audio-onset':
            continue
        t = event['timing']
        ticks, rate = t.get('playbackEvent', 0), t.get('ticksPerSecond', 0)
        if not origin or ticks < origin or not math.isfinite(rate) or rate <= 0:
            continue
        elapsed = (ticks - origin) * 1000 / rate
        if first is None or elapsed < first['captureToFirstOnsetMs']:
            first = {'captureToFirstOnsetMs': elapsed,
                     'firstEndpointReason': t.get('endpointReason'),
                     'firstAfterVadEndEstimatedMs': t.get('afterVadEndEstimatedMs') if t.get('endpointReason') == 1 else None}
    result = {'partialConfirmedUnits': len(partial), 'hasPartialConfirmation': bool(partial),
              'captureToFirstOnsetMs': None, 'firstEndpointReason': None,
              'firstAfterVadEndEstimatedMs': None, 'firstOnsetBeforeInputEnd': None}
    if first:
        result.update(first)
        if record.get('samples', 0) > 0:
            # This is the end of the supplied WAV, not an annotated speech end.
            result['firstOnsetBeforeInputEnd'] = first['captureToFirstOnsetMs'] < record['samples'] / 16
    return result


def attribution_issues(record, events):
    """Reject units ending before this recording, while allowing VAD preroll.

    firstFrame alone can legitimately precede captureFirstFrame. Only a known,
    nonzero window end at or before the recording start proves this mismatch.
    Preserve events as evidence; never reassign their text to another record.
    """
    first = record.get('captureFirstFrame', 0)
    if not first:
        return []
    return [event for event in events
            if event.get('timing', {}).get('windowEndFrame', 0) and
            event['timing']['windowEndFrame'] <= first]


def tokens(text):
    return re.findall(r"[^\W_]+(?:['’][^\W_]+)*", text.casefold().replace('’', "'"))


def distance(a, b):
    previous = list(range(len(b) + 1))
    for i, word in enumerate(a, 1):
        current = [i]
        for j, other in enumerate(b, 1):
            current.append(min(previous[j] + 1, current[-1] + 1,
                               previous[j - 1] + (word != other)))
        previous = current
    return previous[-1]


def lexical_alerts(reference, hypothesis, language):
    # These are review hints, not semantic equivalence rules. For example,
    # "não" may legitimately move and a number may be written out in words.
    negative = ({'não', 'nunca', 'jamais', 'sem', 'nada', 'nenhum', 'nenhuma', 'ninguém', 'nem'}
                if language == 'pt' else
                {'not', 'never', 'no', 'without', 'neither', 'nor', "don't", "doesn't", "didn't",
                 "can't", "cannot", "won't", "isn't", "wasn't"})
    ref, hyp = tokens(reference), tokens(hypothesis)
    alerts = []
    if sorted(w for w in ref if w in negative) != sorted(w for w in hyp if w in negative):
        alerts.append('negation-lexemes-differ: manual meaning review required')
    numbers = lambda text: re.findall(r'\d+(?:[.,:/-]\d+)*', text)
    if numbers(reference) != numbers(hypothesis):
        alerts.append('written-numbers-differ: may be digit/word formatting; review required')
    return alerts


def read_trace(path, expected):
    if not path.exists():
        return {'status': 'MISSING', 'records': {}, 'events': {}, 'summary': None}
    data = path.read_bytes()
    # A live writer can be between bytes of UTF-8 or a JSON line. Only consume
    # complete lines. A malformed complete line is an error, never skipped.
    boundary = data.rfind(b'\n') + 1
    rows = [json.loads(line) for line in data[:boundary].decode('utf-8').splitlines()
            if line.startswith('{')]
    records, events = {}, {}
    summary = None
    for row in rows:
        if row.get('recordingResult'):
            name = row['recording']
            if name in records:
                raise ValueError(f'Duplicate recording result: {path}: {name}')
            records[name] = row
        elif 'state' in row and 'recording' in row:
            events.setdefault(row['recording'], []).append(row)
        elif 'result' in row:
            if summary is not None:
                raise ValueError(f'Duplicate run summary: {path}')
            summary = row
    status = 'OPEN_TRACE' # File contents alone cannot prove a process is alive.
    if summary is not None:
        if boundary != len(data) or len(records) != expected or summary.get('recordings') != expected:
            status = 'INCOMPLETE'
        elif summary.get('result') != 'PASS' or any(r['errors'] for r in records.values()):
            status = 'FUNCTIONAL_FAIL'
        else:
            status = 'FUNCTIONAL_PASS'
    invalid = {name: issues for name, record in records.items()
               if (issues := attribution_issues(record, events.get(name, [])))}
    return {'status': 'INVALID_ATTRIBUTION' if invalid else status,
            'nativeStatus': status, 'records': records, 'events': events,
            'attributionIssues': invalid, 'summary': summary}


def compare(directory, expected=30):
    report = {'scope': 'Development recordings; no meaning, acoustic, physical or offline acceptance',
              'meaningAcceptance': 'PENDING', 'expectedPerRun': expected, 'runs': {}, 'pairs': [],
              'timingScope': 'One first onset per recording; software capture clock includes speech duration. VAD is an estimate; WAV end is not acoustic speech end. Percentiles only with >=30 valid recordings.'}
    for language in ('pt', 'en'):
        runs = {name: read_trace(directory / f'{language}-{name}.jsonl', expected)
                for name in ('control', 'comma-boundary')}
        for name, run in runs.items():
            records = list(run['records'].values())
            words = sum(len(tokens(r['reference'])) for r in records)
            edits = sum(distance(tokens(r['reference']), tokens(r['original'])) for r in records)
            timing = [record_timing(r, run['events'].get(r['recording'], [])) for r in records]
            eligible = not run.get('attributionIssues')
            valid_timing = timing if eligible else []
            report['runs'][f'{language}-{name}'] = {
                'status': run['status'], 'completed': len(records),
                'nativeStatus': run.get('nativeStatus', run['status']),
                'comparisonEligible': eligible,
                'attributionIssues': run.get('attributionIssues', {}),
                'functionalFailures': sum(bool(r['errors']) for r in records),
                'nonSpokenEventsToReview': sum(e['state'] in NON_SPOKEN_STATES
                    for events in run['events'].values() for e in events),
                'sourceWER': edits / words if words and eligible else None,
                'rawSourceWER': edits / words if words else None,
                'recordingsWithPartialConfirmation': sum(t['hasPartialConfirmation'] for t in valid_timing) if eligible else None,
                'recordingsWithOnsetBeforeInputEnd': sum(t['firstOnsetBeforeInputEnd'] is True for t in valid_timing) if eligible else None,
                'captureToFirstOnset': distribution([t['captureToFirstOnsetMs'] for t in valid_timing]),
                'firstAfterVadEndEstimated': distribution([t['firstAfterVadEndEstimatedMs'] for t in valid_timing]),
                'summary': run['summary'],
            }
        baseline, candidate = runs['control']['records'], runs['comma-boundary']['records']
        for name in sorted(baseline.keys() & candidate.keys()):
            before, after = baseline[name], candidate[name]
            for key in ('sha256', 'reference', 'translationReference', 'language', 'split'):
                if before[key] != after[key]:
                    raise ValueError(f'Unmatched {key} for {name}')
            ref = tokens(before['reference'])
            pair = {'recording': name, 'sha256': before['sha256'], 'language': language,
                    'reference': before['reference'], 'translationReference': before['translationReference'],
                    'transcriptChanged': before['original'] != after['original'],
                    'translationChanged': before['translation'] != after['translation'],
                    'reviewRequired': True}
            for mode, row in [('control', before), ('candidate', after)]:
                events = runs['control' if mode == 'control' else 'comma-boundary']['events'].get(name, [])
                issues = attribution_issues(row, events)
                pair[mode] = {key: row[key] for key in ('original', 'translation', 'errors', 'unknownUnits', 'speakers')}
                pair[mode].update(wordEdits=distance(ref, tokens(row['original'])),
                    comparisonEligible=not issues, attributionIssues=issues,
                    timing=record_timing(row, [] if issues else events),
                    rawTiming=record_timing(row, events),
                    lexicalAlerts=lexical_alerts(before['reference'], row['original'], language),
                    revisions=[e for e in events if e['state'] in ('revision-conflict', 'revision-preserved')],
                    nonSpokenEvents=[e for e in events if e['state'] in NON_SPOKEN_STATES],
                    confirmedUnits=[{'text': e['original'], 'windowEndFrame': e['timing'].get('windowEndFrame'),
                                     'endpointReason': e['timing'].get('endpointReason')}
                                    for e in events if e['state'] == 'recognized'])
            # A late unit can also leave its true source recording artificially
            # empty. Do not compare any WER pair from a tainted run, even when
            # that source record cannot be identified without reattribution.
            pair['comparisonEligible'] = (all(pair[mode]['comparisonEligible'] for mode in ('control', 'candidate'))
                and all(not run.get('attributionIssues') for run in runs.values()))
            report['pairs'].append(pair)
    complete = all(r['status'] in ('FUNCTIONAL_PASS', 'FUNCTIONAL_FAIL') for r in report['runs'].values())
    report['comparisonComplete'] = complete and len(report['pairs']) == expected * 2
    report['nativeRunsComplete'] = all(r['nativeStatus'] in ('FUNCTIONAL_PASS', 'FUNCTIONAL_FAIL')
                                       for r in report['runs'].values())
    report['functionalRegressionRecordings'] = [r['recording'] for r in report['pairs']
        if r['candidate']['errors'] and not r['control']['errors']]
    report['increasedWordErrorRecordings'] = [r['recording'] for r in report['pairs']
        if r['comparisonEligible'] and r['candidate']['wordEdits'] > r['control']['wordEdits']]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--expected', type=int, default=30)
    parser.add_argument('--allow-incomplete', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.expected <= 30:
        parser.error('Expected 1..30 recordings per run')
    report = compare(args.directory, args.expected)
    print(json.dumps(report, indent=2, ensure_ascii=False))
    # A complete report is not an approval, regardless of this tool's exit code.
    if not report['comparisonComplete'] and not args.allow_incomplete:
        raise SystemExit(2)


if __name__ == '__main__':
    main()
