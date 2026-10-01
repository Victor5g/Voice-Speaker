#!/bin/bash
source "$(dirname "$0")/local-env.sh"
/usr/bin/python3 scripts/verify-models.py
scripts/build-asr-benchmark.sh
/usr/bin/python3 scripts/benchmark.py
