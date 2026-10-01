#!/bin/bash
source "$(dirname "$0")/local-env.sh"
# Native identity regressions need speaker/segmentation backends, not an ASR,
# translation or TTS startup. Build only their actual production dependencies.
for spec in 'SpeakerRevisionTests speaker-revision-tests' 'SpeakerAlternationTests speaker-alternation-tests'; do
    read -r source_name binary_name <<< "$spec"
    scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
        -isystem build/sherpa/_deps/onnxruntime-src/include -isystem build/sherpa/_deps/kaldi_native_fbank-src \
        -isystem third_party/ctranslate2/include \
        "tests/cpp/$source_name.cpp" engine/speakers/SpeakerEmbeddingEngine.cpp \
        engine/speakers/OverlapDetector.cpp engine/speakers/OnlineDiarizer.cpp engine/separation/SeparationEngine.cpp \
        build/sherpa/lib/libspeech-deps.a \
        -framework Foundation -framework CoreML -framework Metal -framework Accelerate -o "build/$binary_name"
done
