#!/bin/bash
# Compatibility entry point: the old direct-compiler workaround is superseded by Xcode.
source "$(dirname "$0")/local-env.sh"
case "${1:-Debug}" in
    Debug) exec scripts/build-debug.sh ;;
    Release) exec scripts/build-release.sh --diagnostic ;;
    *) echo 'Debug or Release required'; exit 2 ;;
esac
