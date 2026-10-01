#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/build-production-test.sh tests/cpp/CallRoutingGolden.cpp build/call-routing-soak
/usr/bin/python3 - <<'PY'
from pathlib import Path
import hashlib,json
h=hashlib.sha256()
for path in sorted(p for folder in ('engine','bridge') for p in Path(folder).rglob('*') if p.is_file()):
    h.update(str(path).encode());h.update(path.read_bytes())
Path('artifacts/call-soak-provenance.json').write_text(json.dumps(dict(engineFingerprint=h.hexdigest(),binarySHA256=hashlib.sha256(Path('build/call-routing-soak').read_bytes()).hexdigest(),repeatSeconds=1800,scope='Two production cores and synthetic WAV; no meeting app or hardware'),indent=2)+'\n')
PY
/usr/bin/caffeinate -i scripts/project-local.sh build/call-routing-soak models 1800 > artifacts/call-soak-30min.jsonl 2> artifacts/call-soak-30min.err
