#!/usr/bin/python3
"""Summarize development traces; never equate a VAD estimate with acoustic truth."""
import argparse
import collections
import json
import math
from pathlib import Path


def summary(values):
    values = sorted(values)
    return dict(count=len(values), minimum=min(values) if values else None,
                maximum=max(values) if values else None,
                p50=values[math.ceil(len(values)*.5)-1] if len(values) >= 30 else None,
                p95=values[math.ceil(len(values)*.95)-1] if len(values) >= 30 else None)


def analyze(path):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.startswith('{')]
    units = {}
    event_states = collections.Counter()
    stage_values = collections.defaultdict(lambda: collections.defaultdict(list))
    onset_exclusions = collections.Counter()
    for row in rows:
        state = row.get('state')
        if not state:
            continue
        event_states[state] += 1
        key = (row.get('route', ''), row.get('session', 0), row.get('segment', 0))
        unit = units.setdefault(key, dict(accepted=False, states=[], language='unknown'))
        unit['states'].append(state)
        if row.get('language'):
            unit['language'] = row['language']
        if state == 'recognized':
            unit['accepted'] = True
        timing = row.get('timing', {})
        # One observation per unit/stage, taken from its immutable playback identity.
        if state == 'playing':
            for field, value in timing.items():
                if field.endswith('Ms') and isinstance(value, (int, float)):
                    stage_values[unit['language']][field].append(value)
        if state == 'audio-onset':
            estimate = timing.get('afterVadEndEstimatedMs')
            if estimate is not None:
                stage_values[unit['language']]['afterVadEndEstimatedMs'].append(estimate)
            else:
                reason = {0: 'streaming-no-silence-endpoint', 2: 'hard-window-cut'}.get(timing.get('endpointReason'), 'missing-or-invalid-clock')
                onset_exclusions[reason] += 1
    accepted = [u for u in units.values() if u['accepted']]
    outcomes = collections.Counter()
    for unit in accepted:
        states = unit['states']
        terminal = next((s for s in reversed(states) if s in ('played', 'dropped', 'error', 'unavailable')), 'pending')
        outcomes[terminal] += 1
    return dict(input=str(path), scope='Development WAV. Speech end is last-positive VAD, not an annotated acoustic boundary.',
                percentilesMinimumSamples=30, eventStates=dict(event_states), acceptedUnits=len(accepted),
                acceptedOutcomes=dict(outcomes), afterVadEndExclusions=dict(onset_exclusions),
                stagesByLanguage={lang: {key: summary(values) for key, values in stages.items()} for lang, stages in stage_values.items()},
                runSummary=next((r for r in reversed(rows) if 'result' in r), None))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('traces', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps([analyze(path) for path in args.traces], indent=2, ensure_ascii=False)+'\n')
