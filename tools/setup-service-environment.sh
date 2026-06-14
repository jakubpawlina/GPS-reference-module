#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
. "$ROOT/tools/task-logging.sh"

create_environment() {
	python3 -m venv "$ROOT/.venv"
}

install_dependencies() {
	"$ROOT/.venv/bin/pip" install -r "$ROOT/service/requirements.txt"
}

tasklog_begin "Raspberry Pi service environment"
tasklog_step SETUP "Create or refresh local Python virtual environment" create_environment
tasklog_step SETUP "Install service dependencies" install_dependencies
mkdir -p "$ROOT/.mise-state"
touch "$ROOT/.mise-state/service-environment"
tasklog_end "2 steps, output=.venv"
