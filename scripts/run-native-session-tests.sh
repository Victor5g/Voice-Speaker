#!/bin/bash
# Compatibility entry point: test real production state with XCTest via Xcode.
source "$(dirname "$0")/local-env.sh"
exec scripts/run-tests.sh
