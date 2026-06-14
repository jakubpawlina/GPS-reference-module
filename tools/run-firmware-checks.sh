#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
. "$ROOT/tools/task-logging.sh"
FIRMWARE_DIR="$ROOT/firmware/gps_reference_module"
usage() {
	cat <<'EOF'
Usage: ./tools/run-firmware-checks.sh <command>

Commands:
  compile   Compile the firmware with arduino-cli for esp32:esp32:esp32
  docs      Generate Doxygen documentation
EOF
}

require_tool() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required tool: $1" >&2
		echo "Run 'mise install' first, or install it manually." >&2
		exit 1
	fi
}

run_compile() {
	require_tool arduino-cli
	local build_dir
	build_dir="$(mktemp -d /tmp/gps-reference-arduino-build.XXXXXX)"
	trap "rm -rf \"$build_dir\"" EXIT
	tasklog_begin "ESP32 firmware build"
	tasklog_step BUILD "Compile esp32:esp32:esp32 (warnings=all)" \
		arduino-cli compile \
		--fqbn esp32:esp32:esp32 \
		--warnings all \
		--build-path "$build_dir" \
		"$FIRMWARE_DIR"
	tasklog_end "1 step"
}

run_docs() {
	require_tool doxygen
	mkdir -p "$FIRMWARE_DIR/build/doxygen"
	tasklog_begin "Firmware API documentation"
	tasklog_step BUILD "Generate Doxygen HTML documentation" \
		bash -c 'cd "$1" && doxygen Doxyfile' _ "$FIRMWARE_DIR"
	tasklog_end "1 step, output=firmware/gps_reference_module/build/doxygen"
}

main() {
	if [[ $# -ne 1 ]]; then
		usage
		exit 1
	fi

	case "$1" in
	compile) run_compile ;;
	docs) run_docs ;;
	*)
		usage
		exit 1
		;;
	esac
}

main "$@"
