#!/bin/bash
source "$(dirname "$0")/local-env.sh"
test "$(uname -s)" = Darwin || { echo 'macOS required'; exit 1; }
scripts/project-local.sh /usr/bin/xcodebuild -version
scripts/project-local.sh /usr/bin/xcrun --find clang++
if [ "${1:-}" = '--download' ]; then
    scripts/project-local.sh /usr/bin/python3 scripts/download-local.py
    scripts/project-local.sh /usr/bin/python3 scripts/fetch-coreml-model.py
    scripts/project-local.sh /usr/bin/python3 scripts/fetch-translation-models.py
    test -x tools/downloads/conversion-venv/bin/python || scripts/project-local.sh /usr/bin/python3 -m venv tools/downloads/conversion-venv
    scripts/project-local.sh tools/downloads/conversion-venv/bin/python -m pip --disable-pip-version-check install --only-binary=:all: --cache-dir "$ROOT/cache/pip" -r scripts/conversion-requirements.lock
    scripts/project-local.sh tools/downloads/conversion-venv/bin/python scripts/convert-translation-models.py
    scripts/project-local.sh /usr/bin/python3 scripts/fetch-tts-models.py
    scripts/project-local.sh /usr/bin/python3 scripts/fetch-speaker-models.py
    /usr/bin/python3 scripts/verify-models.py --release
fi
/usr/bin/python3 scripts/generate-project.py
echo 'No global installs. Restore CMake, native models and development converter with scripts/bootstrap-local.sh --download. Human fixtures: scripts/fetch-human-fixtures.py.'
