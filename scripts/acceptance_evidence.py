"""Read complete JSONL evidence; a recovered suffix cannot repair confirmed text."""
import json


def require_stable_conversation(path):
    records = []
    with path.open() as stream:
        for line in stream:
            if line.startswith('{'):
                records.append(json.loads(line))
    if not records or records[-1].get('result') != 'PASS':
        raise ValueError(f'{path.name}: missing terminal PASS')
    for record in records:
        if record.get('state', record.get('event')) in ('revision-conflict', 'revision-preserved'):
            raise ValueError(f'{path.name}: confirmed text revised; continuation recovery is not quality acceptance')
        for key in ('preservedRevisions', 'revisionConflicts', 'recoveredContinuations',
                    'outgoingRevisionConflicts', 'incomingRevisionConflicts'):
            value = record.get(key, 0)
            if not isinstance(value, (int, float)) or value != 0:
                raise ValueError(f'{path.name}: invalid or nonzero {key}')
    return records[-1]
