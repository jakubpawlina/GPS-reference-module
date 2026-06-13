#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
FIRMWARE_DIR="${FIRMWARE_DIR:-$ROOT/firmware/gps_reference_module}"
ASSETS_DIR="${ASSETS_DIR:-$ROOT/simulation/assets}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT/simulation/wokwi}"
FQBN="${FQBN:-esp32:esp32:esp32}"
CHIP_SOURCE="${CHIP_SOURCE:-neo-m8n.chip.c}"
CHIP_BINARY="${CHIP_BINARY:-neo-m8n.chip.wasm}"
WOKWI_BUILDER_IMAGE="${WOKWI_BUILDER_IMAGE:-wokwi/builder-clang-wasm}"

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required tool: $1" >&2
    exit 1
  fi
}

require_tool arduino-cli
require_tool docker

python3 "$ROOT/tools/generate-wokwi-project.py" \
  --firmware-dir "$FIRMWARE_DIR" \
  --assets-dir "$ASSETS_DIR" \
  --output-dir "$OUTPUT_DIR"

arduino_build_dir="$(mktemp -d /tmp/gps-reference-arduino-build.XXXXXX)"
arduino_output_dir="$(mktemp -d /tmp/gps-reference-arduino-output.XXXXXX)"
trap 'rm -rf "$arduino_build_dir" "$arduino_output_dir"' EXIT

arduino-cli compile \
  --fqbn "$FQBN" \
  --build-path "$arduino_build_dir" \
  --output-dir "$arduino_output_dir" \
  "$FIRMWARE_DIR"

mapfile -t firmware_bins < <(find "$arduino_output_dir" -maxdepth 1 -type f -name '*.ino.bin')
mapfile -t firmware_elfs < <(find "$arduino_output_dir" -maxdepth 1 -type f -name '*.ino.elf')

if [[ ${#firmware_bins[@]} -ne 1 || ${#firmware_elfs[@]} -ne 1 ]]; then
  echo "Expected exactly one Arduino firmware BIN and ELF artifact" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR/build"
cp "${firmware_bins[0]}" "$OUTPUT_DIR/build/firmware.bin"
cp "${firmware_elfs[0]}" "$OUTPUT_DIR/build/firmware.elf"

docker run --rm \
  --user "$(id -u):$(id -g)" \
  --volume "$OUTPUT_DIR:/work" \
  --workdir /work \
  "$WOKWI_BUILDER_IMAGE" \
  clang \
  --target=wasm32-unknown-wasi \
  --sysroot /opt/wasi-libc \
  -nostartfiles \
  -Wl,--import-memory \
  -Wl,--export-table \
  -Wl,--no-entry \
  -Werror \
  -I. \
  -o "$CHIP_BINARY" \
  "$CHIP_SOURCE"

echo "built $OUTPUT_DIR"
