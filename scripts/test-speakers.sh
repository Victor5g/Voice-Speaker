#!/bin/bash
source "$(dirname "$0")/local-env.sh"
bash scripts/build-speaker-tests.sh
scripts/project-local.sh build/speaker-revision-tests > artifacts/speaker-revision-tests.log 2>&1
scripts/project-local.sh build/speaker-alternation-tests models > artifacts/speaker-alternation-tests.jsonl 2> artifacts/speaker-alternation-tests.err
