#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
. "$ROOT/tools/task-logging.sh"

started_ms="$(tasklog_now_ms)"

"$ROOT/tools/run-tests.sh" all
"$ROOT/tools/build-wokwi-project.sh"

finished_ms="$(tasklog_now_ms)"
duration_ms="$((finished_ms - started_ms))"
printf '\n%s=== PASS%s  Full verification: 30 tests, 19 steps (%s)\n' \
	"$TASKLOG_BOLD$TASKLOG_GREEN" \
	"$TASKLOG_RESET" \
	"$(tasklog_duration "$duration_ms")"
