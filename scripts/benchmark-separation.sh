#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
    -isystem third_party/ctranslate2/include -isystem build/sherpa/_deps/onnxruntime-src/include -isystem third_party/sherpa-onnx \
    tests/cpp/SeparationBenchmark.cpp engine/separation/SeparationEngine.cpp engine/speakers/OverlapDetector.cpp \
    build/sherpa/lib/libspeech-deps.a -framework Foundation -framework Accelerate -o build/separation-benchmark
scripts/project-local.sh build/separation-benchmark > artifacts/separation-benchmark.jsonl 2> artifacts/separation-benchmark.err
