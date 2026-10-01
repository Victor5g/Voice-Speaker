#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -fobjc-arc \
    -isystem third_party/ctranslate2/include tests/cpp/CoreMLComputePlan.mm \
    -framework Foundation -framework CoreML -o build/coreml-compute-plan
