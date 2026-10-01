#!/bin/bash
source "$(dirname "$0")/local-env.sh"
for spec in 'LatencyTimingTests latency-timing-tests' 'VoiceCacheTests voice-cache-tests' 'StreamingPolicyTests streaming-policy-tests' 'InferenceArbiterTests inference-arbiter-tests' 'BoundaryEvidenceTests boundary-evidence-tests' 'CorpusDrainTrackerTests corpus-drain-tracker-tests' 'ASRControlsTests asr-controls-tests' 'ProcessingOptionsTests processing-options-tests'; do
    read -r source_name binary_name <<< "$spec"
    scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O1 -g -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer -isystem third_party/ctranslate2/include "tests/cpp/$source_name.cpp" -o "build/$binary_name"
    scripts/project-local.sh "build/$binary_name" | tee "artifacts/$binary_name.log"
done
