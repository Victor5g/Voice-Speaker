#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -isystem third_party/ctranslate2/include tests/cpp/GlossaryTests.cpp -framework Foundation -o build/glossary-tests
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -isystem third_party/ctranslate2/include -isystem third_party/sentencepiece/src \
  tests/cpp/GlossaryTranslationTests.cpp engine/translation/TranslationEngine.cpp build/translation/lib/libtranslation-deps.a \
  -framework Foundation -framework Accelerate -o build/glossary-translation-tests
