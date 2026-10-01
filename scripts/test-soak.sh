#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/build-production-test.sh tests/cpp/ConversationGolden.cpp build/conversation-soak
/usr/bin/python3 - <<'PY'
from pathlib import Path
import hashlib,json
config=dict(repeatSeconds=1800,fixtureType='synthetic continuous real-time production pipeline; no hardware')
Path('build/soak-config.json').write_text(json.dumps(config)+'\n')
h=hashlib.sha256()
for path in sorted(p for folder in ('engine','bridge') for p in Path(folder).rglob('*') if p.is_file()):
    h.update(str(path).encode());h.update(path.read_bytes())
Path('artifacts/soak-provenance.json').write_text(json.dumps(dict(engineFingerprint=h.hexdigest(),binarySHA256=hashlib.sha256(Path('build/conversation-soak').read_bytes()).hexdigest(),config=config),indent=2)+'\n')
PY
# Temporary OS assertion, released when this test exits; no power setting changes.
/usr/bin/caffeinate -i scripts/project-local.sh build/conversation-soak models build/soak-config.json > artifacts/soak-30min.jsonl 2> artifacts/soak-30min.err
