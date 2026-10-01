#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
    -isystem third_party/ctranslate2/include -isystem build/sherpa/_deps/onnxruntime-src/include \
    -isystem build/sherpa/_deps/kaldi_native_fbank-src tests/cpp/SpeakerBenchmark.cpp engine/speakers/SpeakerEmbeddingEngine.cpp \
    build/sherpa/lib/libspeech-deps.a -framework Foundation -framework Accelerate -o build/speaker-benchmark
scripts/project-local.sh build/speaker-benchmark > artifacts/speaker-benchmark.jsonl 2> artifacts/speaker-benchmark.err
