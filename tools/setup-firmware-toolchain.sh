#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
. "$ROOT/tools/task-logging.sh"

require_tool() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required tool: $1" >&2
		echo "Run 'mise install' first, or install it manually." >&2
		exit 1
	fi
}

require_tool arduino-cli

tasklog_begin "Firmware toolchain setup"
tasklog_step SETUP "Update Arduino package index" arduino-cli core update-index
tasklog_step SETUP "Install ESP32 Arduino core" arduino-cli core install esp32:esp32
tasklog_step SETUP "Install SSD1306 and GFX libraries" \
	arduino-cli lib install "Adafruit SSD1306" "Adafruit GFX Library"
mkdir -p "$ROOT/.mise-state"
touch "$ROOT/.mise-state/firmware-toolchain"
tasklog_end "3 steps"
