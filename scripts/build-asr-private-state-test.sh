#!/bin/bash
source "$(dirname "$0")/local-env.sh"
test -f build/whisper/lib/libwhisper.coreml.a || scripts/build-whisper.sh
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -fobjc-arc \
    -isystem third_party/ctranslate2/include -Ithird_party/whisper.cpp/include -Ithird_party/whisper.cpp/ggml/include \
    tests/cpp/ASRPrivateStateTests.mm engine/asr/WhisperASR.cpp -Lbuild/whisper/lib \
    -lwhisper -lwhisper.coreml -framework CoreML -lggml -lggml-cpu -lggml-metal -lggml-blas -lggml-base \
    -framework Foundation -framework Metal -framework MetalKit -framework Accelerate -o build/asr-private-state-tests
