#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
. "$ROOT/tools/task-logging.sh"

tasklog_begin "Wokwi project generation"
tasklog_step BUILD "Generate project from tracked sources" \
  python3 "$ROOT/tools/generate-wokwi-project.py"
tasklog_end "1 step, output=simulation/wokwi"
