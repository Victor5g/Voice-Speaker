#!/bin/bash
source "$(dirname "$0")/local-env.sh"
scripts/build-vad-library.sh
test -f build/whisper/lib/libwhisper.coreml.a || scripts/build-whisper.sh
test -f build/translation/lib/libtranslation-deps.a || scripts/build-translation.sh
test -f build/sherpa/lib/libspeech-deps.a || scripts/build-sherpa.sh
/usr/bin/python3 scripts/prepare-model-resources.py
/usr/bin/python3 scripts/generate-project.py
scripts/project-local.sh /usr/bin/xcrun clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -fmodules-cache-path="$ROOT/cache/clang" -Wno-deprecated-declarations -isystem build/sherpa/_deps/onnxruntime-src/include -isystem build/sherpa/_deps/kaldi_native_fbank-src -isystem third_party/ctranslate2/include -isystem third_party/sentencepiece/src -isystem third_party/sherpa-onnx -Ithird_party/webrtcvad -Ithird_party/whisper.cpp/include -Ithird_party/whisper.cpp/ggml/include tests/cpp/AudioTests.cpp engine/audio/AudioCore.cpp engine/vad/VadWorker.cpp engine/asr/ASRWorker.cpp engine/speakers/SpeakerEmbeddingEngine.cpp engine/speakers/OverlapDetector.cpp engine/speakers/OnlineDiarizer.cpp engine/separation/SeparationEngine.cpp engine/asr/WhisperASR.cpp engine/conversation/ConversationPipeline.cpp engine/translation/TranslationEngine.cpp engine/tts/TTSEngine.cpp build/translation/lib/libtranslation-deps.a build/sherpa/lib/libspeech-deps.a build/webrtcvad/libwebrtcvad.a -Lbuild/whisper/lib -lwhisper -lwhisper.coreml -framework CoreML -lggml -lggml-cpu -lggml-metal -lggml-blas -lggml-base -framework Foundation -framework Metal -framework MetalKit -framework Accelerate -o build/audio-tests
scripts/project-local.sh "$ROOT/build/audio-tests" 2>&1 | tee artifacts/cpp-tests.log
if [ "${1:-}" != '--cpp-only' ]; then
    scripts/project-local.sh /usr/bin/xcodebuild -project LocalVoiceTranslator.xcodeproj -scheme LocalVoiceTranslator -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath "$ROOT/build/DerivedData" -clonedSourcePackagesDirPath "$ROOT/third_party/SourcePackages" -disableAutomaticPackageResolution -parallel-testing-enabled NO -collect-test-diagnostics never test 2>&1 | tee artifacts/swift-tests.log
fi
