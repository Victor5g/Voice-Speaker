#!/bin/bash
source "$(dirname "$0")/local-env.sh"
# Functional native checks run serially. These are not latency benchmarks.
mkdir -p artifacts/realtime-improvements
bash scripts/test-latency.sh
bash scripts/build-glossary-tests.sh
scripts/project-local.sh build/glossary-tests > artifacts/realtime-improvements/glossary-tests.log 2>&1
scripts/project-local.sh build/glossary-translation-tests models > artifacts/realtime-improvements/glossary-native.jsonl 2> artifacts/realtime-improvements/glossary-native.err
for spec in 'ASRControlsNative asr-controls-native' 'ProcessingRoutesNative processing-routes-native' 'PlaybackBacklogNative playback-backlog-native' 'GlossaryVoiceNative glossary-voice-native'; do
    read -r source_name binary_name <<< "$spec"
    bash scripts/build-production-test.sh "tests/cpp/$source_name.cpp" "build/$binary_name"
    scripts/project-local.sh "build/$binary_name" models > "artifacts/realtime-improvements/$binary_name.jsonl" 2> "artifacts/realtime-improvements/$binary_name.err"
done
bash scripts/build-production-test.sh tests/cpp/ConversationGolden.cpp build/realtime-conversation-golden
scripts/project-local.sh /usr/bin/python3 scripts/test-audio-protections.py
scripts/project-local.sh /usr/bin/python3 tests/performance_report_analysis_tests.py > artifacts/realtime-improvements/performance-parser-tests.log 2>&1
