#!/bin/bash
source "$(dirname "$0")/local-env.sh"
mkdir -p build/webrtcvad
OBJECTS=()
while IFS= read -r file; do
    object="build/webrtcvad/$(basename "${file%.c}").o"
    scripts/project-local.sh /usr/bin/xcrun clang -std=c11 -O2 -DWEBRTC_POSIX -Ithird_party/webrtcvad -c "$file" -o "$object"
    OBJECTS+=("$object")
done < <(/usr/bin/find third_party/webrtcvad -name '*.c' -type f | sort)
scripts/project-local.sh /usr/bin/ar rcs build/webrtcvad/libwebrtcvad.a "${OBJECTS[@]}"
