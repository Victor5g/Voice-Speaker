#!/bin/bash
source "$(dirname "$0")/local-env.sh"
test -f build/translation/lib/libtranslation-deps.a || scripts/build-translation.sh
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
    -Ithird_party/ctranslate2/include -Ithird_party/sentencepiece/src \
    tests/cpp/TranslationBenchmark.cpp engine/translation/TranslationEngine.cpp \
    build/translation/lib/libtranslation-deps.a -framework Accelerate -o build/translation-benchmark
