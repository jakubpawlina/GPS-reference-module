#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
DOCS_DIR="$ROOT/firmware/gps_reference_module/build/doxygen/html"
PORT="${PORT:-8080}"

if [[ ! -f "$DOCS_DIR/index.html" ]]; then
	"$ROOT/tools/run-firmware-checks.sh" docs
fi

trap 'exit 0' INT TERM
python3 -m http.server "$PORT" --directory "$DOCS_DIR"
