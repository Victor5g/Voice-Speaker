#!/bin/bash
source "$(dirname "$0")/local-env.sh"
CMAKE="$ROOT/tools/downloads/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake"
scripts/project-local.sh "$CMAKE" -S third_party/sherpa-onnx -B build/sherpa -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local' -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX="$ROOT/build/sherpa/install" -DCMAKE_CXX_FLAGS=-DLVT_PRIVATE_RUNTIME \
    -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_TESTS=OFF \
    -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF \
    -DSHERPA_ONNX_ENABLE_BINARY=OFF -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF \
    -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=OFF \
    -DSHERPA_ONNX_ENABLE_TTS=ON -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=ON
scripts/project-local.sh "$CMAKE" --build build/sherpa --target sherpa-onnx-c-api --parallel 6
mkdir -p build/sherpa/lib
/usr/bin/python3 - <<'PY'
from pathlib import Path
import subprocess
archives = sorted(str(p) for p in Path('build/sherpa').rglob('*.a') if 'lib/libspeech-deps.a' not in str(p))
subprocess.run(['scripts/project-local.sh', '/usr/bin/libtool', '-static', '-o', 'build/sherpa/lib/libspeech-deps.a'] + archives, check=True)
PY
