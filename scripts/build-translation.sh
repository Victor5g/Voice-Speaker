#!/bin/bash
source "$(dirname "$0")/local-env.sh"
CMAKE="$ROOT/tools/downloads/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake"
scripts/project-local.sh "$CMAKE" -S third_party/ctranslate2 -B build/ctranslate2 -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local' \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_CLI=OFF -DBUILD_TESTS=OFF -DWITH_MKL=OFF -DWITH_DNNL=OFF \
    -DWITH_CUDA=OFF -DWITH_CUDNN=OFF -DWITH_ACCELERATE=ON -DWITH_RUY=ON -DOPENMP_RUNTIME=NONE
scripts/project-local.sh "$CMAKE" --build build/ctranslate2 --parallel 6
scripts/project-local.sh "$CMAKE" -S third_party/sentencepiece -B build/sentencepiece -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local' -DSPM_ENABLE_SHARED=OFF -DSPM_BUILD_TEST=OFF \
    -DCMAKE_CXX_FLAGS=-Dgoogle=lvt_sentencepiece_google
scripts/project-local.sh "$CMAKE" --build build/sentencepiece --target sentencepiece-static --parallel 6
mkdir -p build/translation/lib
/usr/bin/python3 - <<'PY'
from pathlib import Path
import subprocess
archives = sorted(str(p) for p in Path('build/ctranslate2').rglob('*.a'))
archives.append('build/sentencepiece/src/libsentencepiece.a')
subprocess.run(['scripts/project-local.sh', '/usr/bin/libtool', '-static', '-o', 'build/translation/lib/libtranslation-deps.a'] + archives, check=True)
PY
