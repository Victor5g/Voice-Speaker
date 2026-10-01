#!/bin/bash
source "$(dirname "$0")/local-env.sh"
test -f build/whisper/lib/libwhisper.coreml.a || scripts/build-whisper.sh
test -f build/translation/lib/libtranslation-deps.a || scripts/build-translation.sh
test -f build/sherpa/lib/libspeech-deps.a || scripts/build-sherpa.sh
/usr/bin/python3 scripts/prepare-model-resources.py
/usr/bin/python3 scripts/generate-project.py
scripts/project-local.sh /usr/bin/xcodebuild -project LocalVoiceTranslator.xcodeproj -scheme LocalVoiceTranslator -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath "$ROOT/build/DerivedData" -clonedSourcePackagesDirPath "$ROOT/third_party/SourcePackages" -disableAutomaticPackageResolution build "$@" 2>&1 | tee "$ROOT/artifacts/build-debug.log"
