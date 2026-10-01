#!/bin/bash
source "$(dirname "$0")/local-env.sh"
DIAGNOSTIC=0
if [ "${1:-}" = '--diagnostic' ]; then DIAGNOSTIC=1; shift; fi
/usr/bin/python3 scripts/verify-models.py --release
if [ "$DIAGNOSTIC" = 0 ]; then /usr/bin/python3 scripts/release-gates.py; fi
test -f build/whisper/lib/libwhisper.coreml.a || scripts/build-whisper.sh
test -f build/translation/lib/libtranslation-deps.a || scripts/build-translation.sh
test -f build/sherpa/lib/libspeech-deps.a || scripts/build-sherpa.sh
/usr/bin/python3 scripts/prepare-model-resources.py
/usr/bin/python3 scripts/generate-project.py
scripts/project-local.sh /usr/bin/xcodebuild -project LocalVoiceTranslator.xcodeproj -scheme LocalVoiceTranslator -configuration Release -destination 'platform=macOS,arch=arm64' -derivedDataPath "$ROOT/build/DerivedData" -clonedSourcePackagesDirPath "$ROOT/third_party/SourcePackages" -disableAutomaticPackageResolution build "$@" 2>&1 | tee "$ROOT/artifacts/build-release.log"
if [ "$DIAGNOSTIC" = 1 ]; then
    DESTINATION="$ROOT/artifacts/LocalVoiceTranslator-Diagnostic.app"
else
    DESTINATION="$ROOT/artifacts/LocalVoiceTranslator.app"
fi
PACKAGE_STAGE="$(/usr/bin/mktemp -d "$ROOT/build/package.XXXXXX")"
STAGED_APP="$PACKAGE_STAGE/LocalVoiceTranslator.app"
# A clean staging directory prevents removed models surviving a ditto overlay and
# invalidating the resource seal. Keep the previous artifact until checks pass.
scripts/project-local.sh /usr/bin/ditto "$ROOT/build/DerivedData/Build/Products/Release/LocalVoiceTranslator.app" "$STAGED_APP"
/usr/bin/codesign --verify --deep --strict "$STAGED_APP"
/usr/bin/python3 scripts/verify-models.py --root "$STAGED_APP/Contents/Resources/ModelResources" --release
scripts/verify-offline.sh "$STAGED_APP" > artifacts/offline-static-audit.log
if [ -e "$DESTINATION" ]; then /bin/mv "$DESTINATION" "$PACKAGE_STAGE/previous.app"; fi
/bin/mv "$STAGED_APP" "$DESTINATION"
echo "PASS personal application: $DESTINATION. Physical/offline acceptance is recorded separately."
