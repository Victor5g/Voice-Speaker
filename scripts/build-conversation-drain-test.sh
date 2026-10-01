#!/bin/bash
source "$(dirname "$0")/local-env.sh"
# Uses the production coordinator and linked native backends, but the test's
# absent model root ensures no model loading or inference occurs at runtime.
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O1 -g -Wno-deprecated-declarations \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -isystem build/sherpa/_deps/onnxruntime-src/include -isystem build/sherpa/_deps/kaldi_native_fbank-src \
    -isystem third_party/ctranslate2/include -isystem third_party/sentencepiece/src -isystem third_party/sherpa-onnx \
    tests/cpp/ConversationDrainTests.cpp engine/conversation/ConversationPipeline.cpp \
    engine/translation/TranslationEngine.cpp engine/tts/TTSEngine.cpp \
    build/translation/lib/libtranslation-deps.a build/sherpa/lib/libspeech-deps.a \
    -framework Foundation -framework CoreML -framework Metal -framework Accelerate -o build/conversation-drain-tests
