#!/bin/bash
source "$(dirname "$0")/local-env.sh"
/usr/bin/python3 - <<'PY'
import hashlib,json
from pathlib import Path
root=Path('tests/data/echo-regression').resolve()
for example in json.loads((root/'manifest.json').read_text())['cases']:
    for name in ('source','reference'):
        asset=example[name];path=Path(asset['filename']).resolve()
        if root not in path.parents or path.stat().st_size!=asset['expectedSize'] or hashlib.sha256(path.read_bytes()).hexdigest()!=asset['sha256']:
            raise SystemExit('FAIL echo regression fixture integrity')
PY
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O1 -g -Wall -Wextra -Werror \
    -isystem third_party/ctranslate2/include -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/cpp/SeparatedEchoRegression.cpp -o build/separated-echo-regression
scripts/project-local.sh build/separated-echo-regression | tee artifacts/separated-echo-regression.log
