#!/bin/bash
# Source this file. No HOME, PATH or global settings are modified.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
export TMPDIR="$ROOT/cache/tmp/"
export CLANG_MODULE_CACHE_PATH="$ROOT/cache/clang"
export SWIFT_MODULECACHE_PATH="$ROOT/cache/swift"
export XDG_CACHE_HOME="$ROOT/cache/xdg"
export CFFIXED_USER_HOME="$ROOT/cache/preferences"
export PYTHONDONTWRITEBYTECODE=1
mkdir -p "$TMPDIR" "$CLANG_MODULE_CACHE_PATH" "$SWIFT_MODULECACHE_PATH" "$XDG_CACHE_HOME" "$CFFIXED_USER_HOME" "$ROOT/build" "$ROOT/artifacts"
cd "$ROOT"
