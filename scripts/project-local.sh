#!/bin/bash
source "$(dirname "$0")/local-env.sh"
# User approved Xcode-managed temporary/cache writes on 2026-09-11.
# Resolve only this user's Darwin scratch directories, never all /private/var.
XCODE_TEMP="$(/usr/bin/getconf DARWIN_USER_TEMP_DIR)"
XCODE_CACHE="$(/usr/bin/getconf DARWIN_USER_CACHE_DIR)"
XCODE_TEMP="$(cd "$XCODE_TEMP" && pwd -P)"
XCODE_CACHE="$(cd "$XCODE_CACHE" && pwd -P)"
case "$XCODE_TEMP" in /private/var/folders/*/T) ;; *) echo 'Unexpected Darwin temporary directory' >&2; exit 1;; esac
case "$XCODE_CACHE" in /private/var/folders/*/C) ;; *) echo 'Unexpected Darwin cache directory' >&2; exit 1;; esac
case "$ROOT" in *\"*|*\\*) echo 'Unsupported repository path' >&2; exit 1;; esac
# Each concurrent invocation publishes a complete profile atomically. Writing
# directly to the shared path lets sandbox-exec read another launch's truncation.
LVT_PROFILE_TMP="$(mktemp "$ROOT/build/.project-local.sb.XXXXXX")"
trap 'rm -f -- "$LVT_PROFILE_TMP"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cat > "$LVT_PROFILE_TMP" <<EOF
(version 1)
(allow default)
(deny file-write*)
(allow file-write* (subpath "$ROOT") (literal "/dev/null") (literal "/dev/tty"))
(allow file-write* (subpath "$XCODE_TEMP") (subpath "$XCODE_CACHE"))
; XCTest's short-lived pseudo-terminal devices, not persistent filesystem files.
(allow file-write-data (literal "/dev/ptmx") (regex #"^/dev/ttys[0-9]+$"))
EOF
mv -f "$LVT_PROFILE_TMP" "$ROOT/build/project-local.sb"
trap - EXIT HUP INT TERM
exec /usr/bin/sandbox-exec -f "$ROOT/build/project-local.sb" "$@"
