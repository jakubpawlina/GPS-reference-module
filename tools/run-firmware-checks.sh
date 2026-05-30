#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
FIRMWARE_DIR="$ROOT/firmware/gps_reference_module"
OUT_DIR="$ROOT/tests/.build"
OUT_BIN="$OUT_DIR/test_gps_processing"
FIRMWARE_SRC="$ROOT/firmware/gps_reference_module/src"

usage() {
  cat <<'EOF'
Usage: ./tools/run-firmware-checks.sh <command>

Commands:
  test      Run host-side firmware tests
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

run_test() {
  mkdir -p "$OUT_DIR"

  g++ \
    -std=c++17 \
    -Wall \
    -Wextra \
    -pedantic \
    "$ROOT/tests/firmware/test_gps_processing.cpp" \
    "$FIRMWARE_SRC/gps_processing.cpp" \
    "$FIRMWARE_SRC/status_presentation.cpp" \
    "$FIRMWARE_SRC/nmea_stream_framer.cpp" \
    -o "$OUT_BIN"

  "$OUT_BIN"
}

run_compile() {
  require_tool arduino-cli
  local build_dir
  build_dir="$(mktemp -d /tmp/gps-reference-arduino-build.XXXXXX)"
  arduino-cli compile \
    --fqbn esp32:esp32:esp32 \
    --build-path "$build_dir" \
    "$FIRMWARE_DIR"
}

run_docs() {
  require_tool doxygen
  mkdir -p "$FIRMWARE_DIR/build/doxygen"
  (cd "$FIRMWARE_DIR" && doxygen Doxyfile)
}

main() {
  if [[ $# -ne 1 ]]; then
    usage
    exit 1
  fi

  case "$1" in
    test) run_test ;;
    compile) run_compile ;;
    docs) run_docs ;;
    *)
      usage
      exit 1
      ;;
  esac
}

main "$@"
