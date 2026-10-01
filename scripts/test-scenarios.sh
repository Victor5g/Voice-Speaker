#!/bin/bash
source "$(dirname "$0")/local-env.sh"
/usr/bin/python3 scripts/generate-conversation-fixtures.py
scripts/build-production-test.sh tests/cpp/ConversationGolden.cpp build/conversation-evaluation
/usr/bin/python3 - <<'PY'
import subprocess,json
from pathlib import Path
results=[]
for config in sorted(Path('build').glob('scenario-*.json')):
    with open('artifacts/'+config.stem+'.jsonl','w') as out, open('artifacts/'+config.stem+'.err','w') as err:
        run=subprocess.run(['scripts/project-local.sh','build/conversation-evaluation','models',str(config)],stdout=out,stderr=err)
    results.append(dict(scenario=config.stem,exitCode=run.returncode))
    print(config.stem,run.returncode,flush=True)
Path('artifacts/scenario-results.json').write_text(json.dumps(results,indent=2)+'\n')
if any(r['exitCode'] for r in results):raise SystemExit(1)
PY
