#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O1 -g -Wall -Wextra -Werror \
    -Wno-deprecated-declarations -fsanitize=address,undefined -fno-omit-frame-pointer \
    -isystem third_party/ctranslate2/include tests/cpp/ASRBackendTests.cpp -o build/asr-backend-tests
scripts/project-local.sh build/asr-backend-tests | tee artifacts/coreml-comparison/backend-tests.log
if [ "${1:-}" = '--inference' ]; then
    scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
        -isystem third_party/ctranslate2/include -Ithird_party/whisper.cpp/include -Ithird_party/whisper.cpp/ggml/include \
        tests/cpp/ASRReuseTests.cpp engine/asr/WhisperASR.cpp -Lbuild/whisper/lib \
        -lwhisper -lwhisper.coreml -framework CoreML -lggml -lggml-cpu -lggml-metal -lggml-blas -lggml-base \
        -framework Foundation -framework Metal -framework MetalKit -framework Accelerate -o build/asr-reuse-tests
    scripts/project-local.sh build/asr-reuse-tests > artifacts/coreml-comparison/reuse-tests.log 2>&1
fi
