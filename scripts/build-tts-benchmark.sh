#!/bin/bash
source "$(dirname "$0")/local-env.sh"
test -f build/sherpa/lib/libspeech-deps.a || scripts/build-sherpa.sh
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
    -Ithird_party/ctranslate2/include -Ithird_party/sherpa-onnx \
    tests/cpp/TTSBenchmark.cpp engine/tts/TTSEngine.cpp \
    build/sherpa/lib/libspeech-deps.a -framework Foundation -framework Accelerate -o build/tts-benchmark
