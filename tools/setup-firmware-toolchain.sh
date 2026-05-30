#!/usr/bin/env bash
set -Eeuo pipefail

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required tool: $1" >&2
    echo "Run 'mise install' first, or install it manually." >&2
    exit 1
  fi
}

require_tool arduino-cli

arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit SSD1306" "Adafruit GFX Library"
