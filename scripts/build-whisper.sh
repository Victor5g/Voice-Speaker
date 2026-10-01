#!/bin/bash
source "$(dirname "$0")/local-env.sh"
CMAKE="$ROOT/tools/downloads/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake"
test -x "$CMAKE" || { echo 'Missing project-local CMake 3.31.8; see THIRD_PARTY.md'; exit 2; }
SDK_PATH="$(scripts/project-local.sh /usr/bin/xcrun --sdk macosx --show-sdk-path)"
# Xcode upgrades invalidate cached SDK/framework paths; regenerate the cache.
scripts/project-local.sh "$CMAKE" --fresh -S third_party/whisper.cpp -B build/whisper -G 'Unix Makefiles' \
    -DCMAKE_OSX_SYSROOT="$SDK_PATH" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$ROOT/build/whisper/lib" \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$ROOT/build/whisper/bin" \
    -DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local' \
    -DBUILD_SHARED_LIBS=OFF -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=ON \
    -DWHISPER_BUILD_SERVER=OFF -DWHISPER_CURL=OFF -DWHISPER_SDL2=OFF -DWHISPER_FFMPEG=OFF \
    -DWHISPER_COREML=ON -DWHISPER_COREML_ALLOW_FALLBACK=OFF -DGGML_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON -DGGML_ACCELERATE=ON \
    -DGGML_BLAS=ON -DGGML_BLAS_VENDOR=Apple -DGGML_OPENMP=OFF -DGGML_NATIVE=OFF
scripts/project-local.sh "$CMAKE" --build build/whisper --target whisper-cli whisper --parallel 6
