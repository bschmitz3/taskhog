#!/usr/bin/env bash
# M5-T7: roda a suíte de caos/robustez do Hub.
set -euo pipefail
cd "$(dirname "$0")/.."
python -m pytest tests/test_m5_*.py -v --tb=short
