#!/bin/bash
source "$(dirname "$0")/local-env.sh"
if [ "${LVT_VALIDATION_AWAKE:-0}" != 1 ]; then
    export LVT_VALIDATION_AWAKE=1
    exec /usr/bin/caffeinate -i "$ROOT/scripts/validate-personal.sh" "$@"
fi
VALIDATION_FINGERPRINT="$(/usr/bin/python3 scripts/release-gates.py --fingerprint)"
/usr/bin/python3 scripts/verify-models.py --release | tee artifacts/model-verification.log
/usr/bin/python3 scripts/generate-conversation-fixtures.py
scripts/run-tests.sh
bash scripts/test-realtime-improvements.sh
bash scripts/build-conversation-drain-test.sh
scripts/project-local.sh build/conversation-drain-tests > artifacts/conversation-drain-tests.log 2>&1
scripts/project-local.sh /usr/bin/python3 scripts/test-project-local.py > artifacts/project-local-tests.log 2>&1
scripts/test-asr-pipeline.sh
scripts/project-local.sh /usr/bin/python3 tests/cleanup_tests.py > artifacts/cleanup-tests.log 2>&1
scripts/project-local.sh /usr/bin/python3 tests/acceptance_evidence_tests.py > artifacts/acceptance-evidence-tests.log 2>&1
scripts/test-conversation.sh
bash scripts/test-speakers.sh
scripts/test-separated-echo.sh
scripts/build-production-test.sh tests/cpp/CallRoutingGolden.cpp build/call-routing-golden
scripts/project-local.sh build/call-routing-golden models > artifacts/call-routing-golden.jsonl 2> artifacts/call-routing-golden.err
scripts/build-production-test.sh tests/cpp/ReliabilityTests.cpp build/reliability-tests
scripts/project-local.sh build/reliability-tests > artifacts/reliability-tests.log 2> artifacts/reliability-tests.err
scripts/test-scenarios.sh
scripts/project-local.sh build/conversation-evaluation models build/human-golden-config.json > artifacts/human-conversation-golden.jsonl 2> artifacts/human-conversation-golden.err
scripts/project-local.sh build/conversation-evaluation models build/echo-config.json > artifacts/digital-echo.jsonl 2> artifacts/digital-echo.err
test "$VALIDATION_FINGERPRINT" = "$(/usr/bin/python3 scripts/release-gates.py --fingerprint)" || { echo 'FAIL source changed during tests; rerun validation'; exit 1; }
/usr/bin/python3 scripts/release-gates.py --record
