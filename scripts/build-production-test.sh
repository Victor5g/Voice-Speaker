#!/bin/bash
source "$(dirname "$0")/local-env.sh"
SOURCE="${1:?C++ test source required}"
OUTPUT="${2:?Project-local output required}"
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -O2 -Wno-deprecated-declarations \
    -isystem build/sherpa/_deps/onnxruntime-src/include -isystem build/sherpa/_deps/kaldi_native_fbank-src -isystem third_party/ctranslate2/include -isystem third_party/sentencepiece/src -isystem third_party/sherpa-onnx \
    -Ithird_party/webrtcvad -Ithird_party/whisper.cpp/include -Ithird_party/whisper.cpp/ggml/include \
    "$SOURCE" engine/audio/AudioCore.cpp engine/vad/VadWorker.cpp engine/asr/ASRWorker.cpp engine/speakers/SpeakerEmbeddingEngine.cpp engine/speakers/OverlapDetector.cpp engine/speakers/OnlineDiarizer.cpp engine/separation/SeparationEngine.cpp \
    engine/asr/WhisperASR.cpp engine/conversation/ConversationPipeline.cpp engine/translation/TranslationEngine.cpp engine/tts/TTSEngine.cpp \
    build/webrtcvad/libwebrtcvad.a build/translation/lib/libtranslation-deps.a build/sherpa/lib/libspeech-deps.a \
    -Lbuild/whisper/lib -lwhisper -lwhisper.coreml -framework CoreML -lggml -lggml-cpu -lggml-metal -lggml-blas -lggml-base \
    -framework Foundation -framework Metal -framework MetalKit -framework Accelerate -o "$OUTPUT"
