#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
 -isystem third_party/ctranslate2/include -isystem third_party/sherpa-onnx \
 tests/cpp/VoiceCacheBenchmark.cpp engine/tts/TTSEngine.cpp \
 build/sherpa/lib/libspeech-deps.a -framework Foundation -framework CoreML -framework Accelerate \
 -o "${1:-build/voice-cache-benchmark}"
