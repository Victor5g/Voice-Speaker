#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
export PYTHONDONTWRITEBYTECODE=1
exec /usr/bin/python3 "$ROOT/scripts/safe_clean.py" "$@"
